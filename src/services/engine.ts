import path from 'path';
import { TranslationEngine } from '../core/engine.js';
import { createResourceLoader } from '../core/factory.js';
import { getEmbeddedAssetPath } from '../assets/index.js';
import { getConfig } from '../config/index.js';
import * as logger from '../logger/index.js';
import * as models from '../models/index.js';

interface EngineInfo {
  engine: TranslationEngine;
  lastUsed: Date;
  fromLang: string;
  toLang: string;
  timer?: NodeJS.Timeout;
}

const engines = new Map<string, EngineInfo>();

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

  const wasmBinaryPath = getEmbeddedAssetPath('bergamot-translator.wasm');
  const workerScriptPath = getEmbeddedAssetPath('bergamot-translator.js');
  const wasmBinary = await loader.loadWasmBinary(wasmBinaryPath);
  const bergamotModule = await loader.loadBergamotModule(wasmBinary, workerScriptPath);

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
  const engine = await getOrCreateSingleEngine(fromLang, toLang);
  return engine.translate(text, { html: isHTML });
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

  if (fromLang === toLang) {
    return text;
  }

  return translateSegment(fromLang, toLang, text, isHTML);
}

export function cleanupAllEngines() {
  logger.info(`Cleaning up ${engines.size} engine(s)...`);

  for (const [key, info] of engines.entries()) {
    if (info.timer) {
      clearTimeout(info.timer);
    }
    logger.debug(`Stopped engine: ${key}`);
  }

  engines.clear();
  logger.info('All engines cleaned up successfully');
}
