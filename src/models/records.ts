import crypto from 'crypto';
import fsSync from 'fs';
import fs from 'fs/promises';
import path from 'path';
import { createDownloader } from '@/core/factory.js';
import { getConfig } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import { getLargestVersion } from '@/utils/version.js';

const RECORDS_URL = 'https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/translations-models-v2/records';
const ATTACHMENTS_BASE_URL = 'https://firefox-settings-attachments.cdn.mozilla.net';

function getMirrorBase(): string {
  return getConfig().modelMirrorUrl.trim().replace(/\/+$/, '');
}

function getRecordsUrls(): string[] {
  const config = getConfig();
  const mirrorBase = getMirrorBase();
  if (config.modelDownloadSource === 'official' || !mirrorBase) {
    return [RECORDS_URL];
  }

  // 下载站会固定托管 records.json，先用镜像能避免初始化阶段仍然绕回官方源。
  return [`${mirrorBase}/records.json`, RECORDS_URL];
}

function getAttachmentUrls(location: string): string[] {
  const officialUrl = `${ATTACHMENTS_BASE_URL}/${location.replace(/^\/+/, '')}`;
  const config = getConfig();
  const mirrorBase = getMirrorBase();
  if (config.modelDownloadSource === 'official' || !mirrorBase) {
    return [officialUrl];
  }

  const mirrorUrl = `${mirrorBase}/attachments/${location.replace(/^\/+/, '')}`;
  return [mirrorUrl, officialUrl];
}

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

export type ModelDownloadStage = 'checking' | 'downloading' | 'decompressing';

export interface ModelDownloadProgress {
  stage: ModelDownloadStage;
  fileType: string;
  filename: string;
  fileBytesDownloaded: number;
  fileBytesTotal: number;
  completedBytes: number;
  totalBytes: number;
}

export interface ModelSelection {
  records: RecordItem[];
  version: string;
  architecture?: string;
}

export let globalRecords: RecordsData | null = null;
export let recordsLoadedAt: number | null = null;

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

function getPreferredArchitecture(records: RecordItem[]): string | undefined {
  const architectures = Array.from(new Set(records.map(record => record.architecture).filter(Boolean))) as string[];
  const preferredOrder = ['base-memory', 'base', 'tiny'];
  return preferredOrder.find(architecture => architectures.includes(architecture)) || architectures[0];
}

function isSelectionInstalled(records: RecordItem[]): boolean {
  const first = records[0];
  if (!first) return false;

  const modelDir = getConfig().modelDir;
  const pairDir = path.join(modelDir, `${first.sourceLanguage}_${first.targetLanguage}`);
  return records.every(record => {
    const filename = record.attachment.filename.replace(/\.zst$/, '');
    return fsSync.existsSync(path.join(pairDir, filename));
  });
}

export function getModelSelection(
  fromLang: string,
  toLang: string,
  version?: string,
  architecture?: string
): ModelSelection {
  if (!globalRecords) {
    throw new Error('Records not initialized');
  }

  const matchedRecords = globalRecords.data.filter(record =>
    record.sourceLanguage === fromLang &&
    record.targetLanguage === toLang &&
    (!version || record.version === version) &&
    (!architecture || record.architecture === architecture)
  );

  if (matchedRecords.length === 0) {
    throw new Error(`No model found for ${fromLang} -> ${toLang}`);
  }

  let selectedArchitecture = architecture || getPreferredArchitecture(matchedRecords);
  if (!architecture) {
    const architectures = Array.from(new Set(matchedRecords.map(record => record.architecture).filter(Boolean))) as string[];
    const preferredOrder = ['base-memory', 'base', 'tiny'];
    const orderedArchitectures = [
      ...preferredOrder.filter(item => architectures.includes(item)),
      ...architectures.filter(item => !preferredOrder.includes(item)),
    ];

    for (const candidate of orderedArchitectures) {
      const candidateRecords = matchedRecords.filter(record => record.architecture === candidate);
      const candidateVersion = version || getLargestVersion(candidateRecords.map(record => record.version));
      const versionRecords = candidateRecords.filter(record => record.version === candidateVersion);
      if (isSelectionInstalled(versionRecords)) {
        selectedArchitecture = candidate;
        break;
      }
    }
  }

  const architectureRecords = selectedArchitecture
    ? matchedRecords.filter(record => record.architecture === selectedArchitecture)
    : matchedRecords;

  const selectedVersion = version || getLargestVersion(architectureRecords.map(record => record.version));
  const versionRecords = architectureRecords.filter(record => record.version === selectedVersion);

  const fileTypes = new Set(versionRecords.map(record => record.fileType));
  const requiredFileTypes = ['model', 'lex'];
  const hasVocab = fileTypes.has('vocab') || (fileTypes.has('srcvocab') && fileTypes.has('trgvocab'));
  if (!requiredFileTypes.every(fileType => fileTypes.has(fileType)) || !hasVocab) {
    throw new Error(`Incomplete model records for ${fromLang} -> ${toLang}`);
  }

  return {
    records: versionRecords,
    version: selectedVersion,
    architecture: selectedArchitecture,
  };
}

export function getModelSelections(): ModelSelection[] {
  if (!globalRecords) return [];

  const groups = new Map<string, RecordItem[]>();
  for (const record of globalRecords.data) {
    const architecture = record.architecture || 'unknown';
    const key = `${record.sourceLanguage}\u0000${record.targetLanguage}\u0000${architecture}`;
    const records = groups.get(key) || [];
    records.push(record);
    groups.set(key, records);
  }

  const selections: ModelSelection[] = [];
  for (const records of groups.values()) {
    const first = records[0];
    if (!first) continue;
    try {
      selections.push(getModelSelection(
        first.sourceLanguage,
        first.targetLanguage,
        undefined,
        first.architecture
      ));
    } catch {
      // Ignore incomplete records so one malformed remote entry does not hide other models.
    }
  }
  return selections;
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
      recordsLoadedAt = Date.now();
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
      url: getRecordsUrls()[0],
      outputPath: recordsPath,
      urls: getRecordsUrls().slice(1),
      proxy: config.downloadProxy || undefined,
    });

    const data = await fs.readFile(recordsPath, 'utf-8');
    globalRecords = JSON.parse(data);
    recordsLoadedAt = Date.now();
    if (globalRecords) {
      logger.debug(`Loaded ${globalRecords.data.length} model records`);
    }
  } catch (err) {
    logger.warn(`Failed to download records.json: ${err}`);
    throw err;
  }
}

export async function refreshRecords(): Promise<void> {
  const config = getConfig();
  if (config.enableOfflineMode) {
    throw new Error('Cannot refresh model records while offline mode is enabled');
  }

  await initRecords();
}

export async function downloadModel(
  toLang: string,
  fromLang: string,
  version?: string,
  architecture?: string,
  onProgress?: (progress: ModelDownloadProgress) => void,
  signal?: AbortSignal,
  onFallback?: (failedUrl: string, nextUrl: string) => void
): Promise<void> {
  if (!globalRecords) {
    await initRecords();
  }

  if (!globalRecords) {
    throw new Error('Records not initialized');
  }

  const selection = getModelSelection(fromLang, toLang, version, architecture);
  const targetRecords = selection.records;
  const totalBytes = targetRecords.reduce((total, record) => total + record.attachment.size, 0);
  let completedBytes = 0;

  const config = getConfig();
  const langPairDir = path.join(config.modelDir, `${fromLang}_${toLang}`);
  await fs.mkdir(langPairDir, { recursive: true });

  logger.info(`Downloading model files for ${fromLang} -> ${toLang}`);

  const downloader = createDownloader();

  for (const record of targetRecords) {
    const filename = record.attachment.filename;
    const downloadUrls = getAttachmentUrls(record.attachment.location);
    const fileUrl = downloadUrls[0];
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
      onProgress?.({
        stage: 'checking',
        fileType: record.fileType,
        filename: decompressedFilename,
        fileBytesDownloaded: record.attachment.size,
        fileBytesTotal: record.attachment.size,
        completedBytes: completedBytes + record.attachment.size,
        totalBytes,
      });
      completedBytes += record.attachment.size;
      continue;
    }

    logger.debug(`Downloading model file: ${filename} (type: ${record.fileType})`);
    await downloader.download({
      url: fileUrl,
      outputPath: compressedPath,
      urls: downloadUrls.slice(1),
      hash: record.attachment.hash,
      proxy: config.downloadProxy || undefined,
      signal,
      onFallback,
      onProgress: ({ downloadedBytes, totalBytes: fileBytesTotal }) => {
        onProgress?.({
          stage: 'downloading',
          fileType: record.fileType,
          filename,
          fileBytesDownloaded: downloadedBytes,
          fileBytesTotal: fileBytesTotal || record.attachment.size,
          completedBytes,
          totalBytes,
        });
      },
    });

    completedBytes += record.attachment.size;

    if (filename.endsWith('.zst')) {
      logger.debug(`Decompressing: ${filename} -> ${decompressedFilename}`);
      onProgress?.({
        stage: 'decompressing',
        fileType: record.fileType,
        filename: decompressedFilename,
        fileBytesDownloaded: record.attachment.size,
        fileBytesTotal: record.attachment.size,
        completedBytes,
        totalBytes,
      });
      await downloader.decompress(compressedPath, decompressedPath);
      if (record.decompressedHash && !(await downloader.verifyHash(decompressedPath, record.decompressedHash))) {
        throw new Error(`Decompressed file hash mismatch: ${decompressedFilename}`);
      }
      await fs.unlink(compressedPath);
    } else if (record.decompressedHash && !(await downloader.verifyHash(compressedPath, record.decompressedHash))) {
      throw new Error(`Downloaded file hash mismatch: ${filename}`);
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

  const selection = getModelSelection(fromLang, toLang);
  const langPairDir = path.join(modelDir, `${fromLang}_${toLang}`);
  const fileTypeMap = new Map<string, string>();

  for (const record of selection.records) {
    const filename = record.attachment.filename.replace(/\.zst$/, '');
    const fullPath = path.join(langPairDir, filename);

    try {
      await fs.access(fullPath);
      fileTypeMap.set(record.fileType, fullPath);
    } catch {
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
