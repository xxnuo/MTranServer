import crypto from 'crypto';
import fs from 'fs/promises';
import path from 'path';
import { createDownloader } from '@/core/factory.js';
import { getConfig } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import { ensureConverted, type SourceFiles } from '../converter/cache.js';
import { getLargestVersion } from '@/utils/version.js';

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
    pairs.add(`${record.sourceLanguage}_${record.targetLanguage}`);
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

let recordsLoading: Promise<void> | undefined;
export function initRecords(): Promise<void> {
  return recordsLoading ??= loadRecords().finally(() => { recordsLoading = undefined; });
}
async function loadRecords(): Promise<void> {
  const config = getConfig();
  const recordsPath = path.join(config.configDir, 'records.json');

  await fs.mkdir(config.configDir, { recursive: true });
  await fs.mkdir(config.modelDir, { recursive: true });

  if (config.enableOfflineMode) {
    logger.info('Offline mode enabled, records must be pre-downloaded');
    try {
      const data = await fs.readFile(recordsPath, 'utf-8');
      globalRecords = JSON.parse(data);
      if (!Array.isArray(globalRecords?.data)) throw new Error('Invalid cached model registry');
      if (globalRecords) {
        logger.debug(`Loaded ${globalRecords.data.length} model records`);
      }
    } catch (err) {
      throw new Error(`Failed to load records in offline mode: ${err}`);
    }
    return;
  }

  const downloadPath = recordsPath + '.' + crypto.randomUUID() + '.download';
  logger.info('Downloading latest records.json from remote...');
  try {
    const downloader = createDownloader();
    await downloader.download({
      url: RECORDS_URL,
      outputPath: downloadPath,
    });

    const data = await fs.readFile(downloadPath, 'utf-8');
    const records = JSON.parse(data);
    if (!Array.isArray(records.data)) throw new Error('Invalid model registry');
    await fs.rename(downloadPath, recordsPath);
    globalRecords = records;
    logger.debug(`Loaded ${records.data.length} model records`);
  } catch (err) {
    await fs.rm(downloadPath, { force: true });
    logger.warn(`Failed to download records.json: ${err}`);
    throw err;
  }
}

function selectRecords(fromLang: string, toLang: string, version?: string): RecordItem[] {
  const matchedRecords = globalRecords!.data.filter(
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

  return targetRecords;
}

async function downloadRawModel(
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

  const targetRecords = selectRecords(fromLang, toLang, version);

  const config = getConfig();
  const langPairDir = path.join(config.modelDir, `${fromLang}_${toLang}`);
  await fs.mkdir(langPairDir, { recursive: true });

  logger.info(`Downloading model files for ${fromLang} -> ${toLang}`);

  const downloader = createDownloader();

  for (const record of targetRecords) {
    const filename = record.attachment.filename;
    const fileUrl = `${ATTACHMENTS_BASE_URL}/${record.attachment.location}`;
    if (path.basename(filename) !== filename) throw new Error('Invalid model filename');
    const compressedPath = path.join(langPairDir, filename + '.download');
    const decompressedFilename = filename.replace(/\.zst$/, '');
    const decompressedPath = path.join(langPairDir, decompressedFilename);

    const receiptPath = decompressedPath + '.source.json';
    let needDownload = false;
    try {
      await fs.access(decompressedPath);
      if (record.decompressedHash) {
        const localHash = await computeFileHash(decompressedPath);
        if (localHash !== record.decompressedHash) {
          logger.info(`Model file ${decompressedFilename} hash mismatch, updating...`);
          needDownload = true;
        }
      } else {
        const receipt = JSON.parse(await fs.readFile(receiptPath, 'utf8'));
        needDownload = receipt.attachmentHash !== record.attachment.hash || receipt.localHash !== await computeFileHash(decompressedPath);
      }
    } catch {
      needDownload = true;
    }

    if (!needDownload) {
      logger.debug(`Model file up to date: ${decompressedFilename}`);
      continue;
    }

    logger.debug(`Downloading model file: ${filename} (type: ${record.fileType})`);
    const unpacked = decompressedPath + '.unpacked';
    try {
      await downloader.download({ url: fileUrl, outputPath: compressedPath });
      if (await computeFileHash(compressedPath) !== record.attachment.hash) throw new Error(`Download hash mismatch: ${filename}`);
      if (filename.endsWith('.zst')) await downloader.decompress(compressedPath, unpacked);
      else await fs.copyFile(compressedPath, unpacked);
      const localHash = await computeFileHash(unpacked);
      if (record.decompressedHash && localHash !== record.decompressedHash) throw new Error(`Decompressed hash mismatch: ${filename}`);
      await fs.rename(unpacked, decompressedPath);
      await fs.writeFile(receiptPath, JSON.stringify({ attachmentHash: record.attachment.hash, localHash }));
    } finally {
      await fs.rm(compressedPath, { force: true });
      await fs.rm(unpacked, { force: true });
    }
  }

  logger.info(`Model files downloaded successfully for ${fromLang} -> ${toLang}`);
}

export async function getModelFiles(
  modelDir: string,
  fromLang: string,
  toLang: string,
  version?: string
): Promise<SourceFiles> {
  if (!globalRecords) {
    await initRecords();
  }

  if (!globalRecords) {
    throw new Error('Records not initialized');
  }

  const records = selectRecords(fromLang, toLang, version);
  const files = resolveModelFiles(modelDir, fromLang, toLang, records);
  for (const file of new Set(Object.values(files))) await fs.access(file);
  return files;
}

function resolveModelFiles(modelDir: string, fromLang: string, toLang: string, records: RecordItem[]): SourceFiles {
  const langPairDir = path.join(modelDir, `${fromLang}_${toLang}`);
  const fileTypeMap = new Map<string, string>();

  for (const record of records) {
    if (record.sourceLanguage === fromLang && record.targetLanguage === toLang) {
      const filename = record.attachment.filename.replace(/\.zst$/, '');
      if (path.basename(filename) !== filename) throw new Error('Invalid model filename');
      fileTypeMap.set(record.fileType, path.join(langPairDir, filename));
    }
  }

  const files: Record<string, string> = {};

  const modelPath = fileTypeMap.get('model');
  if (!modelPath) {
    throw new Error(`Model file not found for ${fromLang} -> ${toLang}`);
  }
  files.model = modelPath;

  const lexPath = fileTypeMap.get('lex');
  if (lexPath) files.lex = lexPath;

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

  return { model: files.model, vocab_src: files.vocab_src, vocab_trg: files.vocab_trg, ...(files.lex ? { lex: files.lex } : {}) };
}

export async function prepareModel(toLang: string, fromLang: string, version?: string) {
  if (!globalRecords) await initRecords();
  const config = getConfig();
  const records = selectRecords(fromLang, toLang, version);
  const files = resolveModelFiles(config.modelDir, fromLang, toLang, records);
  const manifestRecords = records.map(record => ({ ...record, url: `${ATTACHMENTS_BASE_URL}/${record.attachment.location}` }));
  return ensureConverted(config.modelDir, `${fromLang}_${toLang}`, { files, records: manifestRecords }, async () => {
    if (!config.enableOfflineMode) await downloadRawModel(toLang, fromLang, version);
    const repaired = await getModelFiles(config.modelDir, fromLang, toLang, version);
    return { model: repaired.model, vocab_src: repaired.vocab_src, vocab_trg: repaired.vocab_trg, lex: repaired.lex };
  });
}

export async function downloadModel(toLang: string, fromLang: string, version?: string): Promise<void> {
  await prepareModel(toLang, fromLang, version);
}
