import crypto from 'crypto';
import fs from 'fs/promises';
import path from 'path';
import { createDownloader } from '../core/factory.js';
import { getConfig } from '../config/index.js';
import * as logger from '../logger/index.js';
import { getLargestVersion } from '../utils/version.js';

const RECORDS_URL = 'https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/translations-models-v2/records';
const ATTACHMENTS_BASE_URL = 'https://firefox-settings-attachments.cdn.mozilla.net';

export interface Attachment {
  hash: string;
  size: number;
  filename: string;
  location: string;
  mimetype: string;
}

export interface RecordItem {
  name: string;
  schema: number;
  version: string;
  fileType: string;
  attachment: Attachment;
  architecture?: string;
  sourceLanguage: string;
  targetLanguage: string;
  decompressedHash?: string;
  decompressedSize?: number;
  filter_expression?: string;
  id: string;
  last_modified: number;
}

export interface RecordsData {
  data: RecordItem[];
}

export let globalRecords: RecordsData | null = null;

export function hasLanguagePair(fromLang: string, toLang: string): boolean {
  if (!globalRecords) return false;
  return globalRecords.data.some(
    r => r.sourceLanguage === fromLang && r.targetLanguage === toLang
  );
}

export function getLanguagePairs(): string[] {
  if (!globalRecords) return [];
  const pairs = new Set<string>();
  for (const record of globalRecords.data) {
    pairs.add(`${record.sourceLanguage}-${record.targetLanguage}`);
  }
  return Array.from(pairs);
}

export function getSupportedLanguages(): string[] {
  if (!globalRecords) return [];
  const langs = new Set<string>();
  for (const record of globalRecords.data) {
    langs.add(record.sourceLanguage);
    langs.add(record.targetLanguage);
  }
  return Array.from(langs);
}

function computeHash(data: Buffer): string {
  return crypto.createHash('sha256').update(data).digest('hex');
}

async function computeFileHash(filePath: string): Promise<string> {
  const data = await fs.readFile(filePath);
  return computeHash(data);
}

export async function initRecords(): Promise<void> {
  const config = getConfig();
  const recordsPath = path.join(config.configDir, 'records.json');

  await fs.mkdir(config.configDir, { recursive: true });
  await fs.mkdir(config.modelDir, { recursive: true });

  if (config.enableOfflineMode) {
    logger.info('Offline mode enabled, records must be pre-downloaded');
    try {
      const data = await fs.readFile(recordsPath, 'utf-8');
      globalRecords = JSON.parse(data);
      if (globalRecords) {
        logger.debug(`Loaded ${globalRecords.data.length} model records`);
      }
    } catch (err) {
      throw new Error(`Failed to load records in offline mode: ${err}`);
    }
    return;
  }

  logger.info('Downloading latest records.json from remote...');
  try {
    const downloader = createDownloader();
    await downloader.download({
      url: RECORDS_URL,
      outputPath: recordsPath,
    });

    const data = await fs.readFile(recordsPath, 'utf-8');
    globalRecords = JSON.parse(data);
    if (globalRecords) {
      logger.debug(`Loaded ${globalRecords.data.length} model records`);
    }
  } catch (err) {
    logger.warn(`Failed to download records.json: ${err}`);
    throw err;
  }
}

export async function downloadModel(
  toLang: string,
  fromLang: string,
  version?: string
): Promise<void> {
  if (!globalRecords) {
    await initRecords();
  }

  if (!globalRecords) {
    throw new Error('Records not initialized');
  }

  const matchedRecords = globalRecords.data.filter(
    r =>
      r.targetLanguage === toLang &&
      r.sourceLanguage === fromLang &&
      (!version || r.version === version)
  );

  if (matchedRecords.length === 0) {
    throw new Error(`No model found for ${fromLang} -> ${toLang}`);
  }

  let targetRecords = matchedRecords;
  if (!version) {
    const fileTypeMap = new Map<string, RecordItem[]>();
    for (const record of matchedRecords) {
      const arr = fileTypeMap.get(record.fileType) || [];
      arr.push(record);
      fileTypeMap.set(record.fileType, arr);
    }

    targetRecords = [];
    for (const records of fileTypeMap.values()) {
      const versions = records.map(r => r.version);
      const latest = getLargestVersion(versions);
      const latestRecord = records.find(r => r.version === latest);
      if (latestRecord) targetRecords.push(latestRecord);
    }
  }

  const config = getConfig();
  const langPairDir = path.join(config.modelDir, `${fromLang}_${toLang}`);
  await fs.mkdir(langPairDir, { recursive: true });

  logger.info(`Downloading model files for ${fromLang} -> ${toLang}`);

  const downloader = createDownloader();

  for (const record of targetRecords) {
    const filename = record.attachment.filename;
    const fileUrl = `${ATTACHMENTS_BASE_URL}/${record.attachment.location}`;
    const compressedPath = path.join(langPairDir, filename);
    const decompressedFilename = filename.replace(/\.zst$/, '');
    const decompressedPath = path.join(langPairDir, decompressedFilename);

    let needDownload = false;
    try {
      await fs.access(decompressedPath);
      if (record.decompressedHash) {
        const localHash = await computeFileHash(decompressedPath);
        if (localHash !== record.decompressedHash) {
          logger.info(`Model file ${decompressedFilename} hash mismatch, updating...`);
          needDownload = true;
        }
      }
    } catch {
      needDownload = true;
    }

    if (!needDownload) {
      logger.debug(`Model file up to date: ${decompressedFilename}`);
      continue;
    }

    logger.debug(`Downloading model file: ${filename} (type: ${record.fileType})`);
    await downloader.download({
      url: fileUrl,
      outputPath: compressedPath,
    });

    if (filename.endsWith('.zst')) {
      logger.debug(`Decompressing: ${filename} -> ${decompressedFilename}`);
      await downloader.decompress(compressedPath, decompressedPath);
      await fs.unlink(compressedPath);
    }
  }

  logger.info(`Model files downloaded successfully for ${fromLang} -> ${toLang}`);
}

export async function getModelFiles(
  modelDir: string,
  fromLang: string,
  toLang: string
): Promise<Record<string, string>> {
  if (!globalRecords) {
    await initRecords();
  }

  if (!globalRecords) {
    throw new Error('Records not initialized');
  }

  const langPairDir = path.join(modelDir, `${fromLang}_${toLang}`);
  const fileTypeMap = new Map<string, string>();

  for (const record of globalRecords.data) {
    if (record.sourceLanguage === fromLang && record.targetLanguage === toLang) {
      const filename = record.attachment.filename.replace(/\.zst$/, '');
      const fullPath = path.join(langPairDir, filename);

      try {
        await fs.access(fullPath);
        fileTypeMap.set(record.fileType, fullPath);
      } catch {
      }
    }
  }

  const files: Record<string, string> = {};

  const modelPath = fileTypeMap.get('model');
  if (!modelPath) {
    throw new Error(`Model file not found for ${fromLang} -> ${toLang}`);
  }
  files.model = modelPath;

  const lexPath = fileTypeMap.get('lex');
  if (!lexPath) {
    throw new Error(`Lex file not found for ${fromLang} -> ${toLang}`);
  }
  files.lex = lexPath;

  const vocabPath = fileTypeMap.get('vocab');
  if (vocabPath) {
    files.vocab_src = vocabPath;
    files.vocab_trg = vocabPath;
  } else {
    const srcVocabPath = fileTypeMap.get('srcvocab');
    if (!srcVocabPath) {
      throw new Error(`Source vocab file not found for ${fromLang} -> ${toLang}`);
    }
    files.vocab_src = srcVocabPath;

    const trgVocabPath = fileTypeMap.get('trgvocab');
    if (!trgVocabPath) {
      throw new Error(`Target vocab file not found for ${fromLang} -> ${toLang}`);
    }
    files.vocab_trg = trgVocabPath;
  }

  return files;
}
