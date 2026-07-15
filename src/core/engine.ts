import { BergamotModule } from '@/core/interfaces.js';
import { ModelBuffers } from '@/core/loader.js';
import { isCJKCode } from '@/utils/lang-alias.js';
import * as logger from '@/logger/index.js';

const HTML_ENTITY_PATTERN = /&(?!(?:#\d+|#x[a-fA-F0-9]+|[a-zA-Z][a-zA-Z0-9]+);)/g;
const INVALID_HTML_LT_PATTERN = /<(?!\/?[a-zA-Z][\w:-]*(?:\s[^<>]*?)?\/?>|!--|!DOCTYPE\b|\?xml\b)/g;
const HTML_LIKE_PATTERN = /<\/?[a-zA-Z][\w:-]*(?:\s[^<>]*?)?\/?>|<!--|<!DOCTYPE\b|<\?xml\b/i;
const WASM_ABORT_PATTERN = /aborted|abort/i;
const FATAL_WASM_ERROR_PATTERN =
  new RegExp(
    `${WASM_ABORT_PATTERN.source}|memory access out of bounds|invalid memory access|invalid table access|unreachable`,
    'i'
  );

export interface TranslationOptions {
  sourceLang?: string;
  targetLang?: string;
  cacheSize?: number;
}

export interface TranslateOptions {
  qualityScores?: boolean;
  alignment?: boolean;
  html?: boolean;
  signal?: AbortSignal;
}

export interface TranslationConfig {
  'beam-size'?: number;
  'normalize'?: number;
  'word-penalty'?: number;
  'max-length-break'?: number;
  'mini-batch-words'?: number;
  'workspace'?: number;
  'max-length-factor'?: number;
  'skip-cost'?: boolean;
  'cpu-threads'?: number;
  'quiet'?: boolean;
  'quiet-translation'?: boolean;
  'gemm-precision'?: string;
  'alignment'?: string;
}

interface QueueTask {
  text: string;
  options: TranslateOptions;
  resolve: (value: string) => void;
  reject: (reason: any) => void;
  signal?: AbortSignal;
  onAbort?: () => void;
}

export class TranslationEngine {
  private options: TranslationOptions;
  private bergamot: BergamotModule | null = null;
  private service: any = null;
  private model: any = null;
  private isReady = false;
  private translating = false;
  private pendingQueue: QueueTask[] = [];
  private maxSentenceLength = 512;

  constructor(options: TranslationOptions = {}) {
    this.options = options;
  }

  async init(bergamotModule: BergamotModule, modelBuffers: ModelBuffers, config: TranslationConfig = {}): Promise<void> {
    if (this.isReady) return;

    this.bergamot = bergamotModule;

    const defaultConfig: TranslationConfig = {
      'beam-size': 1,
      'normalize': 1.0,
      'word-penalty': 0,
      'max-length-break': 512,
      'mini-batch-words': 1024,
      'workspace': 128,
      'max-length-factor': 2.0,
      'skip-cost': true,
      'cpu-threads': 0,
      'quiet': true,
      'quiet-translation': true,
      'gemm-precision': 'int8shiftAlphaAll',
      'alignment': 'soft'
    };

    const mergedConfig = { ...defaultConfig, ...config };

    const MODEL_FILE_ALIGNMENTS: Record<string, number> = {
      model: 256,
      lex: 64,
      srcvocab: 64,
      trgvocab: 64,
    };

    const alignedMemories: Record<string, any> = {};
    for (const [key, buffer] of Object.entries(modelBuffers)) {
      const alignment = MODEL_FILE_ALIGNMENTS[key] || 64;
      const alignedMemory = new this.bergamot.AlignedMemory(buffer.byteLength || buffer.length, alignment);
      const view = alignedMemory.getByteArrayView();
      view.set(new Uint8Array(buffer));
      alignedMemories[key] = alignedMemory;
    }

    const vocabList = new this.bergamot.AlignedMemoryList();
    vocabList.push_back(alignedMemories.srcvocab);
    vocabList.push_back(alignedMemories.trgvocab);

    const configStr = Object.entries(mergedConfig)
      .map(([k, v]) => `${k}: ${v}`)
      .join('\n');

    this.model = new this.bergamot.TranslationModel(
      this.options.sourceLang || "en",
      this.options.targetLang || "zh-Hans",
      configStr,
      alignedMemories.model,
      alignedMemories.lex,
      vocabList,
      alignedMemories.qualityModel || null
    );

    this.service = new this.bergamot.BlockingService({
      cacheSize: this.options.cacheSize || 0
    });

    this.isReady = true;
  }

  translate(text: string, options: TranslateOptions = {}): string {
    if (!this.isReady) throw new Error("Engine not initialized");
    this._throwIfAborted(options.signal);

    let processedText = text;
    const shouldUseHtml = !!options.html && this._looksLikeHTML(text);
    if (shouldUseHtml) {
      processedText = this._sanitizeHTML(text);
    }

    const { taggedText, replacements, forceHtml } = this._tagPlaceholders(processedText, shouldUseHtml);
    const { cleanText, replacements: emojiReplacements } = this._hideEmojis(taggedText);
    const normalizedOptions: TranslateOptions = { ...options, html: shouldUseHtml };
    const effectiveOptions: TranslateOptions = forceHtml ? { ...normalizedOptions, html: true } : normalizedOptions;

    let translation: string;
    try {
      if (cleanText.length > this.maxSentenceLength && !effectiveOptions.html) {
        translation = this._translateLongText(cleanText, effectiveOptions);
      } else {
        translation = this._translateInternal(cleanText, effectiveOptions);
      }
      this._throwIfAborted(options.signal);
    } catch (error: any) {
      // Never treat a client-initiated abort as a fatal WASM engine failure.
      if (error.name !== 'AbortError' && this._isFatalWASMError(error)) {
        this.isReady = false;
        throw new Error(`Fatal WASM error: ${error.message}`);
      }
      throw error;
    }

    translation = this._restoreEmojis(translation, emojiReplacements);
    translation = this._restoreTaggedPlaceholders(translation, replacements);

    return translation;
  }

  private _sanitizeHTML(text: string): string {
    text = text.replace(/<(\d+\.\d+)[^>]*>/g, '&lt;$1&gt;');
    text = text.replace(/<([^a-zA-Z/!?][^>]*)>/g, '&lt;$1&gt;');
    const unclosedTags = /<([a-zA-Z]+)(?:\s[^>]*)?>(?![\s\S]*<\/\1>)/g;
    text = text.replace(unclosedTags, (match) => {
      if (match.endsWith('/>')) {
        return match;
      }
      return match.replace(/</g, '&lt;').replace(/>/g, '&gt;');
    });
    text = text.replace(HTML_ENTITY_PATTERN, '&amp;');
    text = text.replace(INVALID_HTML_LT_PATTERN, '&lt;');

    return text;
  }

  private _looksLikeHTML(text: string): boolean {
    return HTML_LIKE_PATTERN.test(text);
  }

  async translateAsync(text: string, options: TranslateOptions = {}): Promise<string> {
    return new Promise((resolve, reject) => {
      const task: QueueTask = {
        text,
        options,
        resolve,
        reject,
        signal: options.signal,
      };
      if (task.signal?.aborted) {
        reject(this._abortError());
        return;
      }
      task.onAbort = () => {
        const index = this.pendingQueue.indexOf(task);
        if (index !== -1) {
          this.pendingQueue.splice(index, 1);
          reject(this._abortError());
        }
      };
      task.signal?.addEventListener('abort', task.onAbort, { once: true });
      this.pendingQueue.push(task);
      this._processQueue();
    });
  }

  private _processQueue(): void {
    if (this.translating || this.pendingQueue.length === 0) return;

    this.translating = true;
    const task = this.pendingQueue.shift()!;
    if (task.signal?.aborted) {
      task.reject(this._abortError());
      this._finishTask(task);
      return;
    }

    try {
      const result = this.translate(task.text, task.options);
      task.resolve(result);
    } catch (error) {
      task.reject(error);
    } finally {
      this._finishTask(task);
    }
  }

  private _finishTask(task: QueueTask): void {
    if (task.signal && task.onAbort) {
      task.signal.removeEventListener('abort', task.onAbort);
    }
    this.translating = false;
    setImmediate(() => this._processQueue());
  }

  private _abortError(): Error {
    const error = new Error('Translation request aborted');
    error.name = 'AbortError';
    return error;
  }

  private _throwIfAborted(signal?: AbortSignal): void {
    if (signal?.aborted) {
      throw this._abortError();
    }
  }

  private _translateInternal(text: string, options: TranslateOptions = {}): string {
    if (!this.bergamot) throw new Error("Bergamot module not initialized");

    if (!text || text.trim().length === 0) {
      return text;
    }

    let cleanedText = text;
    if (!options.html) {
      cleanedText = cleanedText.replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, '');
      cleanedText = cleanedText.replace(/\uFFFD/g, '');
    }

    const messages = new this.bergamot.VectorString();
    const responseOptions = new this.bergamot.VectorResponseOptions();

    try {
      this._throwIfAborted(options.signal);
      messages.push_back(cleanedText);
      responseOptions.push_back({
        qualityScores: options.qualityScores || false,
        alignment: options.alignment !== undefined ? options.alignment : true,
        html: options.html || false
      });

      const responses = this.service.translate(this.model, messages, responseOptions);
      try {
        const result = responses.get(0);
        const translatedText = result.getTranslatedText();
        this._throwIfAborted(options.signal);
        return translatedText;
      } finally {
        responses.delete();
      }
    } catch (error: any) {
      // Don't log or re-wrap client-initiated cancellations as WASM/HTML errors.
      if (error.name === 'AbortError') throw error;
      logger.error(
        `WASM Error Context: TextLength=${cleanedText.length}, Options=${JSON.stringify(options)}`,
        error
      );
      if (options.html && error?.message && WASM_ABORT_PATTERN.test(error.message)) {
        const wrappedError = new Error(`HTML parse error: ${error.message}`);
        (wrappedError as Error & { cause?: unknown }).cause = error;
        throw wrappedError;
      }
      throw error;
    } finally {
      messages.delete();
      responseOptions.delete();
    }
  }

  private _isFatalWASMError(error: Error): boolean {
    return FATAL_WASM_ERROR_PATTERN.test(error.message);
  }

  private _getMappedSeparator(sep: string, targetLang?: string): string {
    if (!targetLang) return sep;

    const isTargetCJK = isCJKCode(targetLang);

    const map: Record<string, { cjk: string; nonCjk: string }> = {
      ". ": { cjk: "。", nonCjk: ". " },
      "。": { cjk: "。", nonCjk: ". " },
      "！": { cjk: "！", nonCjk: "! " },
      "!": { cjk: "！", nonCjk: "! " },
      "？": { cjk: "？", nonCjk: "? " },
      "?": { cjk: "？", nonCjk: "? " },
      "; ": { cjk: "；", nonCjk: "; " },
      "；": { cjk: "；", nonCjk: "; " },
      "：": { cjk: "：", nonCjk: ": " },
      ": ": { cjk: "：", nonCjk: ": " },
      "，": { cjk: "，", nonCjk: ", " },
      ", ": { cjk: "，", nonCjk: ", " }
    };

    if (sep in map) {
      return isTargetCJK ? map[sep].cjk : map[sep].nonCjk;
    }

    return sep;
  }

  private _translateLongText(text: string, options: TranslateOptions = {}): string {
    const separators = [
      "\n", " - ", ". ", "。", "！", "!", "？", "?",
      "; ", "；", "：", ": ", "，", ", "
    ];

    let bestSep = "";
    let bestParts: string[] = [];
    let minMaxLen = text.length;

    for (const sep of separators) {
      const parts = text.split(sep);
      if (parts.length > 1) {
        let maxLen = 0;
        for (const p of parts) {
          if (p.length > maxLen) maxLen = p.length;
        }

        if (maxLen < minMaxLen) {
          minMaxLen = maxLen;
          bestSep = sep;
          bestParts = parts;
        }

        if (maxLen <= this.maxSentenceLength) {
          break;
        }
      }
    }

    if (bestParts.length <= 1) {
      bestParts = this._chunkByWordBoundary(text, this.maxSentenceLength);
      bestSep = "";
    }

    const results = bestParts.map(part => {
      if (!part.trim()) return part;
      if (part.length > this.maxSentenceLength) {
        return this._translateLongText(part, options);
      }
      return this._translateInternal(part, options);
    });

    const targetSep = this._getMappedSeparator(bestSep, this.options.targetLang);
    return results.join(targetSep);
  }

  private _chunkByWordBoundary(text: string, limit: number): string[] {
    const parts = text.split(/(\s+)/);
    const chunks: string[] = [];
    let currentChunk = "";

    for (const part of parts) {
      if (currentChunk.length + part.length <= limit) {
        currentChunk += part;
      } else {
        if (currentChunk.length > 0) {
          chunks.push(currentChunk);
          currentChunk = "";
        }

        if (part.length > limit) {
          const subChunks = this._chunkByLength(part, limit);
          chunks.push(...subChunks);
        } else {
          currentChunk = part;
        }
      }
    }

    if (currentChunk.length > 0) {
      chunks.push(currentChunk);
    }

    return chunks;
  }

  private _chunkByLength(text: string, chunkSize: number): string[] {
    if (chunkSize <= 0) return [text];
    const chunks: string[] = [];
    const codePoints = Array.from(text);
    for (let i = 0; i < codePoints.length; i += chunkSize) {
      chunks.push(codePoints.slice(i, i + chunkSize).join(''));
    }
    return chunks;
  }

  private _hideEmojis(text: string): { cleanText: string; replacements: Array<{ original: string; placeholder: string }> } {
    const emojiRegex = /(\p{RI}\p{RI}|\p{Emoji_Presentation}|\p{Extended_Pictographic})/gu;
    const replacements: Array<{ original: string; placeholder: string }> = [];
    const cleanText = text.replace(emojiRegex, (match) => {
      const placeholder = `[EE${replacements.length}]`;
      replacements.push({ original: match, placeholder });
      return placeholder;
    });
    return { cleanText, replacements };
  }

  private _restoreEmojis(text: string, replacements: Array<{ original: string; placeholder: string }>): string {
    let result = text;
    for (let i = 0; i < replacements.length; i++) {
      const { original } = replacements[i];
      const pattern = new RegExp(`\\[EE${i}\\]`, 'gi');
      result = result.replace(pattern, original);
    }
    return result;
  }

  private _tagPlaceholders(
    text: string,
    htmlEnabled: boolean = false
  ): { taggedText: string; replacements: string[]; forceHtml: boolean } {
    const placeholderRegex = /(\{\d+\}|\[\d+\])/g;
    if (!placeholderRegex.test(text)) {
      return { taggedText: text, replacements: [], forceHtml: false };
    }

    const replacements: string[] = [];
    const taggedText = text.replace(placeholderRegex, (match) => {
      const index = replacements.length;
      replacements.push(match);
      if (htmlEnabled) {
        return `<mt${index} />`;
      }
      return `MT_PLACEHOLDER_${index}_TOKEN`;
    });

    return { taggedText, replacements, forceHtml: false };
  }

  private _restoreTaggedPlaceholders(text: string, replacements: string[]): string {
    if (replacements.length === 0) {
      return text;
    }

    let result = text;
    result = result.replace(/<\/mt\d+>/gi, '');
    result = result.replace(/<mt(\d+)\s*\/?>/gi, (_, index) => {
      const idx = Number(index);
      return replacements[idx] ?? _;
    });
    result = result.replace(/MT_PLACEHOLDER_(\d+)_TOKEN/g, (_, index) => {
      const idx = Number(index);
      return replacements[idx] ?? _;
    });

    return result;
  }

  destroy(): void {
    try {
      if (this.model) {
        this.model.delete();
        this.model = null;
      }
      if (this.service) {
        this.service.delete();
        this.service = null;
      }
      this.isReady = false;
    } catch (error) {
      console.error('Error during cleanup:', error);
    }
  }
}
