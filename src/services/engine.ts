import path from 'path';
import { parseFragment } from 'parse5';
import { readFile } from 'fs/promises';
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
}

const engines = new Map<string, EngineInfo>();
const inFlightTranslations = new WeakMap<TranslationEngine, Map<string, Promise<string>>>();
const loadingPromises = new Map<string, { lexicalShortlist: boolean; workersPerLanguage: number; promise: Promise<EngineInfo> }>();
const zhCommaTest = /,/;
const zhCommaGlobal = /,/g;

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
  sampleText: string
): Promise<TranslationEngine> {
  const key = `${fromLang}-${toLang}`;

  while (true) {
    let loading = loadingPromises.get(key);
    if (!loading) {
      const request = {
        lexicalShortlist: getConfig().lexicalShortlist === true,
        workersPerLanguage: getConfig().workersPerLanguage,
        promise: Promise.resolve().then(async (): Promise<EngineInfo> => {
          const config = getConfig();
          request.lexicalShortlist = config.lexicalShortlist === true;
          request.workersPerLanguage = config.workersPerLanguage;
          const existing = engines.get(key);
          if (existing) {
            resetIdleTimer(existing);
            const manifestPath = path.join(getConfig().modelDir, `${fromLang}_${toLang}`, 'manifest.json');
            const identity = await readFile(manifestPath, 'utf8').then(s => JSON.parse(s).identity).catch(() => null);
            if (identity === existing.engine.identity && existing.lexicalShortlist === request.lexicalShortlist
              && existing.workersPerLanguage === request.workersPerLanguage) {
              existing.lastUsed = new Date();
              resetIdleTimer(existing);
              return existing;
            }
            if (existing.timer) clearTimeout(existing.timer);
            engines.delete(key);
            await existing.engine.destroy();
          }

          logger.info(`Creating new engine for ${fromLang} -> ${toLang}`);
          const model = await models.prepareModel(toLang, fromLang, undefined, request.lexicalShortlist);
          const engine = new TranslationEngine({ sourceLang: fromLang, targetLang: toLang });
          await engine.init(model, {
            'max-length-break': config.maxSentenceLength,
            'max-concurrent-runs': request.workersPerLanguage === 0 ? undefined : request.workersPerLanguage,
          }, sampleText);
          const info: EngineInfo = {
            engine,
            // Keep the requested mode even when a model without lex falls back to K9.
            lexicalShortlist: request.lexicalShortlist,
            workersPerLanguage: request.workersPerLanguage,
            lastUsed: new Date(),
            fromLang,
            toLang,
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
      if (info.lexicalShortlist === (getConfig().lexicalShortlist === true)
        && info.workersPerLanguage === getConfig().workersPerLanguage) return info.engine;
    } catch (error) {
      if (loading.lexicalShortlist === (getConfig().lexicalShortlist === true)
        && loading.workersPerLanguage === getConfig().workersPerLanguage) throw error;
    }
    // Configuration changed while waiting; prepare again before serving a translation.
  }
}

function resetIdleTimer(info: EngineInfo) {
  if (info.timer) {
    clearTimeout(info.timer);
  }

  const config = getConfig();
  const timeout = config.workerIdleTimeout * 1000;

  info.timer = setTimeout(() => {
    if (info.engine.busy) { resetIdleTimer(info); return; }
    const key = `${info.fromLang}-${info.toLang}`;
    logger.info(`Engine ${key} idle timeout, stopping...`);
    void info.engine.destroy().catch(error => logger.error(`Engine cleanup failed: ${error}`));
    engines.delete(key);
    logger.info(`Engine ${key} stopped due to idle timeout`);
  }, timeout);
}

async function translateSingleLanguageText(
  fromLang: string,
  toLang: string,
  text: string,
  isHTML: boolean
): Promise<string> {
  const engine = await getOrCreateSingleEngine(fromLang, toLang, text);
  const cacheKey = () => [fromLang, toLang, engine.identity, engine.backend?.provider, engine.backend?.cpuThreads, engine.backend?.tuning, text, isHTML];
  const cached = readCache(cacheKey());
  if (cached !== null) return cached;
  const translate = async () => {
    const result = await engine.translateAsync(text, { html: isHTML });
    writeCache(result, cacheKey());
    return result;
  };
  if (getConfig().cacheSize <= 0) return translate();

  let pending = inFlightTranslations.get(engine);
  if (!pending) {
    pending = new Map();
    inFlightTranslations.set(engine, pending);
  }
  const key = JSON.stringify(cacheKey());
  const existing = pending.get(key);
  if (existing) return existing;
  const promise = translate().finally(() => pending.delete(key));
  pending.set(key, promise);
  return promise;
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
      result = '';
      let lastEnd = 0;

      for (const seg of segments) {
        if (seg.start > lastEnd) {
          result += text.substring(lastEnd, seg.start);
        }

        if (seg.language === toLang) {
          result += seg.text;
        } else {
          try {
            const translated = await translateSegment(seg.language, toLang, seg.text, isHTML);
            result += translated;
          } catch (error) {
            logger.error(`Failed to translate segment: ${error}`);
            result += seg.text;
          }
        }
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

  const results: string[] = [];

  for (let i = 0; i < sentences.length; i++) {
    const { segment } = sentences[i];
    try {
      const translated = await translateSegment(fromLang, toLang, segment, false);
      results.push(translated);

      if ((i + 1) % 10 === 0) {
        logger.debug(`Translated ${i + 1}/${sentences.length} sentences`);
      }
    } catch (error) {
      logger.error(`Failed to translate sentence ${i + 1}: ${error}`);
      results.push(segment);
    }
  }

  return results.join('');
}

export function getEngineRuntimes() {
  return [...engines.values()].map(({ fromLang, toLang, engine }) => ({ from: fromLang, to: toLang, ...engine.backend }));
}

export async function cleanupAllEngines() {
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
