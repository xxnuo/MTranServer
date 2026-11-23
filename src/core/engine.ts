import * as ort from 'onnxruntime-node';
import { availableParallelism, endianness } from 'node:os';
import { createNativeSession, hasNativeAddon, hasNativeSsru, NativeRunError, type InferenceSession } from './native.js';
import { parseFragment } from 'parse5';
import { createMetalWorker, getMetalRuntime, type MetalWorker, type MetalInput } from './metal.js';
import { Tokenizer } from './tokenizer.js';
import { Shortlist } from './shortlist.js';
import type { ConvertedModel } from '../converter/cache.js';
import { ensureNativeSsru } from '../converter/ssru-cache.js';
import { createSession, selectBackend, type BackendInfo, type SessionTuning } from './backend.js';
import * as logger from '../logger/index.js';

export interface TranslationOptions { sourceLang?: string; targetLang?: string; cacheSize?: number }
export interface TranslateOptions { qualityScores?: boolean; alignment?: boolean; html?: boolean }
export interface TranslationConfig { 'cpu-threads'?: number; 'max-length-break'?: number; 'max-concurrent-runs'?: number }
const sentenceSegmenter = new Intl.Segmenter(undefined, { granularity: 'sentence' });
const littleEndian = endianness() === 'LE';
const protectedSplitRe = /(\{\d+\}|\[\d+\]|\p{RI}\p{RI}|\p{Extended_Pictographic}(?:\uFE0F|\p{Emoji_Modifier})?(?:\u200D\p{Extended_Pictographic}(?:\uFE0F|\p{Emoji_Modifier})?)*)/gu;
const protectedQuickRe = /\{\d+\}|\[\d+\]|\p{RI}|\p{Extended_Pictographic}/u;
function splitSentences(content: string): string[] {
  const sentences = [...sentenceSegmenter.segment(content)].map(s => s.segment);
  if (sentences.length === 1) {
    const chars = Array.from(content);
    if (chars.length < 2) throw new Error('Single character exceeds model input limit');
    let midpoint = Math.floor(chars.length / 2);
    for (let i = midpoint; i > midpoint / 2; i--) if (/\s/.test(chars[i])) { midpoint = i + 1; break; }
    sentences.splice(0, 1, chars.slice(0, midpoint).join(''), chars.slice(midpoint).join(''));
  }
  return sentences;
}

interface BatchLeaf {
  inputIds: BigInt64Array;
  before: string;
  after: string;
  out?: string;
}

export class TranslationEngine {
  private session?: InferenceSession;
  private nativeSessions = new WeakSet<InferenceSession>();
  private converted?: ConvertedModel;
  private metal?: Promise<MetalWorker | undefined>;
  private metalBusy = false;
  private metalDisabled = false;
  private metalSelections = new Map<string, boolean>();
  private src?: Tokenizer;
  private dst?: Tokenizer;
  private shortlist?: Shortlist;
  private tasks = new Set<Promise<unknown>>();
  private running = 0;
  private waiters: Array<() => void> = [];
  private pending = 0;
  private requestEpoch = 0;
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
      this.dst = model.targetVocab === model.sourceVocab
        ? (typeof (this.src as { retain?: () => Tokenizer }).retain === 'function' ? this.src.retain() : this.src)
        : await Tokenizer.load(model.targetVocab);
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
        const cpuOnly = process.env.MT_CPU_ONLY === 'true' || !!getMetalRuntime(model);
        const canSsru = (!!model.lex || !!model.sourceModel) && process.env.MT_NATIVE_SSRU !== '0';
        const poolSize = Number.parseInt(process.env.UV_THREADPOOL_SIZE ?? '4', 10) || 4;
        const ssruMaxConcurrentRuns = Math.min(
          config['max-concurrent-runs'] ?? Infinity,
          Math.max(1, poolSize),
          Math.min(availableParallelism(), Math.max(8, Math.floor(availableParallelism() * 0.75)))
        );
        const ssruTuning: SessionTuning = {
          executionMode: 'sequential', interOpNumThreads: 1, graphOptimizationLevel: 'all',
          enableCpuMemArena: true, enableMemPattern: true, spinning: false, dynamicBlockBase: 0,
        };
        let activeSession: InferenceSession | undefined;
        let info: BackendInfo | undefined;
        let fallbackCpuThreads = 1;
        let fallbackCpuTuning: SessionTuning | undefined = { ...ssruTuning, spinning: true };

        // Skip building & tearing down a 140MB ORT model.onnx session when CPU-only SSRU is active.
        if (cpuOnly && config['cpu-threads'] === undefined && canSsru && hasNativeSsru()) {
          const isShortlist = !!model.lex;
          const directInputNames = isShortlist ? ['X', 'eos_id', 'candidate_ids'] : ['X', 'eos_id'];
          let native: InferenceSession | undefined;
          try {
            const nativeModel = await ensureNativeSsru(model, isShortlist);
            native = await createNativeSession(nativeModel.model, directInputNames, 1, true);
            if (!native) throw new Error('Native SSRU session unavailable');
            this.nativeSessions.add(native);
            const started = performance.now();
            await probe(native);
            const probeMs = performance.now() - started;
            if (sampleIds.length + 1 <= this.tokenLimit) {
              const ids = [...sampleIds, this.src.eos];
              const fullInput = new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]);
              try {
                try { await this.infer(native, fullInput, eos, true); }
                catch (error) {
                  if (!(error instanceof Error) || error.message !== 'Translation exceeded generation limit') throw error;
                }
              } finally { fullInput.dispose(); }
            }
            activeSession = native;
            info = {
              provider: 'cpu', runtime: 'native-ssru', cpuThreads: 1, tuning: ssruTuning,
              maxConcurrentRuns: ssruMaxConcurrentRuns,
              measurements: [{ provider: 'cpu', cpuThreads: 1, tuning: ssruTuning, samples: 1, medianMs: probeMs }],
            };
            logger.info(`ONNX selected cpu (native-ssru direct), CPU threads=1, concurrency=${info.maxConcurrentRuns}, probe=${probeMs.toFixed(2)}ms`);
          } catch (error) {
            await native?.release().catch(() => {});
            logger.warn(`Direct native SSRU startup failed; falling back to standard selection: ${error}`);
          }
        }

        if (!activeSession || !info) {
          const skipCpuSweep = config['cpu-threads'] === undefined && ((!!model.lex && hasNativeAddon()) || (canSsru && hasNativeSsru()));
          const selected = await selectBackend(model.model, probe, config['cpu-threads'], model.identity, !!getMetalRuntime(model), skipCpuSweep);
          activeSession = selected.session;
          info = selected.info;
          fallbackCpuThreads = selected.fallbackCpuThreads;
          fallbackCpuTuning = selected.fallbackCpuTuning;
          const inputNames = selected.session.inputNames ?? ['X', 'eos_id'];
          const isShortlist = inputNames.includes('candidate_ids');
          const preferSsru = selected.info.provider === 'cpu' && (isShortlist || !!model.sourceModel) && process.env.MT_NATIVE_SSRU !== '0';
          const ssru = preferSsru && hasNativeSsru();
          if (preferSsru && !ssru) info = { ...info, fallbackReason: 'Native SSRU addon unavailable' };
          if (selected.info.provider === 'cpu'
            && (ssru || !isShortlist || config['cpu-threads'] === undefined || config['cpu-threads'] === 1)) {
            const nativeThreads = ssru ? config['cpu-threads'] ?? 1 : isShortlist ? 1 : selected.info.cpuThreads;
            let native: InferenceSession | undefined;
            try {
              const nativeModel = ssru ? await ensureNativeSsru(model, isShortlist) : model;
              native = ssru ? await createNativeSession(nativeModel.model, inputNames, nativeThreads, true)
                : await createNativeSession(nativeModel.model, inputNames, nativeThreads);
              if (ssru && !native) throw new Error('Native SSRU session unavailable');
              if (native) {
                this.nativeSessions.add(native);
                await probe(native);
                if (ssru) {
                  const ids = sampleIds.length + 1 <= this.tokenLimit ? [...sampleIds] : this.src.encode('Hello.');
                  ids.push(this.src.eos);
                  const input = new ort.Tensor('int64', BigInt64Array.from(ids, BigInt), [ids.length]);
                  try {
                    let requireEos = true;
                    try { await this.infer(selected.session, input, eos); }
                    catch (error) {
                      if (!(error instanceof Error) || error.message !== 'Translation exceeded generation limit') throw error;
                      requireEos = false;
                    }
                    // Full-sentence completion is checked against Fast; numeric differences need independent quality review.
                    await this.infer(native, input, eos, requireEos);
                  } finally { input.dispose(); }
                }
                activeSession = native;
                info = { ...info, runtime: ssru ? 'native-ssru' : 'native-async', cpuThreads: nativeThreads,
                  tuning: { ...selected.info.tuning, executionMode: 'sequential', interOpNumThreads: 1,
                    graphOptimizationLevel: 'all', spinning: false, dynamicBlockBase: 0 },
                  maxConcurrentRuns: ssru
                    ? ssruMaxConcurrentRuns
                    : Math.min(config['max-concurrent-runs'] ?? Infinity, Math.max(1, poolSize), availableParallelism()) };
                await selected.session.release().catch(error => logger.debug(`ONNX session release failed: ${error}`));
              }
            } catch (error) {
              await native?.release().catch(() => {});
              if (ssru) info = { ...info, fallbackReason: `Native SSRU validation failed: ${error}` };
              logger.warn(`Native ONNX${ssru ? ' SSRU' : ''} validation failed; using standard runtime: ${error}`);
            }
          }
        }
        this.session = activeSession;
        this.backend = info;
        this.fallbackCpuThreads = fallbackCpuThreads;
        this.fallbackCpuTuning = fallbackCpuTuning;
        this.modelPath = model.model;
      } finally { for (const input of inputs) input.dispose(); eos.dispose(); }
      this.identity = model.identity;
      this.converted = model;
    } catch (error) { this.closeTokenizers(); this.shortlist = undefined; throw error; }
  }
  private closeTokenizers() {
    const { src, dst } = this;
    this.src = this.dst = undefined;
    src?.close();
    if (dst && (dst !== src || typeof (dst as { retain?: () => Tokenizer }).retain === 'function')) dst.close();
  }
  translateAsync(text: string, options: TranslateOptions = {}): Promise<string> {
    if (this.closing || !this.session) return Promise.reject(new Error('Engine not initialized or closing'));
    this.pending++;
    this.requestEpoch++;
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
  translateBatchAsync(texts: string[], options: TranslateOptions = {}): Promise<string[]> {
    if (this.closing || !this.session) return Promise.reject(new Error('Engine not initialized or closing'));
    if (texts.length === 0) return Promise.resolve([]);
    if (texts.length === 1) return this.translateAsync(texts[0], options).then(res => [res]);
    this.pending++;
    this.requestEpoch++;
    const task = (async () => {
      if (this.running >= (this.backend?.maxConcurrentRuns ?? 1)) await new Promise<void>(resolve => this.waiters.push(resolve));
      else this.running++;
      try {
        if (options.html) {
          const out = new Array<string>(texts.length);
          let next = 0, failed = false, failure: unknown;
          const workers = Math.min(texts.length, this.backend?.maxConcurrentRuns ?? 1);
          await Promise.all(Array.from({ length: workers }, async () => {
            while (!failed && next < texts.length) {
              const index = next++;
              try { out[index] = await this.translateHTML(texts[index]); }
              catch (error) { if (!failed) { failed = true; failure = error; } }
            }
          }));
          if (failed) throw failure;
          return out;
        }
        if (typeof this.session?.runBatch === 'function') return await this.translateBatchTexts(texts);
        const out = new Array<string>(texts.length);
        let next = 0, failed = false, failure: unknown;
        const workers = Math.min(texts.length, this.backend?.maxConcurrentRuns ?? 1);
        await Promise.all(Array.from({ length: workers }, async () => {
          while (!failed && next < texts.length) {
            const index = next++;
            try { out[index] = await this.translateText(texts[index]); }
            catch (error) { if (!failed) { failed = true; failure = error; } }
          }
        }));
        if (failed) throw failure;
        return out;
      }
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
    if (nodes.length > 1 && typeof this.session?.runBatch === 'function') {
      const translated = await this.translateBatchTexts(nodes.map(node => node.value as string));
      const edits = nodes.map((node, i) => ({
        start: node.sourceCodeLocation.startOffset as number,
        end: node.sourceCodeLocation.endOffset as number,
        text: translated[i].replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'),
      }));
      for (const edit of edits.sort((a, b) => b.start - a.start)) html = html.slice(0, edit.start) + edit.text + html.slice(edit.end);
      return html;
    }
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
    if (!protectedQuickRe.test(text)) return this.translatePlain(text);
    // Protect placeholders and pictographs without asking the model to reproduce sentinel tokens.
    const protectedParts = text.split(protectedSplitRe);
    const translated: string[] = [];
    for (let i = 0; i < protectedParts.length; i++) translated.push(i % 2 ? protectedParts[i] : await this.translatePlain(protectedParts[i]));
    return translated.join('');
  }
  private toInputIds(ids: number[]): BigInt64Array {
    const inputIds = new BigInt64Array(ids.length + 1);
    if (littleEndian) {
      const view32 = new Int32Array(inputIds.buffer);
      for (let i = 0; i < ids.length; i++) view32[i * 2] = ids[i];
      view32[ids.length * 2] = this.src!.eos;
    } else {
      for (let i = 0; i < ids.length; i++) inputIds[i] = BigInt(ids[i]);
      inputIds[ids.length] = BigInt(this.src!.eos);
    }
    return inputIds;
  }
  private async translateBatchTexts(texts: string[]): Promise<string[]> {
    const leaves: BatchLeaf[] = [];
    const collectPlain = (text: string): (() => string) => {
      if (!text) return () => text;
      let before = '', content = text, after = '';
      const firstCode = text.charCodeAt(0), lastCode = text.charCodeAt(text.length - 1);
      if (firstCode <= 32 || lastCode <= 32 || firstCode >= 160 || lastCode >= 160) {
        const trimmed = text.trim();
        if (!trimmed) return () => text;
        if (trimmed.length !== text.length) {
          const start = text.indexOf(trimmed);
          before = text.slice(0, start);
          content = trimmed;
          after = text.slice(start + trimmed.length);
        }
      }
      const ids = content.length > this.tokenLimit ? undefined : this.src!.encode(content);
      if (!ids || ids.length + 1 > this.tokenLimit) {
        const sentences = splitSentences(content);
        const getters = sentences.map(collectPlain);
        return () => before + getters.map(g => g()).join('') + after;
      }
      const leafIndex = leaves.length;
      leaves.push({ inputIds: this.toInputIds(ids), before, after });
      return () => leaves[leafIndex].out!;
    };
    const collectText = (text: string): (() => string) => {
      if (!protectedQuickRe.test(text)) return collectPlain(text);
      const protectedParts = text.split(protectedSplitRe);
      const partGetters = protectedParts.map((part, i) => (i % 2 ? undefined : collectPlain(part)));
      return () => protectedParts.map((part, i) => (i % 2 ? part : partGetters[i]!())).join('');
    };
    const getters = texts.map(collectText);
    if (leaves.length === 0) return getters.map(g => g());
    if (leaves.length === 1) {
      const tokens = await this.runSingleWithFallback(leaves[0].inputIds);
      leaves[0].out = leaves[0].before + this.dst!.decode(tokens) + leaves[0].after;
      return getters.map(g => g());
    }
    const order = Array.from({ length: leaves.length }, (_, i) => i);
    order.sort((a, b) => leaves[a].inputIds.length - leaves[b].inputIds.length);
    const batchInputs: BigInt64Array[] = new Array(order.length);
    for (let i = 0; i < order.length; i++) batchInputs[i] = leaves[order[i]].inputIds;
    const batchTokens = await this.runBatchWithFallback(batchInputs);
    for (let i = 0; i < order.length; i++) {
      const leaf = leaves[order[i]];
      leaf.out = leaf.before + this.dst!.decode(batchTokens[i]) + leaf.after;
    }
    return getters.map(g => g());
  }
  private async translatePlain(text: string, parallel = true): Promise<string> {
    if (!text) return text;
    let before = '', content = text, after = '';
    const firstCode = text.charCodeAt(0), lastCode = text.charCodeAt(text.length - 1);
    if (firstCode <= 32 || lastCode <= 32 || firstCode >= 160 || lastCode >= 160) {
      const trimmed = text.trim();
      if (!trimmed) return text;
      if (trimmed.length !== text.length) {
        const start = text.indexOf(trimmed);
        before = text.slice(0, start);
        content = trimmed;
        after = text.slice(start + trimmed.length);
      }
    }
    // Skip encoding the entire document when character count already exceeds tokenLimit.
    const ids = content.length > this.tokenLimit ? undefined : this.src!.encode(content);
    if (!ids || ids.length + 1 > this.tokenLimit) {
      if (parallel && !this.metalBusy && !this.metalDisabled && this.converted && (this.running === 1 || this.metalSelections.size > 0)) {
        const accelerated = await this.translateMetal(content);
        if (accelerated !== undefined) return before + accelerated + after;
      }
      const sentences = splitSentences(content);
      await Promise.resolve();
      if (parallel && sentences.length > 1 && typeof this.session?.runBatch === 'function') {
        const translated = await this.translateBatchTexts(sentences);
        return before + translated.join('') + after;
      }
      const out: string[] = new Array(sentences.length);
      let next = 0, failed = false, failure: unknown;
      // Nested splits stay serial; all leaves still share the engine inference budget.
      const workers = parallel ? Math.min(sentences.length, this.backend?.maxConcurrentRuns ?? 1) : 1;
      await Promise.all(Array.from({ length: workers }, async (_, worker) => {
        while (!failed && next < sentences.length) {
          if (worker > 0 && this.running > 1) return;
          const index = next++;
          try { out[index] = await this.translatePlain(sentences[index], false); }
          catch (error) { if (!failed) { failed = true; failure = error; } }
        }
      }));
      if (failed) throw failure;
      return before + out.join('') + after;
    }
    const tokens = await this.runSingleWithFallback(this.toInputIds(ids));
    return before + this.dst!.decode(tokens) + after;
  }
  private async fallbackToStandardCpu(error: unknown): Promise<InferenceSession> {
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
    })().catch(err => { this.fallback = undefined; throw err; });
    return this.fallback;
  }
  private async runSingleWithFallback(inputIds: BigInt64Array): Promise<Int32Array> {
    try { return await this.inferIds(this.session!, inputIds); }
    catch (error) {
      const session = await this.fallbackToStandardCpu(error);
      return await this.inferIds(session, inputIds);
    }
  }
  private async runBatchWithFallback(batchInputs: BigInt64Array[]): Promise<Int32Array[]> {
    try { return await this.inferIdsBatch(this.session!, batchInputs); }
    catch (error) {
      const session = await this.fallbackToStandardCpu(error);
      return await Promise.all(batchInputs.map(inp => this.inferIds(session, inp)));
    }
  }
  private async translateMetal(content: string): Promise<string | undefined> {
    // CPU keeps short inputs and competing requests; only documents can amortize GPU dispatch.
    if (this.backend?.provider !== 'cpu' || !this.converted || !getMetalRuntime(this.converted)) return;
    const leaves: Array<MetalInput & { text: string; before: string; after: string }> = [];
    const parts: Array<string | number> = [];
    const collect = (text: string) => {
      if (!text.trim()) { parts.push(text); return; }
      const [, before, value, after] = /^(\s*)([\s\S]*?)(\s*)$/.exec(text)!;
      const ids = value.length > this.tokenLimit ? undefined : this.src!.encode(value);
      if (!ids || ids.length + 1 > this.tokenLimit) {
        parts.push(before); splitSentences(value).forEach(collect); parts.push(after);
      } else {
        ids.push(this.src!.eos);
        parts.push(leaves.length);
        leaves.push({ text, before, after, ids, candidates: this.shortlist?.candidates(ids) ?? [] });
      }
    };
    collect(content);
    if (leaves.length < 16) return;
    const maxTokens = 2 ** Math.ceil(Math.log2(leaves.reduce((max, row) => Math.max(max, row.ids.length), 0)));
    const batchSize = Math.min(32, leaves.length);
    const key = `${batchSize}:${maxTokens}`;
    if (this.metalSelections.get(key) === false) return;
    if (!this.metalSelections.has(key) && this.running !== 1) return;
    this.metalBusy = true;
    let worker: MetalWorker | undefined;
    try {
      this.metal ??= createMetalWorker(this.converted!, this.src!.eos, this.dst!.eos);
      worker = await this.metal;
      if (!worker) { this.metalDisabled = true; return; }
      if (!this.metalSelections.has(key)) {
        if (this.running !== 1) return;
        const epoch = this.requestEpoch;
        const sample = [...leaves].sort((a, b) => b.ids.length - a.ids.length).slice(0, batchSize);
        const fast = !!this.shortlist;
        await worker.translateMany(sample, fast);
        const cpu = async () => {
          const start = performance.now();
          let next = 0;
          await Promise.all(Array.from({ length: Math.min(sample.length, this.backend?.maxConcurrentRuns ?? 1) }, async () => {
            while (next < sample.length) await this.translatePlain(sample[next++].text, false);
          }));
          return performance.now() - start;
        };
        const gpu = async () => { const start = performance.now(); await worker!.translateMany(sample, fast); return performance.now() - start; };
        const cpuFirst = await cpu(), gpuFirst = await gpu();
        const gpuSecond = await gpu(), cpuSecond = await cpu();
        const cpuMs = (cpuFirst + cpuSecond) / 2, gpuMs = (gpuFirst + gpuSecond) / 2;
        // Do not cache a decision contaminated by another user request.
        if (this.running !== 1 || epoch !== this.requestEpoch) return;
        const selected = gpuMs < cpuMs * .9;
        this.metalSelections.set(key, selected);
        if (this.backend) this.backend = { ...this.backend, documentAcceleration: {
          provider: process.platform === 'linux' ? 'cuda' : 'metal', runtime: 'mlx', batchSize, maxTokens, cpuMs, gpuMs, selected,
        } };
        if (!selected) { await worker.close(); this.metal = undefined; return; }
      }
      const results = await worker.translateMany(leaves, !!this.shortlist);
      return parts.map(part => typeof part === 'string' ? part : leaves[part].before + this.dst!.decode(results[part]) + leaves[part].after).join('');
    } catch (error) {
      this.metalDisabled = true;
      await worker?.close().catch(() => {});
      logger.warn(`GPU document acceleration unavailable; using CPU: ${error}`);
      if (this.backend) this.backend = { ...this.backend, documentAcceleration: {
        provider: process.platform === 'linux' ? 'cuda' : 'metal', runtime: 'mlx', selected: false, error: String(error),
      } };
      return;
    } finally { this.metalBusy = false; }
  }

  private async runInference(session: InferenceSession, feeds: Record<string, ort.Tensor>) {
    // Initialization probes measure their own concurrency before this.session is published.
    const gated = !!this.session;
    if (gated) {
      if (this.inferRunning >= (this.backend?.maxConcurrentRuns ?? 1)) await new Promise<void>(resolve => this.inferWaiters.push(resolve));
      else this.inferRunning++;
    }
    // Queued requests may still hold the session that a concurrent failure retired.
    const actual = gated ? this.session! : session;
    const wasNative = this.nativeSessions.has(actual);
    try { return { outputs: await actual.run(feeds), wasNative }; }
    catch (error) {
      if (wasNative && !(error instanceof NativeRunError)) throw new NativeRunError(String(error));
      throw error;
    }
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
  private extractOutputTokens(raw: unknown, inputLength: number, requireEos: boolean, wasNative: boolean): Int32Array {
    const invalid = () => new (wasNative ? NativeRunError : Error)('Invalid translation output');
    if (!(raw instanceof BigInt64Array) || raw.length < 1 || raw.length > 1 + 3 * inputLength) throw invalid();
    const dstSize = this.dst!.size, eosVal = this.dst!.eos;
    let end = -1;
    if (littleEndian) {
      const v32 = new Int32Array(raw.buffer, raw.byteOffset, raw.length * 2);
      for (let i = 0; i < raw.length; i++) {
        const lo = v32[i * 2], hi = v32[i * 2 + 1];
        if (hi !== 0 || lo < 0 || lo >= dstSize) throw invalid();
        if (end < 0 && lo === eosVal) end = i;
      }
      if (end < 0 && requireEos) throw new Error('Translation exceeded generation limit');
      const length = end < 0 ? raw.length : end;
      const tokens = new Int32Array(length);
      for (let i = 0; i < length; i++) tokens[i] = v32[i * 2];
      return tokens;
    }
    const size = BigInt(dstSize), eosId = BigInt(eosVal);
    for (let i = 0; i < raw.length; i++) {
      const token = raw[i];
      if (token < 0n || token >= size) throw invalid();
      if (end < 0 && token === eosId) end = i;
    }
    if (end < 0 && requireEos) throw new Error('Translation exceeded generation limit');
    const length = end < 0 ? raw.length : end;
    const tokens = new Int32Array(length);
    for (let i = 0; i < length; i++) tokens[i] = Number(raw[i]);
    return tokens;
  }
  private getRawCandidates(session: InferenceSession, inputIds: BigInt64Array): BigInt64Array | null {
    if (!session.inputNames?.includes('candidate_ids')) return null;
    if (!this.shortlist) throw new Error('Shortlist ONNX graph requires a validated lexicon (ConvertedModel.lex)');
    return typeof (this.shortlist as any).candidatesBigInt === 'function'
      ? this.shortlist.candidatesBigInt(inputIds)
      : BigInt64Array.from(this.shortlist.candidates(Array.from(inputIds, Number)), BigInt);
  }
  private async acquireInferGate() {
    if (this.inferRunning >= (this.backend?.maxConcurrentRuns ?? 1)) await new Promise<void>(resolve => this.inferWaiters.push(resolve));
    else this.inferRunning++;
  }
  private releaseInferGate() {
    this.inferRunning--;
    while (this.inferWaiters.length && this.inferRunning < (this.backend?.maxConcurrentRuns ?? 1)) {
      this.inferRunning++;
      this.inferWaiters.shift()!();
    }
  }
  private async infer(session: InferenceSession, input: ort.Tensor, eos: ort.Tensor, requireEos = true) {
    let candidates: ort.Tensor | undefined;
    try {
      const feeds: Record<string, ort.Tensor> = { X: input, eos_id: eos };
      const rawCandidates = this.getRawCandidates(session, input.data as BigInt64Array);
      if (rawCandidates) feeds.candidate_ids = candidates = new ort.Tensor('int64', rawCandidates, [rawCandidates.length]);
      const { outputs, wasNative } = await this.runInference(session, feeds);
      try {
        const y = outputs.Y;
        if (y?.type !== 'int64' || !(y.data instanceof BigInt64Array) || !Array.isArray(y.dims) || y.dims.length !== 1 || y.dims[0] !== y.data.length)
          throw new (wasNative ? NativeRunError : Error)('Invalid translation output');
        return this.extractOutputTokens(y.data, input.data.length, requireEos, wasNative);
      } finally { for (const tensor of Object.values(outputs)) tensor.dispose(); }
    } finally { candidates?.dispose(); }
  }
  private async inferIds(session: InferenceSession, inputIds: BigInt64Array, requireEos = true): Promise<Int32Array> {
    if (typeof session.runRaw !== 'function') {
      const input = new ort.Tensor('int64', inputIds, [inputIds.length]);
      const eos = new ort.Tensor('int64', BigInt64Array.of(BigInt(this.dst!.eos)), [1]);
      try { return await this.infer(session, input, eos, requireEos); }
      finally { input.dispose(); eos.dispose(); }
    }
    const rawCandidates = this.getRawCandidates(session, inputIds);
    const gated = !!this.session;
    if (gated) await this.acquireInferGate();
    const actual = gated ? this.session! : session;
    if (typeof actual.runRaw !== 'function') {
      if (gated) this.releaseInferGate();
      const input = new ort.Tensor('int64', inputIds, [inputIds.length]);
      const eos = new ort.Tensor('int64', BigInt64Array.of(BigInt(this.dst!.eos)), [1]);
      try { return await this.infer(actual, input, eos, requireEos); }
      finally { input.dispose(); eos.dispose(); }
    }
    const wasNative = this.nativeSessions.has(actual);
    let raw: BigInt64Array;
    try { raw = await actual.runRaw(inputIds, rawCandidates, BigInt(this.dst!.eos)); }
    catch (error) {
      if (wasNative && !(error instanceof NativeRunError)) throw new NativeRunError(String(error));
      throw error;
    } finally {
      if (gated) this.releaseInferGate();
    }
    return this.extractOutputTokens(raw, inputIds.length, requireEos, wasNative);
  }
  private async inferIdsBatch(session: InferenceSession, batchInputs: BigInt64Array[], requireEos = true): Promise<Int32Array[]> {
    if (batchInputs.length === 0) return [];
    if (typeof session.runBatch !== 'function') return Promise.all(batchInputs.map(inp => this.inferIds(session, inp, requireEos)));
    const gated = !!this.session;
    if (gated) await this.acquireInferGate();
    const actual = gated ? this.session! : session;
    if (typeof actual.runBatch !== 'function') {
      if (gated) this.releaseInferGate();
      return Promise.all(batchInputs.map(inp => this.inferIds(actual, inp, requireEos)));
    }
    const wasNative = this.nativeSessions.has(actual);
    let rawList: BigInt64Array[];
    try {
      const rawCandidates = batchInputs.map(inp => this.getRawCandidates(actual, inp));
      rawList = await actual.runBatch(batchInputs, rawCandidates, BigInt(this.dst!.eos));
    } catch (error) {
      if (wasNative && !(error instanceof NativeRunError)) throw new NativeRunError(String(error));
      throw error;
    } finally {
      if (gated) this.releaseInferGate();
    }
    if (!Array.isArray(rawList) || rawList.length !== batchInputs.length) {
      throw new (wasNative ? NativeRunError : Error)('Invalid translation output');
    }
    return rawList.map((raw, i) => this.extractOutputTokens(raw, batchInputs[i].length, requireEos, wasNative));
  }
  destroy(): Promise<void> {
    if (this.closed) return this.closed;
    this.closing = true;
    this.closed = (async () => {
      await this.initializing?.catch(() => {});
      await Promise.allSettled([...this.tasks]);
      await (await this.metal?.catch(() => undefined))?.close();
      try { await this.session?.release(); } finally { this.session = undefined; this.closeTokenizers(); this.shortlist = undefined; }
    })();
    return this.closed;
  }
}
