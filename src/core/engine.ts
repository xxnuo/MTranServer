import * as ort from 'onnxruntime-node';
import { parseFragment } from 'parse5';
import { Tokenizer } from './tokenizer.js';
import type { ConvertedModel } from '../converter/cache.js';

export interface TranslationOptions { sourceLang?: string; targetLang?: string; cacheSize?: number }
export interface TranslateOptions { qualityScores?: boolean; alignment?: boolean; html?: boolean }
export interface TranslationConfig { 'cpu-threads'?: number; 'max-length-break'?: number }
export class TranslationEngine {
  private session?: ort.InferenceSession;
  private src?: Tokenizer;
  private dst?: Tokenizer;
  private queue: Promise<unknown> = Promise.resolve();
  private pending = 0;
  private closing = false;
  private closed?: Promise<void>;
  private tokenLimit = 512;
  identity = '';
  constructor(private options: TranslationOptions = {}) {}
  get busy() { return this.pending > 0; }
  async init(model: ConvertedModel, config: TranslationConfig = {}) {
    if (this.session) return;
    this.tokenLimit = Math.max(8, config['max-length-break'] ?? 512);
    try {
      this.src = await Tokenizer.load(model.sourceVocab);
      this.dst = await Tokenizer.load(model.targetVocab);
      this.session = await ort.InferenceSession.create(model.model, {
        executionProviders: ['cpu'], graphOptimizationLevel: 'all',
        intraOpNumThreads: config['cpu-threads'] ?? 1, interOpNumThreads: 1,
      });
      this.identity = model.identity;
    } catch (error) { this.src?.close(); this.dst?.close(); throw error; }
  }
  translateAsync(text: string, options: TranslateOptions = {}): Promise<string> {
    if (this.closing || !this.session) return Promise.reject(new Error('Engine not initialized or closing'));
    this.pending++;
    const task = this.queue.then(() => options.html ? this.translateHTML(text) : this.translateText(text));
    this.queue = task.catch(() => {}).finally(() => { this.pending--; });
    return task;
  }
  private async translateHTML(html: string) {
    const document = parseFragment(html, { sourceCodeLocationInfo: true });
    const edits: Array<{ start: number; end: number; text: string }> = [];
    const visit = async (node: any, blocked = false) => {
      const attrs: Array<{name: string; value: string}> = node.attrs ?? [];
      blocked ||= ['script', 'style', 'noscript', 'template'].includes(node.tagName)
        || attrs.some(a => a.name === 'translate' && a.value.toLowerCase() === 'no')
        || attrs.some(a => a.name === 'class' && a.value.split(/\s+/).includes('notranslate'));
      if (blocked) return;
      if (node.nodeName === '#text' && node.value.trim() && node.sourceCodeLocation) {
        const text = await this.translateText(node.value);
        const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
        edits.push({ start: node.sourceCodeLocation.startOffset, end: node.sourceCodeLocation.endOffset, text: escaped });
      }
      for (const child of node.childNodes ?? []) await visit(child, blocked);
    };
    await visit(document);
    for (const edit of edits.sort((a, b) => b.start - a.start)) html = html.slice(0, edit.start) + edit.text + html.slice(edit.end);
    return html;
  }
  private async translateText(text: string): Promise<string> {
    // Protect placeholders and pictographs without asking the model to reproduce sentinel tokens.
    const protectedParts = text.split(/(\{\d+\}|\[\d+\]|\p{RI}\p{RI}|\p{Extended_Pictographic}(?:\uFE0F|\p{Emoji_Modifier})?(?:\u200D\p{Extended_Pictographic}(?:\uFE0F|\p{Emoji_Modifier})?)*)/gu);
    const translated: string[] = [];
    for (let i = 0; i < protectedParts.length; i++) translated.push(i % 2 ? protectedParts[i] : await this.translatePlain(protectedParts[i]));
    return translated.join('');
  }
  private async translatePlain(text: string): Promise<string> {
    if (!text.trim()) return text;
    const match = /^(\s*)([\s\S]*?)(\s*)$/.exec(text)!;
    const [, before, content, after] = match;
    const ids = this.src!.encode(content);
    if (ids.length + 1 > this.tokenLimit || content.length > this.tokenLimit) {
      const sentences = [...new Intl.Segmenter(undefined, { granularity: 'sentence' }).segment(content)].map(s => s.segment);
      if (sentences.length === 1) {
        const chars = Array.from(content);
        if (chars.length < 2) throw new Error('Single character exceeds model input limit');
        let midpoint = Math.floor(chars.length / 2);
        for (let i = midpoint; i > midpoint / 2; i--) if (/\s/.test(chars[i])) { midpoint = i + 1; break; }
        sentences.splice(0, 1, chars.slice(0, midpoint).join(''), chars.slice(midpoint).join(''));
      }
      const out: string[] = []; for (const sentence of sentences) out.push(await this.translatePlain(sentence));
      return before + out.join('') + after;
    }
    ids.push(this.src!.eos);
    const input = new ort.Tensor('int64', BigInt64Array.from(ids.map(BigInt)), [ids.length]);
    const eos = new ort.Tensor('int64', BigInt64Array.of(BigInt(this.dst!.eos)), [1]);
    let output: ort.InferenceSession.ReturnType | undefined;
    try {
      output = await this.session!.run({ X: input, eos_id: eos });
      const tokens = Array.from(output.Y.data as BigInt64Array, Number), end = tokens.indexOf(this.dst!.eos);
      if (end < 0) throw new Error('Translation exceeded generation limit');
      return before + this.dst!.decode(tokens.slice(0, end)) + after;
    } finally { input.dispose(); eos.dispose(); if (output) for (const tensor of Object.values(output)) tensor.dispose(); }
  }
  destroy(): Promise<void> {
    if (this.closed) return this.closed;
    this.closing = true;
    this.closed = this.queue.then(async () => {
      try { await this.session?.release(); } finally { this.session = undefined; this.src?.close(); this.dst?.close(); this.src = this.dst = undefined; }
    });
    return this.closed;
  }
}
