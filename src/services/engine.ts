import path from 'path';
import { readFile } from 'fs/promises';
import { TranslationEngine } from '@/core/engine.js';
import { createResourceLoader } from '@/core/factory.js';
import { getConfig } from '@/config/index.js';
import loadBergamot from '@/lib/bergamot/bergamot-translator.js';
import wasmPath from '@/lib/bergamot/bergamot-translator.wasm' with { type: 'file' };
import * as logger from '@/logger/index.js';
import * as models from '@/models/index.js';
import { detectLanguage, detectMultipleLanguages } from './detector.js';
import { readCache, writeCache } from '@/utils/cache.js';

interface EngineInfo {
  engine: TranslationEngine;
  lastUsed: Date;
  fromLang: string;
  toLang: string;
  timer?: NodeJS.Timeout;
}

const engines = new Map<string, EngineInfo>();
const loadingPromises = new Map<string, Promise<TranslationEngine>>();

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
  toLang: string
): Promise<TranslationEngine> {
  const key = `${fromLang}-${toLang}`;

  const existing = engines.get(key);
  if (existing) {
    existing.lastUsed = new Date();
    resetIdleTimer(existing);
    return existing.engine;
  }

  // Check if initialization is already in progress
  if (loadingPromises.has(key)) {
    logger.debug(`Waiting for existing engine initialization for ${key}...`);
    return loadingPromises.get(key)!;
  }

  const initPromise = (async () => {
    try {
      const config = getConfig();

      logger.info(`Creating new engine for ${fromLang} -> ${toLang}`);

      if (!config.enableOfflineMode) {
        logger.info(`Downloading model for ${fromLang} -> ${toLang}`);
        await models.downloadModel(toLang, fromLang);
      }

      const modelFiles = await models.getModelFiles(config.modelDir, fromLang, toLang);
      const langPairDir = path.join(config.modelDir, `${fromLang}_${toLang}`);

      const engine = new TranslationEngine();
      const loader = createResourceLoader();

      logger.debug(`Loading WASM from: ${wasmPath}`);
      const wasmBinary = await readFile(wasmPath);
      logger.debug(`WASM loaded, size: ${wasmBinary.byteLength} bytes`);

      const bergamotModule = await loader.loadBergamotModule(wasmBinary, loadBergamot);

      const modelBuffers = await loader.loadModelFiles(langPairDir, {
        model: path.basename(modelFiles.model),
        lex: path.basename(modelFiles.lex),
        srcvocab: path.basename(modelFiles.vocab_src),
        trgvocab: path.basename(modelFiles.vocab_trg),
      });

      await engine.init(bergamotModule, modelBuffers);

      const info: EngineInfo = {
        engine,
        lastUsed: new Date(),
        fromLang,
        toLang,
      };

      resetIdleTimer(info);
      engines.set(key, info);

      logger.info(`Engine created successfully for ${fromLang} -> ${toLang}`);

      return engine;
    } finally {
      loadingPromises.delete(key);
    }
  })();

  loadingPromises.set(key, initPromise);
  return initPromise;
}

function resetIdleTimer(info: EngineInfo) {
  if (info.timer) {
    clearTimeout(info.timer);
  }

  const config = getConfig();
  const timeout = config.workerIdleTimeout * 1000;

  info.timer = setTimeout(() => {
    const key = `${info.fromLang}-${info.toLang}`;
    logger.info(`Engine ${key} idle timeout, stopping...`);
    info.engine.destroy();
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
  const cacheKey = [fromLang, toLang, text, isHTML];
  const cached = readCache(cacheKey);
  if (cached !== null) {
    logger.debug(`Cache hit: ${fromLang} -> ${toLang}, text length: ${text.length}`);
    return cached;
  }

  const MAX_RETRIES = 3;
  let lastError: any;

  for (let i = 0; i < MAX_RETRIES; i++) {
    try {
      const engine = await getOrCreateSingleEngine(fromLang, toLang);
      const result = await engine.translateAsync(text, { html: isHTML });
      writeCache(result, cacheKey);
      return result;
    } catch (error: any) {
      lastError = error;
      const isSIMDError = error.message && (
        error.message.includes('wasm-simd is not enabled') ||
        error.message.includes('SIMD') ||
        (error.message.includes('CompileError') && error.message.includes('WebAssembly'))
      );
      const isMemoryError = error.message && (
        error.message.includes('Out of bounds memory access') ||
        error.message.includes('memory access out of bounds') ||
        error.message.includes('unreachable') ||
        error.message.includes('abort')
      );

      if (isMemoryError) {
        logger.warn(`WASM memory error during translation (${fromLang}->${toLang}), retrying (${i + 1}/${MAX_RETRIES})...`);

        const key = `${fromLang}-${toLang}`;
        const info = engines.get(key);
        if (info) {
          logger.warn(`Destroying crashed engine: ${key}`);
          info.engine.destroy();
          engines.delete(key);
        }

        await new Promise(resolve => setTimeout(resolve, 100 * (i + 1)));
        continue;
      }

      if (isSIMDError) {
        logger.fatal('WebAssembly SIMD is not supported on this CPU.');
        logger.fatal('If you are using the standard build, please download and use the legacy build for non-AVX2 CPUs:');
        logger.fatal('  - Binary: mtranserver-*-legacy');
        logger.fatal('  - Docker: xxnuo/mtranserver:legacy');
        logger.fatal('If you are already using the legacy build, sorry your CPU is too old to run MTranServer.');
        process.exit(1);
      }
      throw error;
    }
  }

  logger.error(`Failed to translate after ${MAX_RETRIES} retries: ${lastError.message}`);
  throw lastError;
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

  if (fromLang !== 'auto' && fromLang === toLang) {
    return text;
  }

  if (fromLang !== 'auto' && text.length <= 128) {
    return translateSegment(fromLang, toLang, text, isHTML);
  }

  const config = getConfig();
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
      return text;
    }

    if (text.length > config.maxLengthBreak && !isHTML) {
      return translateLongText(effectiveFromLang, toLang, text);
    }

    return translateSegment(effectiveFromLang, toLang, text, isHTML);
  }

  logger.debug(`Detected ${segments.length} language segments`);
  let result = '';
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

  return result;
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

export function cleanupAllEngines() {
  logger.info(`Cleaning up ${engines.size} engine(s)...`);

  for (const [key, info] of engines.entries()) {
    if (info.timer) {
      clearTimeout(info.timer);
    }
    info.engine.destroy();
    logger.debug(`Stopped engine: ${key}`);
  }

  engines.clear();
  logger.info('All engines cleaned up successfully');
}
