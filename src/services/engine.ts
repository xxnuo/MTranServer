import path from 'path';
import { parseFragment } from 'parse5';
import { readFileSync, statSync } from 'fs';
import { access, readFile, readdir } from 'fs/promises';
import { TranslationEngine } from '@/core/engine.js';
import { getConfig } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import * as models from '@/models/index.js';
import { detectLanguage, detectMultipleLanguages } from './detector.js';
import { readCache, writeCache } from '@/utils/cache.js';

interface EngineInfo {
  engine: TranslationEngine;
  lexicalShortlist: boolean;
  workersPerLanguage: number;
  lastUsed: Date;
  fromLang: string;
  toLang: string;
  timer?: NodeJS.Timeout;
  timerTimeoutMs?: number;
  touched?: boolean;
  manifestPath: string;
  manifestModelDir: string;
  manifestCheckedAt: number;
}

const engines = new Map<string, EngineInfo>();
const inFlightTranslations = new WeakMap<TranslationEngine, Map<string, Promise<string>>>();
const loadingPromises = new Map<string, { lexicalShortlist: boolean; workersPerLanguage: number; promise: Promise<EngineInfo> }>();
const manifestIdentityCache = new Map<string, { ino: bigint; size: bigint; mtimeNs: bigint; ctimeNs: bigint; identity: unknown }>();
const tuningStringCache = new WeakMap<object, string>();
const EMPTY_CACHE_ARGS: readonly unknown[] = [];
const MANIFEST_CHECK_INTERVAL_MS = 2000;
let warmupPromise: Promise<void> | undefined;
let warmupController: AbortController | undefined;
const zhCommaTest = /,/;
const zhCommaGlobal = /,/g;

function readManifestIdentitySync(manifestPath: string): unknown {
  try {
    const st = statSync(manifestPath, { bigint: true, throwIfNoEntry: false });
    if (!st) {
      manifestIdentityCache.delete(manifestPath);
      return null;
    }
    const settled = Number(st.mtimeMs) + 25 < Date.now();
    const cached = manifestIdentityCache.get(manifestPath);
    if (settled && cached && cached.ino === st.ino && cached.size === st.size
      && cached.mtimeNs === st.mtimeNs && cached.ctimeNs === st.ctimeNs) {
      return cached.identity;
    }
    const identity = JSON.parse(readFileSync(manifestPath, 'utf8')).identity;
    if (settled) {
      if (manifestIdentityCache.size >= 128) manifestIdentityCache.delete(manifestIdentityCache.keys().next().value!);
      manifestIdentityCache.set(manifestPath, { ino: st.ino, size: st.size, mtimeNs: st.mtimeNs, ctimeNs: st.ctimeNs, identity });
    } else {
      manifestIdentityCache.delete(manifestPath);
    }
    return identity;
  } catch {
    manifestIdentityCache.delete(manifestPath);
    return null;
  }
}

function formatChinesePunctuation(text: string, toLang: string, isHTML: boolean, enabled: boolean): string {
  if (!enabled || isHTML || !toLang.startsWith('zh')) {
    return text;
  }
  if (!zhCommaTest.test(text)) {
    return text;
  }
  return text.replace(zhCommaGlobal, '，');
}

function needsPivotTranslation(fromLang: string, toLang: string): boolean {
  if (fromLang === 'en' || toLang === 'en') {
    return false;
  }

  if (models.hasLanguagePair(fromLang, toLang)) {
    return false;
  }

  return true;
}

async function getOrCreateSingleEngine(
  fromLang: string,
  toLang: string,
  sampleText = 'Hello, this is a translation test.',
  localOnly = false,
  signal?: AbortSignal
): Promise<TranslationEngine> {
  const key = `${fromLang}-${toLang}`;

  while (true) {
    signal?.throwIfAborted();
    let loading = loadingPromises.get(key);
    if (!loading) {
      const fastConfig = getConfig();
      const existingFast = engines.get(key);
      if (existingFast && existingFast.lexicalShortlist === (fastConfig.lexicalShortlist === true)
        && existingFast.workersPerLanguage === fastConfig.workersPerLanguage) {
        const now = Date.now();
        let valid = existingFast.manifestModelDir === fastConfig.modelDir
          && now - existingFast.manifestCheckedAt < MANIFEST_CHECK_INTERVAL_MS;
        if (!valid) {
          const manifestPath = existingFast.manifestModelDir === fastConfig.modelDir
            ? existingFast.manifestPath
            : path.join(fastConfig.modelDir, `${fromLang}_${toLang}`, 'manifest.json');
          if (readManifestIdentitySync(manifestPath) === existingFast.engine.identity) {
            existingFast.manifestModelDir = fastConfig.modelDir;
            existingFast.manifestPath = manifestPath;
            existingFast.manifestCheckedAt = now;
            valid = true;
          }
        }
        if (valid) {
          touchIdleTimer(existingFast);
          return existingFast.engine;
        }
      }
      const request = {
        lexicalShortlist: fastConfig.lexicalShortlist === true,
        workersPerLanguage: fastConfig.workersPerLanguage,
        promise: Promise.resolve().then(async (): Promise<EngineInfo> => {
          const config = getConfig();
          request.lexicalShortlist = config.lexicalShortlist === true;
          request.workersPerLanguage = config.workersPerLanguage;
          const existing = engines.get(key);
          const manifestPath = path.join(config.modelDir, `${fromLang}_${toLang}`, 'manifest.json');
          if (existing) {
            resetIdleTimer(existing);
            const identity = await readFile(manifestPath, 'utf8').then(s => JSON.parse(s).identity).catch(() => null);
            if (identity === existing.engine.identity && existing.lexicalShortlist === request.lexicalShortlist
              && existing.workersPerLanguage === request.workersPerLanguage) {
              existing.lastUsed = new Date();
              existing.manifestModelDir = config.modelDir;
              existing.manifestPath = manifestPath;
              existing.manifestCheckedAt = Date.now();
              resetIdleTimer(existing);
              return existing;
            }
            if (existing.timer) clearTimeout(existing.timer);
            engines.delete(key);
            await existing.engine.destroy();
          }

          logger.info(`Creating new engine for ${fromLang} -> ${toLang}`);
          const model = localOnly
            ? await models.prepareModel(toLang, fromLang, undefined, request.lexicalShortlist, true)
            : await models.prepareModel(toLang, fromLang, undefined, request.lexicalShortlist);
          signal?.throwIfAborted();
          const engine = new TranslationEngine({ sourceLang: fromLang, targetLang: toLang });
          try {
            await engine.init(model, {
              'max-length-break': config.maxSentenceLength,
              'max-concurrent-runs': request.workersPerLanguage === 0 ? undefined : request.workersPerLanguage,
            }, sampleText);
          } catch (error) {
            await engine.destroy().catch(cleanup => logger.error(`Engine cleanup failed: ${cleanup}`));
            throw error;
          }
          const info: EngineInfo = {
            engine,
            // Keep the requested mode even when a model without lex falls back to K9.
            lexicalShortlist: request.lexicalShortlist,
            workersPerLanguage: request.workersPerLanguage,
            lastUsed: new Date(),
            fromLang,
            toLang,
            manifestPath,
            manifestModelDir: config.modelDir,
            manifestCheckedAt: Date.now(),
          };
          resetIdleTimer(info);
          engines.set(key, info);
          logger.info(`Engine created successfully for ${fromLang} -> ${toLang}`);
          return info;
        }).finally(() => loadingPromises.delete(key)),
      };
      loadingPromises.set(key, request);
      loading = request;
    }

    try {
      const info = await loading.promise;
      signal?.throwIfAborted();
      if (info.lexicalShortlist === (getConfig().lexicalShortlist === true)
        && info.workersPerLanguage === getConfig().workersPerLanguage) return info.engine;
    } catch (error) {
      if (loading.lexicalShortlist === (getConfig().lexicalShortlist === true)
        && loading.workersPerLanguage === getConfig().workersPerLanguage) throw error;
    }
    // Configuration changed while waiting; prepare again before serving a translation.
  }
}

function touchIdleTimer(info: EngineInfo) {
  if (warmupPromise) {
    if (info.timer) {
      clearTimeout(info.timer);
      info.timer = undefined;
      info.timerTimeoutMs = undefined;
    }
    info.touched = false;
    return;
  }
  const timeout = getConfig().workerIdleTimeout * 1000;
  if (info.timer && info.timerTimeoutMs === timeout) {
    info.touched = true;
    return;
  }
  resetIdleTimer(info);
}

function resetIdleTimer(info: EngineInfo) {
  if (info.timer) {
    clearTimeout(info.timer);
  }
  info.timer = undefined;
  info.timerTimeoutMs = undefined;
  info.touched = false;
  if (warmupPromise) return;

  const config = getConfig();
  const timeout = config.workerIdleTimeout * 1000;
  info.timerTimeoutMs = timeout;

  info.timer = setTimeout(() => {
    const key = `${info.fromLang}-${info.toLang}`;
    if (engines.get(key) !== info) return;
    if (info.engine.busy || info.touched) {
      info.lastUsed = new Date();
      resetIdleTimer(info);
      return;
    }
    logger.info(`Engine ${key} idle timeout, stopping...`);
    void info.engine.destroy().catch(error => logger.error(`Engine cleanup failed: ${error}`));
    engines.delete(key);
    logger.info(`Engine ${key} stopped due to idle timeout`);
  }, timeout);
}

export function warmupEngines(): Promise<void> {
  if (warmupPromise) return warmupPromise;
  const controller = warmupController = new AbortController();
  return warmupPromise = Promise.resolve().then(async () => {
    for (const info of engines.values()) resetIdleTimer(info);
    while (true) {
      controller.signal.throwIfAborted();
      const { modelDir, lexicalShortlist, workersPerLanguage } = getConfig();
      const entries = (await readdir(modelDir, { withFileTypes: true })).sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
      for (const entry of entries) {
        controller.signal.throwIfAborted();
        if (!entry.isDirectory() || !/^[A-Za-z0-9-]+_[A-Za-z0-9-]+$/.test(entry.name)) continue;
        const [fromLang, toLang] = entry.name.split('_');
        try {
          try { await access(path.join(modelDir, entry.name, 'manifest.json')); }
          catch (error: any) { if (error.code === 'ENOENT') continue; throw error; }
          logger.info(`Warming up engine for ${fromLang} -> ${toLang}`);
          await getOrCreateSingleEngine(fromLang, toLang, undefined, true, controller.signal);
        } catch (error) {
          controller.signal.throwIfAborted();
          logger.warn(`Engine warmup failed for ${fromLang} -> ${toLang}: ${error}`);
        }
      }
      controller.signal.throwIfAborted();
      const current = getConfig();
      if (modelDir === current.modelDir && lexicalShortlist === current.lexicalShortlist
        && workersPerLanguage === current.workersPerLanguage) return;
    }
  }).finally(() => {
    warmupPromise = undefined;
    warmupController = undefined;
    // Start the idle budget after all models have finished warming up.
    for (const info of engines.values()) resetIdleTimer(info);
  });
}

function serializeTuning(tuning: unknown): string {
  if (!tuning || typeof tuning !== 'object') return String(tuning ?? '');
  let cached = tuningStringCache.get(tuning);
  if (cached === undefined) {
    cached = JSON.stringify(tuning);
    tuningStringCache.set(tuning, cached);
  }
  return cached;
}

function getCachePrefix(fromLang: string, toLang: string, engine: TranslationEngine, isHTML: boolean) {
  const backend = engine.backend;
  const tuning = serializeTuning(backend?.tuning);
  return {
    flat: `${fromLang}\0${toLang}\0${engine.identity}\0${backend?.provider ?? ''}\0${backend?.runtime ?? ''}\0${backend?.cpuThreads ?? ''}\0${tuning}\0${isHTML ? '1' : '0'}`,
    tuning,
    backend,
  };
}

function buildCacheArgs(
  fromLang: string,
  toLang: string,
  engine: TranslationEngine,
  tuning: string,
  text: string,
  isHTML: boolean
): unknown[] {
  const backend = engine.backend;
  return [fromLang, toLang, engine.identity, backend?.provider, backend?.runtime, backend?.cpuThreads, tuning, text, isHTML];
}

async function translateSingleLanguageText(
  fromLang: string,
  toLang: string,
  text: string,
  isHTML: boolean
): Promise<string> {
  const engine = await getOrCreateSingleEngine(fromLang, toLang, text);
  if (getConfig().cacheSize <= 0) {
    const cached = readCache(EMPTY_CACHE_ARGS as any[]);
    if (cached !== null) return cached;
    return engine.translateAsync(text, { html: isHTML });
  }
  const startPrefix = getCachePrefix(fromLang, toLang, engine, isHTML);
  const startKey = buildCacheArgs(fromLang, toLang, engine, startPrefix.tuning, text, isHTML);
  const cached = readCache(startKey);
  if (cached !== null) return cached;
  const key = `${startPrefix.flat}\0${text}`;
  const translate = async () => {
    const result = await engine.translateAsync(text, { html: isHTML });
    if (getCachePrefix(fromLang, toLang, engine, isHTML).flat === startPrefix.flat) writeCache(result, startKey);
    return result;
  };

  let pending = inFlightTranslations.get(engine);
  if (!pending) {
    pending = new Map();
    inFlightTranslations.set(engine, pending);
  }
  const existing = pending.get(key);
  if (existing) return existing;
  const promise = translate().finally(() => pending.delete(key));
  pending.set(key, promise);
  return promise;
}

async function translateSingleLanguageBatch(
  fromLang: string,
  toLang: string,
  texts: string[],
  isHTML: boolean
): Promise<string[]> {
  if (texts.length === 0) return [];
  if (texts.length === 1) return [await translateSingleLanguageText(fromLang, toLang, texts[0], isHTML)];

  const engine = await getOrCreateSingleEngine(fromLang, toLang, texts[0]);
  if (getConfig().cacheSize <= 0) {
    readCache(EMPTY_CACHE_ARGS as any[]);
    return typeof engine.translateBatchAsync === 'function'
      ? engine.translateBatchAsync(texts, { html: isHTML })
      : Promise.all(texts.map(text => engine.translateAsync(text, { html: isHTML })));
  }

  const startPrefix = getCachePrefix(fromLang, toLang, engine, isHTML);
  const results = new Array<Promise<string> | string>(texts.length);
  let pending = inFlightTranslations.get(engine);
  if (!pending) {
    pending = new Map();
    inFlightTranslations.set(engine, pending);
  }

  const misses: Array<{ text: string; key: string; startKey: unknown[]; resolve: (v: string) => void; reject: (e: unknown) => void }> = [];
  for (let i = 0; i < texts.length; i++) {
    const text = texts[i];
    const startKey = buildCacheArgs(fromLang, toLang, engine, startPrefix.tuning, text, isHTML);
    const cached = readCache(startKey);
    if (cached !== null) {
      results[i] = cached;
      continue;
    }
    const key = `${startPrefix.flat}\0${text}`;
    const existing = pending.get(key);
    if (existing) {
      results[i] = existing;
      continue;
    }
    let resolve!: (v: string) => void;
    let reject!: (e: unknown) => void;
    const itemPromise = new Promise<string>((res, rej) => { resolve = res; reject = rej; });
    const tracked = itemPromise.finally(() => pending!.delete(key));
    pending.set(key, tracked);
    misses.push({ text, key, startKey, resolve, reject });
    results[i] = tracked;
  }

  if (misses.length > 0) {
    const batchInputs = misses.map(m => m.text);
    const batchPromise = typeof engine.translateBatchAsync === 'function'
      ? engine.translateBatchAsync(batchInputs, { html: isHTML })
      : Promise.all(batchInputs.map(t => engine.translateAsync(t, { html: isHTML })));
    batchPromise.then(
      batchResults => {
        const stable = getCachePrefix(fromLang, toLang, engine, isHTML).flat === startPrefix.flat;
        for (let i = 0; i < misses.length; i++) {
          const value = batchResults[i];
          if (stable) writeCache(value, misses[i].startKey);
          misses[i].resolve(value);
        }
      },
      error => {
        for (const miss of misses) miss.reject(error);
      }
    );
  }

  return Promise.all(results);
}

async function translateSegmentBatch(
  fromLang: string,
  toLang: string,
  texts: string[],
  isHTML: boolean
): Promise<string[]> {
  if (fromLang === toLang) return texts;
  if (!needsPivotTranslation(fromLang, toLang)) return translateSingleLanguageBatch(fromLang, toLang, texts, isHTML);
  const intermediate = await translateSingleLanguageBatch(fromLang, 'en', texts, isHTML);
  return translateSingleLanguageBatch('en', toLang, intermediate, isHTML);
}

export async function translateBatchWithPivot(
  fromLang: string,
  toLang: string,
  texts: string[],
  isHTML: boolean = false
): Promise<string[]> {
  if (texts.length === 0) return [];
  if (texts.length === 1) return [await translateWithPivot(fromLang, toLang, texts[0], isHTML)];

  let results: string[];
  if (fromLang !== 'auto' && fromLang === toLang) {
    results = [...texts];
  } else if (fromLang !== 'auto') {
    results = await translateSegmentBatch(fromLang, toLang, texts, isHTML);
  } else if (isHTML) {
    results = await Promise.all(texts.map(text => translateWithPivot(fromLang, toLang, text, true)));
  } else {
    const config = getConfig();
    const detected = await Promise.all(texts.map(async text => {
      const segments = await detectMultipleLanguages(text);
      if (segments.length <= 1) {
        const lang = segments.length === 1 ? segments[0].language : await detectLanguage(text);
        if (!lang) throw new Error('Failed to detect source language');
        return { lang, text, multi: false as const };
      }
      return { lang: '', text, multi: true as const };
    }));
    const groups = new Map<string, number[]>();
    const out = new Array<string>(texts.length);
    const complexPromises: Array<Promise<void>> = [];
    for (let i = 0; i < detected.length; i++) {
      const item = detected[i];
      if (!item.multi && item.lang === toLang) {
        out[i] = item.text;
      } else if (!item.multi && item.text.length <= config.maxSentenceLength) {
        let indices = groups.get(item.lang);
        if (!indices) { indices = []; groups.set(item.lang, indices); }
        indices.push(i);
      } else {
        complexPromises.push(
          translateWithPivot(fromLang, toLang, item.text, false).then(res => { out[i] = res; })
        );
      }
    }
    const groupPromises = [...groups.entries()].map(async ([lang, indices]) => {
      const batchTexts = indices.map(idx => texts[idx]);
      const batchOut = await translateSegmentBatch(lang, toLang, batchTexts, false);
      for (let j = 0; j < indices.length; j++) out[indices[j]] = batchOut[j];
    });
    await Promise.all([...groupPromises, ...complexPromises]);
    results = out;
  }

  if (!toLang.startsWith('zh')) return results;
  const { fullwidthZhPunctuation } = getConfig();
  return results.map(r => formatChinesePunctuation(r, toLang, isHTML, fullwidthZhPunctuation));
}

async function translateSegment(
  fromLang: string,
  toLang: string,
  text: string,
  isHTML: boolean
): Promise<string> {
  if (fromLang === toLang) {
    return text;
  }

  if (!needsPivotTranslation(fromLang, toLang)) {
    return translateSingleLanguageText(fromLang, toLang, text, isHTML);
  }

  const intermediateText = await translateSingleLanguageText(fromLang, 'en', text, isHTML);
  return translateSingleLanguageText('en', toLang, intermediateText, isHTML);
}

export async function translateWithPivot(
  fromLang: string,
  toLang: string,
  text: string,
  isHTML: boolean = false
): Promise<string> {
  logger.debug(
    `TranslateWithPivot: ${fromLang} -> ${toLang}, text length: ${text.length}, isHTML: ${isHTML}`
  );

  let config: ReturnType<typeof getConfig> | null = null;
  let result = text;

  if (fromLang !== 'auto' && fromLang === toLang) {
    result = text;
  } else if (fromLang !== 'auto') {
    result = await translateSegment(fromLang, toLang, text, isHTML);
  } else if (isHTML) {
    const parts: string[] = [];
    const visit = (node: any) => {
      if (['script', 'style', 'noscript'].includes(node.tagName)) return;
      if (node.nodeName === '#text') parts.push(node.value);
      for (const child of node.childNodes ?? []) visit(child);
    };
    visit(parseFragment(text));
    const language = await detectLanguage(parts.join(' '));
    if (!language) throw new Error('Failed to detect source language');
    result = language === toLang ? text : await translateSegment(language, toLang, text, true);
  } else {
    const segments = await detectMultipleLanguages(text);

    if (segments.length <= 1) {
      let effectiveFromLang: string;
      if (segments.length === 1) {
        effectiveFromLang = segments[0].language;
      } else if (fromLang === 'auto') {
        const detected = await detectLanguage(text);
        if (!detected) {
          throw new Error('Failed to detect source language');
        }
        effectiveFromLang = detected;
      } else {
        effectiveFromLang = fromLang;
      }

      if (effectiveFromLang === toLang) {
        result = text;
      } else if (!isHTML) {
        config = config ?? getConfig();
        result = text.length > config.maxSentenceLength
          ? await translateLongText(effectiveFromLang, toLang, text)
          : await translateSegment(effectiveFromLang, toLang, text, isHTML);
      } else {
        result = await translateSegment(effectiveFromLang, toLang, text, isHTML);
      }
    } else {
      logger.debug(`Detected ${segments.length} language segments`);
      const translatedSegments = await Promise.all(
        segments.map(async seg => {
          if (seg.language === toLang) return seg.text;
          try {
            return await translateSegment(seg.language, toLang, seg.text, isHTML);
          } catch (error) {
            logger.error(`Failed to translate segment: ${error}`);
            return seg.text;
          }
        })
      );
      result = '';
      let lastEnd = 0;
      for (let i = 0; i < segments.length; i++) {
        const seg = segments[i];
        if (seg.start > lastEnd) {
          result += text.substring(lastEnd, seg.start);
        }
        result += translatedSegments[i];
        lastEnd = seg.end;
      }

      if (lastEnd < text.length) {
        result += text.substring(lastEnd);
      }
    }
  }

  if (!toLang.startsWith('zh')) {
    return result;
  }
  config = config ?? getConfig();
  return formatChinesePunctuation(result, toLang, isHTML, config.fullwidthZhPunctuation);
}

async function translateLongText(
  fromLang: string,
  toLang: string,
  text: string
): Promise<string> {
  logger.debug(`Splitting long text (${text.length} chars) into sentences`);

  const segmenterAny = new (Intl as any).Segmenter(undefined, { granularity: 'sentence' });
  const sentences = Array.from(segmenterAny.segment(text)) as Array<{ segment: string, index: number }>;

  logger.debug(`Split into ${sentences.length} sentences`);
  const segments = sentences.map(s => s.segment);
  if (segments.length === 0) return '';

  try {
    const translated = await translateSegmentBatch(fromLang, toLang, segments, false);
    return translated.join('');
  } catch {
    const results = await Promise.all(
      segments.map(async (segment, i) => {
        try {
          return await translateSegment(fromLang, toLang, segment, false);
        } catch (error) {
          logger.error(`Failed to translate sentence ${i + 1}: ${error}`);
          return segment;
        }
      })
    );
    return results.join('');
  }
}

export function getEngineRuntimes() {
  return [...engines.values()].map(({ fromLang, toLang, engine }) => ({ from: fromLang, to: toLang, ...engine.backend }));
}

export async function cleanupAllEngines() {
  warmupController?.abort(new Error('Engine warmup interrupted by shutdown'));
  await warmupPromise?.catch(() => {});
  await Promise.allSettled([...loadingPromises.values()].map(loading => loading.promise));
  const closing: Promise<void>[] = [];
  logger.info(`Cleaning up ${engines.size} engine(s)...`);

  for (const [key, info] of engines.entries()) {
    if (info.timer) {
      clearTimeout(info.timer);
    }
    closing.push(info.engine.destroy());
    logger.debug(`Stopped engine: ${key}`);
  }

  engines.clear();
  await Promise.all(closing);
  logger.info('All engines cleaned up successfully');
}
