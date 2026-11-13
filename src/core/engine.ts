import * as ort from 'onnxruntime-node';
import { availableParallelism } from 'node:os';
import { createNativeSession, NativeRunError, type InferenceSession } from './native.js';
import { parseFragment } from 'parse5';
import { Tokenizer } from './tokenizer.js';
import { Shortlist } from './shortlist.js';
import type { ConvertedModel } from '../converter/cache.js';
import { createSession, selectBackend, type BackendInfo, type SessionTuning } from './backend.js';
import * as logger from '../logger/index.js';

export interface TranslationOptions { sourceLang?: string; targetLang?: string; cacheSize?: number }
export interface TranslateOptions { qualityScores?: boolean; alignment?: boolean; html?: boolean }
export interface TranslationConfig { 'cpu-threads'?: number; 'max-length-break'?: number; 'max-concurrent-runs'?: number }
export class TranslationEngine {
  private session?: InferenceSession;
  private src?: Tokenizer;
  private dst?: Tokenizer;
  private shortlist?: Shortlist;
  private tasks = new Set<Promise<string>>();
  private running = 0;
  private waiters: Array<() => void> = [];
  private pending = 0;
  private inferRunning = 0;
  private inferWaiters: Array<() => void> = [];
  private closing = false;
  private closed?: Promise<void>;
  private tokenLimit = 512;
  private modelPath = '';
  private fallbackCpuThreads = 1;
  private fallbackCpuTuning?: SessionTuning;
  private fallback?: Promise<InferenceSession>;
  private initializing?: Promise<void>;
  backend?: BackendInfo;
  identity = '';
  constructor(private options: TranslationOptions = {}) {}
  get busy() { return this.pending > 0; }
  init(model: ConvertedModel, config: TranslationConfig = {}, sampleText = 'Hello, this is a translation test.') {
    if (this.closing) return Promise.reject(new Error('Engine is closing'));
    return this.initializing ??= this.initialize(model, config, sampleText);
  }
  private async initialize(model: ConvertedModel, config: TranslationConfig, sampleText: string) {
    if (this.session) return;
    const limit = config['max-concurrent-runs'];
    if (limit !== undefined && (!Number.isSafeInteger(limit) || limit < 1)) throw new Error('max-concurrent-runs must be a positive integer');
    this.tokenLimit = Math.max(8, config['max-length-break'] ?? 512);
    try {
      this.src = await Tokenizer.load(model.sourceVocab);
      this.dst = await Tokenizer.load(model.targetVocab);
      let shortlist: Promise<Shortlist> | undefined;
      const sampleIds = this.src.encode(Array.from(sampleText).slice(0, 512).join(''));
      const inputs = [...new Set([Math.min(16, this.tokenLimit - 1), Math.min(32, this.tokenLimit - 1)])].map(length => {
        const ids = sampleIds.slice(0, length);
        ids.push(this.src!.eos);
        return new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]);
      });
      const eos = new ort.Tensor('int64', BigInt64Array.of(BigInt(this.dst.eos)), [1]);
      try {
        const probe = async (session: InferenceSession, short?: boolean) => {
          if (session.inputNames?.includes('candidate_ids')) {
            shortlist ??= model.lex ? Shortlist.load(model.lex, model.sourceVocab, model.targetVocab)
              : Promise.reject(new Error('Shortlist ONNX graph requires ConvertedModel.lex'));
            this.shortlist = await shortlist;
          }
          // A bounded timing probe need not finish a translation; user requests still require EOS.
          for (const input of short ? inputs.slice(0, 1) : inputs) await this.infer(session, input, eos, false);
        };
        const selected = await selectBackend(model.model, probe, config['cpu-threads'], model.identity);
        let activeSession: InferenceSession = selected.session;
        let info = selected.info;
        const inputNames = selected.session.inputNames ?? ['X', 'eos_id'];
        const isShortlist = inputNames.includes('candidate_ids');
        if (selected.info.provider === 'cpu'
          && (!isShortlist || config['cpu-threads'] === undefined || config['cpu-threads'] === 1)) {
          const nativeThreads = isShortlist ? 1 : selected.info.cpuThreads;
          const native = await createNativeSession(model.model, inputNames, nativeThreads);
          if (native) {
            try {
              await probe(native);
              const poolSize = Number.parseInt(process.env.UV_THREADPOOL_SIZE ?? '4', 10) || 4;
              activeSession = native;
              info = { ...selected.info, runtime: 'native-async', cpuThreads: nativeThreads,
                tuning: { ...selected.info.tuning, executionMode: 'sequential', interOpNumThreads: 1,
                  graphOptimizationLevel: 'all', spinning: false, dynamicBlockBase: 0 },
                maxConcurrentRuns: Math.min(config['max-concurrent-runs'] ?? Infinity, Math.max(1, poolSize), availableParallelism()) };
              await selected.session.release().catch(error => logger.debug(`ONNX session release failed: ${error}`));
            } catch (error) {
              await native.release().catch(() => {});
              logger.warn(`Native ONNX validation failed; using standard runtime: ${error}`);
            }
          }
        }
        this.session = activeSession;
        this.backend = info;
        this.fallbackCpuThreads = selected.fallbackCpuThreads;
        this.fallbackCpuTuning = selected.fallbackCpuTuning;
        this.modelPath = model.model;
      } finally { for (const input of inputs) input.dispose(); eos.dispose(); }
      this.identity = model.identity;
    } catch (error) { this.src?.close(); this.dst?.close(); this.src = this.dst = undefined; this.shortlist = undefined; throw error; }
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
        this.running--;
        while (this.waiters.length && this.running < (this.backend?.maxConcurrentRuns ?? 1)) {
          this.running++;
          this.waiters.shift()!();
        }
      }
    })();
    this.tasks.add(task);
    task.then(() => this.tasks.delete(task), () => this.tasks.delete(task));
    return task;
  }
  private async translateHTML(html: string) {
    const document = parseFragment(html, { sourceCodeLocationInfo: true });
    const nodes: any[] = [];
    const visit = (node: any, blocked = false) => {
      const attrs: Array<{name: string; value: string}> = node.attrs ?? [];
      blocked ||= ['script', 'style', 'noscript', 'template'].includes(node.tagName)
        || attrs.some(a => a.name === 'translate' && a.value.toLowerCase() === 'no')
        || attrs.some(a => a.name === 'class' && a.value.split(/\s+/).includes('notranslate'));
      if (blocked) return;
      if (node.nodeName === '#text' && node.value.trim() && node.sourceCodeLocation) nodes.push(node);
      for (const child of node.childNodes ?? []) visit(child, blocked);
    };
    visit(document);
    // Let requests accepted in the same turn establish the shared load first.
    await Promise.resolve();
    const edits: Array<{ start: number; end: number; text: string }> = [];
    let next = 0, failed = false, failure: unknown;
    await Promise.all(Array.from({ length: Math.min(nodes.length, this.backend?.maxConcurrentRuns ?? 1) }, async (_, worker) => {
      while (!failed && next < nodes.length) {
        // Other requests already provide parallel work; avoid per-document fan-out.
        if (worker > 0 && this.running > 1) return;
        const node = nodes[next++];
        try {
          const text = await this.translateText(node.value);
          const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
          edits.push({ start: node.sourceCodeLocation.startOffset, end: node.sourceCodeLocation.endOffset, text: escaped });
        } catch (error) { if (!failed) { failed = true; failure = error; } }
      }
    }));
    if (failed) throw failure;
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
        if (!this.backend || (this.backend.provider === 'cpu' && !(error instanceof NativeRunError))) throw error;
        this.fallback ??= (async () => {
          const session = await createSession(this.modelPath, 'cpu', this.fallbackCpuThreads, this.fallbackCpuTuning);
          const previous = this.session!;
          this.session = session;
          this.backend = { ...this.backend!, runtime: undefined, provider: 'cpu', cpuThreads: this.fallbackCpuThreads,
            tuning: this.fallbackCpuTuning ?? this.backend!.tuning, maxConcurrentRuns: 1, fallbackReason: String(error) };
          await previous.release().catch(releaseError => logger.debug(`ONNX session release failed: ${releaseError}`));
          logger.warn(`ONNX execution failed, falling back to standard CPU runtime: ${error}`);
          return session;
        })().catch(error => { this.fallback = undefined; throw error; });
        const session = await this.fallback;
        tokens = await this.infer(session, input, eos);
      }
      return before + this.dst!.decode(tokens) + after;
    } finally { input.dispose(); eos.dispose(); }
  }
  private async runInference(session: InferenceSession, feeds: Record<string, ort.Tensor>) {
    // Initialization probes measure their own concurrency before this.session is published.
    const gated = !!this.session;
    if (gated) {
      if (this.inferRunning >= (this.backend?.maxConcurrentRuns ?? 1)) await new Promise<void>(resolve => this.inferWaiters.push(resolve));
      else this.inferRunning++;
    }
    try { return await session.run(feeds); }
    finally {
      if (gated) {
        this.inferRunning--;
        while (this.inferWaiters.length && this.inferRunning < (this.backend?.maxConcurrentRuns ?? 1)) {
          this.inferRunning++;
          this.inferWaiters.shift()!();
        }
      }
    }
  }
  private async infer(session: InferenceSession, input: ort.Tensor, eos: ort.Tensor, requireEos = true) {
    let candidates: ort.Tensor | undefined;
    try {
      const feeds: Record<string, ort.Tensor> = { X: input, eos_id: eos };
      if (session.inputNames?.includes('candidate_ids')) {
        if (!this.shortlist) throw new Error('Shortlist ONNX graph requires a validated lexicon (ConvertedModel.lex)');
        const ids = this.shortlist.candidates(Array.from(input.data as BigInt64Array, Number));
        feeds.candidate_ids = candidates = new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]);
      }
      const output = await this.runInference(session, feeds);
      try {
        if (!(output.Y?.data instanceof BigInt64Array)) throw new Error('Invalid translation output');
        const tokens = Array.from(output.Y.data, Number), end = tokens.indexOf(this.dst!.eos);
        if (end < 0 && requireEos) throw new Error('Translation exceeded generation limit');
        return end < 0 ? tokens : tokens.slice(0, end);
      } finally { for (const tensor of Object.values(output)) tensor.dispose(); }
    } finally { candidates?.dispose(); }
  }
  destroy(): Promise<void> {
    if (this.closed) return this.closed;
    this.closing = true;
    this.closed = (async () => {
      await this.initializing?.catch(() => {});
      await Promise.allSettled([...this.tasks]);
      try { await this.session?.release(); } finally { this.session = undefined; this.src?.close(); this.dst?.close(); this.src = this.dst = undefined; this.shortlist = undefined; }
    })();
    return this.closed;
  }
}
