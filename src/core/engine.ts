import * as ort from 'onnxruntime-node';
import { parseFragment } from 'parse5';
import { Tokenizer } from './tokenizer.js';
import type { ConvertedModel } from '../converter/cache.js';
import { createSession, selectBackend, type BackendInfo, type SessionTuning } from './backend.js';
import * as logger from '../logger/index.js';

export interface TranslationOptions { sourceLang?: string; targetLang?: string; cacheSize?: number }
export interface TranslateOptions { qualityScores?: boolean; alignment?: boolean; html?: boolean }
export interface TranslationConfig { 'cpu-threads'?: number; 'max-length-break'?: number }
export class TranslationEngine {
  private session?: ort.InferenceSession;
  private src?: Tokenizer;
  private dst?: Tokenizer;
  private tasks = new Set<Promise<string>>();
  private running = 0;
  private waiters: Array<() => void> = [];
  private pending = 0;
  private closing = false;
  private closed?: Promise<void>;
  private tokenLimit = 512;
  private modelPath = '';
  private fallbackCpuThreads = 1;
  private fallbackCpuTuning?: SessionTuning;
  backend?: BackendInfo;
  identity = '';
  constructor(private options: TranslationOptions = {}) {}
  get busy() { return this.pending > 0; }
  async init(model: ConvertedModel, config: TranslationConfig = {}, sampleText = 'Hello, this is a translation test.') {
    if (this.session) return;
    this.tokenLimit = Math.max(8, config['max-length-break'] ?? 512);
    try {
      this.src = await Tokenizer.load(model.sourceVocab);
      this.dst = await Tokenizer.load(model.targetVocab);
      const sampleIds = this.src.encode(Array.from(sampleText).slice(0, 512).join(''));
      const inputs = [...new Set([Math.min(16, this.tokenLimit - 1), Math.min(32, this.tokenLimit - 1)])].map(length => {
        const ids = sampleIds.length ? Array.from({ length }, (_, i) => sampleIds[i % sampleIds.length]) : [];
        ids.push(this.src!.eos);
        return new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]);
      });
      const eos = new ort.Tensor('int64', BigInt64Array.of(BigInt(this.dst.eos)), [1]);
      try {
        const selected = await selectBackend(model.model, async (session, short) => {
          for (const input of short ? inputs.slice(0, 1) : inputs) await this.infer(session, input, eos);
        }, config['cpu-threads'], model.identity);
        this.session = selected.session;
        this.backend = selected.info;
        this.fallbackCpuThreads = selected.fallbackCpuThreads;
        this.fallbackCpuTuning = selected.fallbackCpuTuning;
        this.modelPath = model.model;
      } finally { for (const input of inputs) input.dispose(); eos.dispose(); }
      this.identity = model.identity;
    } catch (error) { this.src?.close(); this.dst?.close(); throw error; }
  }
  translateAsync(text: string, options: TranslateOptions = {}): Promise<string> {
    if (this.closing || !this.session) return Promise.reject(new Error('Engine not initialized or closing'));
    this.pending++;
    const task = (async () => {
      if (this.running >= (this.backend?.maxConcurrentRuns ?? 1)) await new Promise<void>(resolve => this.waiters.push(resolve));
      else this.running++;
      try { return await (options.html ? this.translateHTML(text) : this.translateText(text)); }
      finally {
        this.pending--;
        const next = this.waiters.shift();
        if (next) next(); else this.running--;
      }
    })();
    this.tasks.add(task);
    task.then(() => this.tasks.delete(task), () => this.tasks.delete(task));
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
    try {
      let tokens: number[];
      try { tokens = await this.infer(this.session!, input, eos); }
      catch (error) {
        if (!this.backend || this.backend.provider === 'cpu') throw error;
        const session = await createSession(this.modelPath, 'cpu', this.fallbackCpuThreads, this.fallbackCpuTuning);
        const previous = this.session!;
        this.session = session;
        this.backend = { ...this.backend, provider: 'cpu', cpuThreads: this.fallbackCpuThreads,
          tuning: this.fallbackCpuTuning ?? this.backend.tuning, maxConcurrentRuns: 1, fallbackReason: String(error) };
        await previous.release().catch(releaseError => logger.debug(`ONNX accelerator release failed: ${releaseError}`));
        logger.warn(`ONNX accelerator failed, falling back to CPU: ${error}`);
        tokens = await this.infer(session, input, eos);
      }
      return before + this.dst!.decode(tokens) + after;
    } finally { input.dispose(); eos.dispose(); }
  }
  private async infer(session: ort.InferenceSession, input: ort.Tensor, eos: ort.Tensor) {
    const output = await session.run({ X: input, eos_id: eos });
    try {
      if (!(output.Y?.data instanceof BigInt64Array)) throw new Error('Invalid translation output');
      const tokens = Array.from(output.Y.data, Number), end = tokens.indexOf(this.dst!.eos);
      if (end < 0) throw new Error('Translation exceeded generation limit');
      return tokens.slice(0, end);
    } finally { for (const tensor of Object.values(output)) tensor.dispose(); }
  }
  destroy(): Promise<void> {
    if (this.closed) return this.closed;
    this.closing = true;
    this.closed = Promise.allSettled([...this.tasks]).then(async () => {
      try { await this.session?.release(); } finally { this.session = undefined; this.src?.close(); this.dst?.close(); this.src = this.dst = undefined; }
    });
    return this.closed;
  }
}
