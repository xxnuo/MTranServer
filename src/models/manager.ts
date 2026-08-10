import crypto from 'crypto';
import fs from 'fs/promises';
import path from 'path';
import { getConfig, saveConfigFile, setConfig } from '@/config/index.js';
import { createDownloader, DownloadCancelledError } from '@/core/factory.js';
import * as logger from '@/logger/index.js';
import {
  globalRecords,
  downloadModel,
  getModelSelection,
  getModelSelections,
  initRecords,
  refreshRecords,
  recordsLoadedAt,
} from './records.js';
import type { ModelDownloadProgress, ModelSelection } from './records.js';

const LANGUAGE_CODE_PATTERN = /^[A-Za-z0-9-]+$/;
const MAX_DOWNLOAD_HISTORY = 100;
const MAX_ACTIVE_DOWNLOADS = 2;
const DOWNLOAD_STATE_FILENAME = 'downloads.json';

export type ModelStatus = 'available' | 'installed' | 'downloading' | 'decompressing' | 'failed';
export type DownloadJobStatus = 'queued' | 'checking' | 'downloading' | 'decompressing' | 'completed' | 'failed' | 'cancelled';
export type ModelDownloadSource = 'mirror' | 'official';
export type DownloadErrorCode = 'network' | 'hash_mismatch' | 'decompression' | 'cancelled' | 'unknown';

export class ModelManagerError extends Error {
  constructor(
    public readonly status: number,
    public readonly code: string,
    message: string
  ) {
    super(message);
    this.name = 'ModelManagerError';
  }
}

export interface ModelFileState {
  fileType: string;
  filename: string;
  compressedSize: number;
  decompressedSize: number;
  installedSize: number;
  installed: boolean;
}

export interface ManagedModel {
  id: string;
  from: string;
  to: string;
  architecture: string;
  version: string;
  status: ModelStatus;
  downloadSize: number;
  installedSize: number;
  progress: number;
  downloadId?: string;
  error?: string;
  errorCode?: DownloadErrorCode;
  fallbackUsed?: boolean;
  files: ModelFileState[];
}

export interface ModelCatalog {
  modelDir: string;
  recordsLoadedAt: string | null;
  models: ManagedModel[];
  totalModels: number;
  filteredModels: number;
  page: number;
  pageSize: number;
  totalPages: number;
  architectures: string[];
  statusCounts: Record<ModelStatus, number>;
}

export interface ModelDownloadSettings {
  source: ModelDownloadSource;
  mirrorUrl: string;
  proxyUrl: string;
  fallbackToOfficial: boolean;
}

export interface DownloadSpeedTestItem {
  source: ModelDownloadSource;
  url: string;
  status: 'ok' | 'failed';
  latencyMs: number | null;
  speedMbps: number | null;
  downloadedBytes: number;
  durationMs: number | null;
  error?: string;
}

export interface DownloadSpeedTest {
  testedAt: string;
  viaProxy: boolean;
  mirror: DownloadSpeedTestItem;
  official: DownloadSpeedTestItem;
}

export interface DownloadLatencyTestItem {
  source: ModelDownloadSource;
  url: string;
  status: 'ok' | 'failed';
  latencyMs: number | null;
  error?: string;
}

export interface DownloadLatencyTest {
  testedAt: string;
  viaProxy: boolean;
  mirror: DownloadLatencyTestItem;
  official: DownloadLatencyTestItem;
}

export interface OcrModelFile {
  role: string;
  path: string;
  url: string;
  sizeBytes: number;
  sha256: string;
  available: boolean;
}

export interface OcrModelSummary {
  id: string;
  name: string;
  version: string;
  variant: string;
  backend: string;
  device: string;
  languages: string[];
  recommended: boolean;
  description: Record<string, string>;
  sizeBytes: number;
  available: boolean;
  files: OcrModelFile[];
  status: ModelStatus;
  progress: number;
  downloadedBytes: number;
  downloadId?: string;
  error?: string;
  errorCode?: DownloadErrorCode;
}

export interface OcrModelCatalog {
  schema: number;
  updatedAt: string;
  models: OcrModelSummary[];
}

export interface DownloadJob {
  kind: 'translation' | 'ocr';
  id: string;
  from: string;
  to: string;
  architecture: string;
  version: string;
  source: ModelDownloadSource;
  status: DownloadJobStatus;
  progress: number;
  downloadedBytes: number;
  totalBytes: number;
  currentFile?: string;
  error?: string;
  errorCode?: DownloadErrorCode;
  fallbackUsed?: boolean;
  downloadRateMbps?: number;
  createdAt: string;
  startedAt?: string;
  finishedAt?: string;
  modelId?: string;
}

interface DownloadJobState extends DownloadJob {
  key: string;
  controller: AbortController;
  lastProgressAt?: number;
  lastProgressBytes?: number;
}

interface PersistedTranslationDownload {
  kind: 'translation';
  from: string;
  to: string;
  version?: string;
  architecture?: string;
}

interface PersistedOcrDownload {
  kind: 'ocr';
  modelId: string;
}

type PersistedDownload = PersistedTranslationDownload | PersistedOcrDownload;

const jobs = new Map<string, DownloadJobState>();
const activeJobs = new Map<string, string>();
const ocrJobs = new Map<string, DownloadJobState>();
const activeOcrJobs = new Map<string, string>();
const ocrHashCache = new Map<string, { size: number; mtimeMs: number; valid: boolean }>();
let modelCatalogCache: { recordsLoadedAt: number | null; cachedAt: number; models: ManagedModel[] } | null = null;
let persistQueue: Promise<void> = Promise.resolve();
let resumePromise: Promise<void> | null = null;

function validateUrl(value: string, field: string, protocols: string[]): string {
  if (!value) return '';
  if (value.length > 2048) {
    throw new ModelManagerError(400, 'INVALID_URL', `${field} URL is too long`);
  }
  let parsed: URL;
  try {
    parsed = new URL(value);
  } catch {
    throw new ModelManagerError(400, 'INVALID_URL', `Invalid ${field} URL`);
  }
  if (!protocols.includes(parsed.protocol)) {
    throw new ModelManagerError(400, 'INVALID_URL', `Unsupported ${field} protocol`);
  }
  return value.trim().replace(/\/+$/, '');
}

function normalizeProxyUrl(value: string): string {
  return value.replace(/^s5:/i, 'socks5:');
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function requiredString(value: unknown, field: string): string {
  if (typeof value !== 'string' || !value.trim()) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return value;
}

function requiredOcrModelId(value: unknown, field: string): string {
  const id = requiredString(value, field);
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(id)) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return id;
}

function requiredOcrRelativePath(value: unknown, field: string): string {
  const rawPath = requiredString(value, field).replace(/\\/g, '/');
  const normalizedPath = path.posix.normalize(rawPath);
  if (path.posix.isAbsolute(rawPath) || normalizedPath === '.' || normalizedPath === '..' || normalizedPath.startsWith('../') || rawPath.includes('\u0000')) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return normalizedPath;
}

function requiredOcrFileUrl(value: unknown, field: string, mirrorUrl: string): string {
  const rawUrl = requiredString(value, field);
  let resolved: URL;
  try {
    resolved = new URL(rawUrl, `${mirrorUrl}/`);
  } catch {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  if (resolved.origin !== new URL(mirrorUrl).origin || !['http:', 'https:'].includes(resolved.protocol)) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return resolved.toString();
}

function requiredSha256(value: unknown, field: string): string {
  const sha256 = requiredString(value, field).toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(sha256)) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return sha256;
}

function requiredNonNegativeNumber(value: unknown, field: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog field is invalid: ${field}`);
  }
  return value;
}

function parseOcrCatalog(payload: unknown, mirrorUrl: string): OcrModelCatalog {
  if (!isRecord(payload) || !Array.isArray(payload.models)) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', 'OCR catalog response is invalid');
  }

  const models = payload.models.map((rawModel, modelIndex): OcrModelSummary => {
    if (!isRecord(rawModel) || !Array.isArray(rawModel.files) || !Array.isArray(rawModel.languages)) {
      throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog model ${modelIndex} is invalid`);
    }

    const rawDescription = rawModel.description;
    const description: Record<string, string> = {};
    if (isRecord(rawDescription)) {
      for (const [language, value] of Object.entries(rawDescription)) {
        if (typeof value === 'string') description[language] = value;
      }
    }

    const files = rawModel.files.map((rawFile, fileIndex): OcrModelFile => {
      if (!isRecord(rawFile)) {
        throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR catalog file ${modelIndex}:${fileIndex} is invalid`);
      }
      return {
        role: requiredString(rawFile.role, `models[${modelIndex}].files[${fileIndex}].role`),
        path: requiredOcrRelativePath(rawFile.path, `models[${modelIndex}].files[${fileIndex}].path`),
        url: requiredOcrFileUrl(rawFile.url, `models[${modelIndex}].files[${fileIndex}].url`, mirrorUrl),
        sizeBytes: requiredNonNegativeNumber(rawFile.sizeBytes, `models[${modelIndex}].files[${fileIndex}].sizeBytes`),
        sha256: requiredSha256(rawFile.sha256, `models[${modelIndex}].files[${fileIndex}].sha256`),
        available: rawFile.available === true,
      };
    });

    return {
      id: requiredOcrModelId(rawModel.id, `models[${modelIndex}].id`),
      name: requiredString(rawModel.name, `models[${modelIndex}].name`),
      version: requiredString(rawModel.version, `models[${modelIndex}].version`),
      variant: requiredString(rawModel.variant, `models[${modelIndex}].variant`),
      backend: requiredString(rawModel.backend, `models[${modelIndex}].backend`),
      device: requiredString(rawModel.device, `models[${modelIndex}].device`),
      languages: rawModel.languages.map((language, languageIndex) => requiredString(language, `models[${modelIndex}].languages[${languageIndex}]`)),
      recommended: rawModel.recommended === true,
      description,
      sizeBytes: requiredNonNegativeNumber(rawModel.sizeBytes, `models[${modelIndex}].sizeBytes`),
      available: rawModel.available === true,
      files,
      status: 'available',
      progress: 0,
      downloadedBytes: 0,
    };
  });

  return {
    schema: typeof payload.schema === 'number' ? payload.schema : 1,
    updatedAt: requiredString(payload.updatedAt, 'updatedAt'),
    models,
  };
}

export async function getOcrModelCatalog(): Promise<OcrModelCatalog> {
  const config = getConfig();
  const mirrorUrl = validateUrl(config.modelMirrorUrl.trim(), 'mirror', ['http:', 'https:']);
  if (!mirrorUrl) {
    throw new ModelManagerError(409, 'OCR_CATALOG_UNAVAILABLE', 'OCR model mirror URL is not configured');
  }

  try {
    const payload = await createDownloader(30_000).fetchJson<unknown>(`${mirrorUrl}/ocr/models`, config.downloadProxy || undefined);
    const catalog = parseOcrCatalog(payload, mirrorUrl);
    return {
      ...catalog,
      models: await Promise.all(catalog.models.map(inspectOcrModel)),
    };
  } catch (error) {
    if (error instanceof ModelManagerError) throw error;
    throw new ModelManagerError(
      502,
      'OCR_CATALOG_UNAVAILABLE',
      `Failed to load OCR model catalog: ${error instanceof Error ? error.message : String(error)}`
    );
  }
}

export function getModelDownloadSettings(): ModelDownloadSettings {
  const config = getConfig();
  return {
    source: config.modelDownloadSource,
    mirrorUrl: config.modelMirrorUrl,
    proxyUrl: config.downloadProxy,
    fallbackToOfficial: config.modelDownloadSource === 'mirror',
  };
}

async function measureDownloadSource(
  source: ModelDownloadSource,
  url: string,
  proxy: string
): Promise<DownloadSpeedTestItem> {
  if (!url) {
    return {
      source,
      url,
      status: 'failed',
      latencyMs: null,
      speedMbps: null,
      downloadedBytes: 0,
      durationMs: null,
      error: 'Download source URL is not configured',
    };
  }

  try {
    const result = await createDownloader(30_000).measureSpeed({
      url,
      proxy: proxy || undefined,
      maxBytes: 256 * 1024,
    });
    return { source, url, status: 'ok', ...result };
  } catch (error) {
    return {
      source,
      url,
      status: 'failed',
      latencyMs: null,
      speedMbps: null,
      downloadedBytes: 0,
      durationMs: null,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

async function measureLatencySource(
  source: ModelDownloadSource,
  url: string,
  proxy: string
): Promise<DownloadLatencyTestItem> {
  if (!url) {
    return { source, url, status: 'failed', latencyMs: null, error: 'Download source URL is not configured' };
  }

  try {
    const latencyMs = await createDownloader(30_000).measureLatency({ url, proxy: proxy || undefined });
    return { source, url, status: 'ok', latencyMs };
  } catch (error) {
    return {
      source,
      url,
      status: 'failed',
      latencyMs: null,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

async function getDownloadTestContext(overrides?: { mirrorUrl?: string; proxyUrl?: string }) {
  if (!globalRecords) {
    await initRecords();
  }

  const config = getConfig();
  const mirrorValue = overrides?.mirrorUrl ?? config.modelMirrorUrl;
  const proxyValue = overrides?.proxyUrl === undefined
    ? config.downloadProxy
    : validateUrl(normalizeProxyUrl(overrides.proxyUrl.trim()), 'proxy', [
      'http:',
      'https:',
      'socks:',
      'socks4:',
      'socks4a:',
      'socks5:',
      'socks5h:',
    ]);
  const mirrorBase = validateUrl(mirrorValue.trim(), 'mirror', ['http:', 'https:']).replace(/\/+$/, '');
  const firstAttachment = globalRecords?.data.find(record => record.attachment.location)?.attachment;
  const officialUrl = firstAttachment
    ? `https://firefox-settings-attachments.cdn.mozilla.net/${firstAttachment.location.replace(/^\/+/, '')}`
    : '';
  return {
    proxy: proxyValue,
    mirrorUrl: mirrorBase ? `${mirrorBase}/speedtest` : '',
    officialUrl,
  };
}

export async function testDownloadSources(overrides?: { mirrorUrl?: string; proxyUrl?: string }): Promise<DownloadSpeedTest> {
  const context = await getDownloadTestContext(overrides);
  const [mirror, official] = await Promise.all([
    measureDownloadSource('mirror', context.mirrorUrl, context.proxy),
    measureDownloadSource('official', context.officialUrl, context.proxy),
  ]);

  return {
    testedAt: new Date().toISOString(),
    viaProxy: Boolean(context.proxy),
    mirror,
    official,
  };
}

export async function testDownloadLatencies(overrides?: { mirrorUrl?: string; proxyUrl?: string }): Promise<DownloadLatencyTest> {
  const context = await getDownloadTestContext(overrides);
  const [mirror, official] = await Promise.all([
    measureLatencySource('mirror', context.mirrorUrl, context.proxy),
    measureLatencySource('official', context.officialUrl, context.proxy),
  ]);
  return {
    testedAt: new Date().toISOString(),
    viaProxy: Boolean(context.proxy),
    mirror,
    official,
  };
}

export function updateModelDownloadSettings(input: Partial<ModelDownloadSettings>): ModelDownloadSettings {
  const source = input.source === 'official' ? 'official' : input.source === 'mirror' ? 'mirror' : null;
  if (!source) {
    throw new ModelManagerError(400, 'INVALID_DOWNLOAD_SOURCE', 'Download source must be mirror or official');
  }

  const mirrorUrl = validateUrl(input.mirrorUrl?.trim() || '', 'mirror', ['http:', 'https:']);
  const proxyUrl = validateUrl(normalizeProxyUrl(input.proxyUrl?.trim() || ''), 'proxy', [
    'http:',
    'https:',
    'socks:',
    'socks4:',
    'socks4a:',
    'socks5:',
    'socks5h:',
  ]);
  if (source === 'mirror' && !mirrorUrl) {
    throw new ModelManagerError(400, 'INVALID_MIRROR_URL', 'Mirror URL is required');
  }

  setConfig({
    modelDownloadSource: source,
    modelMirrorUrl: mirrorUrl,
    downloadProxy: proxyUrl,
  });
  saveConfigFile({
    modelDownloadSource: source,
    modelMirrorUrl: mirrorUrl,
    downloadProxy: proxyUrl,
  });
  return getModelDownloadSettings();
}

function assertLanguageCode(code: string, field: string): string {
  if (!code || !LANGUAGE_CODE_PATTERN.test(code)) {
    throw new ModelManagerError(400, 'INVALID_LANGUAGE_CODE', `Invalid ${field} language code`);
  }
  return code;
}

function modelKey(from: string, to: string, architecture: string, version: string): string {
  return `${from}\u0000${to}\u0000${architecture}\u0000${version}`;
}

function modelId(selection: ModelSelection): string {
  const first = selection.records[0];
  if (!first) throw new Error('Model selection is empty');
  return `${first.sourceLanguage}_${first.targetLanguage}_${selection.architecture || 'unknown'}`;
}

function modelDirectory(from: string, to: string): string {
  const config = getConfig();
  return path.join(config.modelDir, `${from}_${to}`);
}

function ocrModelDirectory(modelId: string): string {
  return path.join(getConfig().modelDir, 'ocr', modelId);
}

function ocrFilePath(model: OcrModelSummary, file: OcrModelFile): string {
  const root = path.resolve(ocrModelDirectory(model.id));
  const target = path.resolve(root, ...file.path.split('/'));
  if (target !== root && !target.startsWith(`${root}${path.sep}`)) {
    throw new ModelManagerError(502, 'INVALID_OCR_CATALOG', `OCR model file path escapes model directory: ${file.path}`);
  }
  return target;
}

async function verifyOcrFile(model: OcrModelSummary, file: OcrModelFile): Promise<boolean> {
  const filePath = ocrFilePath(model, file);
  try {
    const stat = await fs.stat(filePath);
    if (stat.size !== file.sizeBytes) {
      ocrHashCache.delete(filePath);
      return false;
    }

    const cached = ocrHashCache.get(filePath);
    if (cached && cached.size === stat.size && cached.mtimeMs === stat.mtimeMs) return cached.valid;

    const valid = await createDownloader().verifyHash(filePath, file.sha256);
    ocrHashCache.set(filePath, { size: stat.size, mtimeMs: stat.mtimeMs, valid });
    return valid;
  } catch {
    ocrHashCache.delete(filePath);
    return false;
  }
}

async function inspectOcrModel(model: OcrModelSummary): Promise<OcrModelSummary> {
  const installedFiles = await Promise.all(model.files.map(file => verifyOcrFile(model, file)));
  const installed = model.files.length > 0 && installedFiles.every(Boolean);
  const activeJobId = activeOcrJobs.get(model.id);
  const activeJob = activeJobId ? ocrJobs.get(activeJobId) : undefined;
  const latestJob = activeJob || getLatestOcrJob(model.id);
  let status: ModelStatus = installed ? 'installed' : 'available';
  if (activeJob) {
    status = 'downloading';
  } else if (latestJob?.status === 'failed') {
    status = 'failed';
  }

  return {
    ...model,
    status,
    progress: activeJob?.progress ?? (installed ? 100 : 0),
    downloadedBytes: activeJob?.downloadedBytes ?? (installed ? model.sizeBytes : 0),
    downloadId: latestJob?.id,
    error: latestJob?.error,
    errorCode: latestJob?.errorCode,
  };
}

async function inspectSelection(selection: ModelSelection): Promise<ManagedModel> {
  const first = selection.records[0];
  if (!first) throw new Error('Model selection is empty');

  const files: ModelFileState[] = [];
  for (const record of selection.records) {
    const filename = record.attachment.filename.replace(/\.zst$/, '');
    const filePath = path.join(modelDirectory(first.sourceLanguage, first.targetLanguage), filename);
    let installedSize = 0;
    let installed = false;

    try {
      const stat = await fs.stat(filePath);
      installedSize = stat.size;
      installed = record.decompressedSize === undefined || stat.size === record.decompressedSize;
    } catch {
      // A missing file is the normal state before the first download.
    }

    files.push({
      fileType: record.fileType,
      filename,
      compressedSize: record.attachment.size,
      decompressedSize: record.decompressedSize || 0,
      installedSize,
      installed,
    });
  }

  const downloadSize = files.reduce((total, file) => total + file.compressedSize, 0);
  const installedSize = files.reduce((total, file) => total + file.installedSize, 0);
  const key = modelKey(
    first.sourceLanguage,
    first.targetLanguage,
    selection.architecture || 'unknown',
    selection.version
  );
  const activeJobId = activeJobs.get(key);
  const activeJob = activeJobId ? jobs.get(activeJobId) : undefined;
  const latestJob = activeJob || getLatestJob(key);
  const installed = files.length > 0 && files.every(file => file.installed);

  let status: ModelStatus = installed ? 'installed' : 'available';
  if (activeJob) {
    status = activeJob.status === 'decompressing' ? 'decompressing' : 'downloading';
  } else if (latestJob?.status === 'failed') {
    status = 'failed';
  }

  return {
    id: modelId(selection),
    from: first.sourceLanguage,
    to: first.targetLanguage,
    architecture: selection.architecture || 'unknown',
    version: selection.version,
    status,
    downloadSize,
    installedSize,
    progress: latestJob?.progress ?? (installed ? 100 : 0),
    downloadId: latestJob?.id,
    error: latestJob?.error,
    errorCode: latestJob?.errorCode,
    fallbackUsed: latestJob?.fallbackUsed,
    files,
  };
}

function trimJobHistory() {
  while (jobs.size > MAX_DOWNLOAD_HISTORY) {
    const oldest = jobs.keys().next().value;
    if (!oldest) return;
    if (Array.from(activeJobs.values()).includes(oldest)) return;
    jobs.delete(oldest);
  }
}

function trimOcrJobHistory() {
  while (ocrJobs.size > MAX_DOWNLOAD_HISTORY) {
    const oldest = ocrJobs.keys().next().value;
    if (!oldest) return;
    if (Array.from(activeOcrJobs.values()).includes(oldest)) return;
    ocrJobs.delete(oldest);
  }
}

function getDownloadStatePath(): string {
  return path.join(getConfig().configDir, DOWNLOAD_STATE_FILENAME);
}

function persistActiveDownloads(): Promise<void> {
  const pending: PersistedDownload[] = [
    ...Array.from(activeJobs.values())
    .map(id => jobs.get(id))
    .filter((job): job is DownloadJobState => Boolean(job))
    .map(job => ({
      kind: 'translation' as const,
      from: job.from,
      to: job.to,
      version: job.version,
      architecture: job.architecture,
    })),
    ...Array.from(activeOcrJobs.values())
      .map(id => ocrJobs.get(id))
      .filter((job): job is DownloadJobState => Boolean(job && job.modelId))
      .map(job => ({ kind: 'ocr' as const, modelId: job.modelId as string })),
  ];

  persistQueue = persistQueue.then(async () => {
    const statePath = getDownloadStatePath();
    const temporaryPath = `${statePath}.part`;
    await fs.mkdir(path.dirname(statePath), { recursive: true });
    await fs.writeFile(temporaryPath, JSON.stringify(pending, null, 2), 'utf8');
    await fs.rm(statePath, { force: true });
    await fs.rename(temporaryPath, statePath);
  });
  return persistQueue;
}

function queuePersistActiveDownloads(): void {
  void persistActiveDownloads().catch(error => {
    logger.warn(`Failed to persist download state: ${error instanceof Error ? error.message : String(error)}`);
  });
}

async function readPersistedDownloads(): Promise<PersistedDownload[]> {
  try {
    const value = JSON.parse(await fs.readFile(getDownloadStatePath(), 'utf8'));
    if (!Array.isArray(value)) return [];
    return value.flatMap((item): PersistedDownload[] => {
      if (!item || typeof item !== 'object') return [];
      if (item.kind === 'ocr' && typeof item.modelId === 'string') {
        return [{ kind: 'ocr' as const, modelId: item.modelId }];
      }
      if (typeof item.from === 'string' && typeof item.to === 'string') {
        return [{
          kind: 'translation' as const,
          from: item.from,
          to: item.to,
          ...(typeof item.version === 'string' ? { version: item.version } : {}),
          ...(typeof item.architecture === 'string' ? { architecture: item.architecture } : {}),
        }];
      }
      return [];
    });
  } catch {
    return [];
  }
}

function toPublicJob(job: DownloadJobState): DownloadJob {
  const {
    key: _key,
    controller: _controller,
    lastProgressAt: _lastProgressAt,
    lastProgressBytes: _lastProgressBytes,
    ...publicJob
  } = job;
  return publicJob;
}

function getLatestJob(key: string): DownloadJobState | undefined {
  const history = Array.from(jobs.values()).reverse();
  return history.find(job => job.key === key);
}

function getLatestOcrJob(modelId: string): DownloadJobState | undefined {
  const history = Array.from(ocrJobs.values()).reverse();
  return history.find(job => job.modelId === modelId);
}

async function ensureRecords() {
  if (!getModelSelections().length) {
    await initRecords();
  }
}

async function getInspectedModels(): Promise<ManagedModel[]> {
  await ensureRecords();

  const now = Date.now();
  if (modelCatalogCache && modelCatalogCache.recordsLoadedAt === recordsLoadedAt && now - modelCatalogCache.cachedAt < 1_000) {
    return modelCatalogCache.models;
  }

  const selections = getModelSelections();
  const models = await Promise.all(selections.map(inspectSelection));

  models.sort((a, b) => {
    const pairCompare = `${a.from}_${a.to}`.localeCompare(`${b.from}_${b.to}`);
    if (pairCompare !== 0) return pairCompare;
    return a.architecture.localeCompare(b.architecture);
  });

  modelCatalogCache = { recordsLoadedAt, cachedAt: Date.now(), models };
  return models;
}

interface ModelCatalogQuery {
  page?: number;
  pageSize?: number;
  query?: string;
  status?: string;
  architecture?: string;
  locale?: string;
}

export async function getModelCatalog(options: ModelCatalogQuery = {}): Promise<ModelCatalog> {
  const models = await getInspectedModels();
  const requestedPage = Number.isFinite(options.page) ? Math.max(1, Math.floor(options.page as number)) : 1;
  const pageSize = Number.isFinite(options.pageSize)
    ? Math.min(48, Math.max(1, Math.floor(options.pageSize as number)))
    : 24;
  const normalizedQuery = options.query?.trim().toLocaleLowerCase() || '';
  let displayNames: Intl.DisplayNames | null = null;
  if (options.locale?.trim()) {
    try {
      displayNames = new Intl.DisplayNames([options.locale.trim()], { type: 'language' });
    } catch {
      displayNames = null;
    }
  }
  const status = ['available', 'installed', 'downloading', 'decompressing', 'failed'].includes(options.status || '')
    ? options.status
    : undefined;
  const architecture = options.architecture?.trim() && options.architecture !== 'all'
    ? options.architecture.trim()
    : undefined;
  const statusCounts: Record<ModelStatus, number> = {
    available: 0,
    installed: 0,
    downloading: 0,
    decompressing: 0,
    failed: 0,
  };
  const architectures = new Set<string>();

  for (const model of models) {
    statusCounts[model.status] += 1;
    architectures.add(model.architecture);
  }

  const filteredModels = models.filter(model => {
    if (status && model.status !== status) return false;
    if (architecture && model.architecture !== architecture) return false;
    if (!normalizedQuery) return true;
    const sourceName = displayNames?.of(model.from) || '';
    const targetName = displayNames?.of(model.to) || '';
    const searchable = `${model.id} ${model.from} ${model.to} ${sourceName} ${targetName} ${model.architecture} ${model.version}`.toLocaleLowerCase();
    return searchable.includes(normalizedQuery);
  });
  const totalPages = Math.max(1, Math.ceil(filteredModels.length / pageSize));
  const page = Math.min(requestedPage, totalPages);

  return {
    modelDir: getConfig().modelDir,
    recordsLoadedAt: recordsLoadedAt ? new Date(recordsLoadedAt).toISOString() : null,
    models: filteredModels.slice((page - 1) * pageSize, page * pageSize),
    totalModels: models.length,
    filteredModels: filteredModels.length,
    page,
    pageSize,
    totalPages,
    architectures: Array.from(architectures).sort(),
    statusCounts,
  };
}

export async function getModelDetails(from: string, to: string): Promise<ManagedModel[]> {
  assertLanguageCode(from, 'source');
  assertLanguageCode(to, 'target');
  const models = (await getInspectedModels()).filter(model => model.from === from && model.to === to);
  if (models.length === 0) {
    throw new ModelManagerError(404, 'MODEL_NOT_FOUND', `No model found for ${from} -> ${to}`);
  }
  return models;
}

export async function startModelDownload(
  from: string,
  to: string,
  version?: string,
  architecture?: string
): Promise<DownloadJob> {
  assertLanguageCode(from, 'source');
  assertLanguageCode(to, 'target');

  await ensureRecords();

  let selection: ModelSelection;
  try {
    selection = getModelSelection(from, to, version, architecture);
  } catch (error: any) {
    throw new ModelManagerError(404, 'MODEL_NOT_FOUND', error.message);
  }

  const resolvedArchitecture = selection.architecture || 'unknown';
  const key = modelKey(from, to, resolvedArchitecture, selection.version);
  const existingJobId = activeJobs.get(key);
  if (existingJobId) {
    const existingJob = jobs.get(existingJobId);
    if (existingJob) return toPublicJob(existingJob);
  }
  if (activeJobs.size + activeOcrJobs.size >= MAX_ACTIVE_DOWNLOADS) {
    throw new ModelManagerError(
      409,
      'DOWNLOAD_CONCURRENCY_LIMIT',
      `At most ${MAX_ACTIVE_DOWNLOADS} model downloads can run at the same time`
    );
  }

  const totalBytes = selection.records.reduce((total, record) => total + record.attachment.size, 0);
  const now = new Date().toISOString();
  const job: DownloadJobState = {
    kind: 'translation',
    id: crypto.randomUUID(),
    key,
    from,
    to,
    architecture: resolvedArchitecture,
    version: selection.version,
    source: getConfig().modelDownloadSource,
    status: 'queued',
    progress: 0,
    downloadedBytes: 0,
    totalBytes,
    createdAt: now,
    controller: new AbortController(),
  };

  jobs.set(job.id, job);
  activeJobs.set(key, job.id);
  trimJobHistory();
  queuePersistActiveDownloads();

  void runDownload(job, selection);
  return toPublicJob(job);
}

export async function startOcrModelDownload(modelId: string): Promise<DownloadJob> {
  if (typeof modelId !== 'string' || !/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(modelId)) {
    throw new ModelManagerError(400, 'INVALID_OCR_MODEL_ID', 'Invalid OCR model ID');
  }

  const existingJobId = activeOcrJobs.get(modelId);
  if (existingJobId) {
    const existingJob = ocrJobs.get(existingJobId);
    if (existingJob) return toPublicJob(existingJob);
  }
  if (activeJobs.size + activeOcrJobs.size >= MAX_ACTIVE_DOWNLOADS) {
    throw new ModelManagerError(
      409,
      'DOWNLOAD_CONCURRENCY_LIMIT',
      `At most ${MAX_ACTIVE_DOWNLOADS} model downloads can run at the same time`
    );
  }

  const catalog = await getOcrModelCatalog();
  const model = catalog.models.find(item => item.id === modelId);
  if (!model) {
    throw new ModelManagerError(404, 'OCR_MODEL_NOT_FOUND', `OCR model not found: ${modelId}`);
  }
  if (model.status === 'installed') {
    throw new ModelManagerError(409, 'OCR_MODEL_ALREADY_INSTALLED', 'OCR model is already installed');
  }
  if (!model.available || model.files.length === 0) {
    throw new ModelManagerError(409, 'OCR_MODEL_UNAVAILABLE', 'OCR model files are not available on the download site');
  }

  const key = `ocr\u0000${model.id}`;
  const totalBytes = model.files.reduce((total, file) => total + file.sizeBytes, 0);
  const now = new Date().toISOString();
  const job: DownloadJobState = {
    kind: 'ocr',
    id: crypto.randomUUID(),
    key,
    modelId: model.id,
    from: 'ocr',
    to: model.id,
    architecture: 'ocr',
    version: model.version,
    source: 'mirror',
    status: 'queued',
    progress: 0,
    downloadedBytes: 0,
    totalBytes,
    createdAt: now,
    controller: new AbortController(),
  };

  ocrJobs.set(job.id, job);
  activeOcrJobs.set(model.id, job.id);
  trimOcrJobHistory();
  queuePersistActiveDownloads();

  void runOcrDownload(job, model);
  return toPublicJob(job);
}

export async function resumePendingDownloads(): Promise<void> {
  if (resumePromise) return resumePromise;
  resumePromise = (async () => {
    const pending = await readPersistedDownloads();
    for (const item of pending) {
      try {
        if (item.kind === 'ocr') {
          await startOcrModelDownload(item.modelId);
        } else {
          await startModelDownload(item.from, item.to, item.version, item.architecture);
        }
      } catch (error) {
        const target = item.kind === 'ocr' ? `ocr/${item.modelId}` : `${item.from} -> ${item.to}`;
        logger.warn(`Failed to resume model download ${target}: ${error instanceof Error ? error.message : String(error)}`);
      }
    }
  })();
  return resumePromise;
}

async function runDownload(job: DownloadJobState, selection: ModelSelection) {
  job.status = 'checking';
  job.startedAt = new Date().toISOString();

  try {
    await downloadModel(
      job.to,
      job.from,
      selection.version,
      selection.architecture,
      progress => updateDownloadJob(job, progress),
      job.controller.signal,
      () => { job.fallbackUsed = true }
    );
    job.status = 'completed';
    job.progress = 100;
    job.downloadedBytes = job.totalBytes;
    job.finishedAt = new Date().toISOString();
    logger.info(`Model download completed: ${job.from} -> ${job.to} (${job.architecture})`);
  } catch (error: any) {
    const cancelled = error instanceof DownloadCancelledError || job.controller.signal.aborted;
    job.status = cancelled ? 'cancelled' : 'failed';
    job.errorCode = cancelled ? 'cancelled' : classifyDownloadError(error);
    job.error = cancelled ? 'Download cancelled; partial files were kept for a later resume' : error?.message || String(error);
    job.finishedAt = new Date().toISOString();
    if (cancelled) {
      logger.info(`Model download cancelled: ${job.from} -> ${job.to}`);
    } else {
      logger.error(`Model download failed: ${job.from} -> ${job.to}: ${job.error}`);
    }
  } finally {
    activeJobs.delete(job.key);
    queuePersistActiveDownloads();
    trimJobHistory();
  }
}

async function runOcrDownload(job: DownloadJobState, model: OcrModelSummary) {
  job.status = 'checking';
  job.startedAt = new Date().toISOString();
  const downloader = createDownloader();
  const proxy = getConfig().downloadProxy || undefined;
  let completedBytes = 0;

  try {
    for (const file of model.files) {
      const filePath = ocrFilePath(model, file);
      job.currentFile = file.path;
      job.status = 'downloading';
      updateOcrDownloadJob(job, completedBytes, file, 0);
      await downloader.download({
        url: file.url,
        outputPath: filePath,
        hash: file.sha256,
        proxy,
        signal: job.controller.signal,
        onProgress: ({ downloadedBytes }) => updateOcrDownloadJob(job, completedBytes, file, downloadedBytes),
      });
      completedBytes += file.sizeBytes;
      updateOcrDownloadJob(job, completedBytes - file.sizeBytes, file, file.sizeBytes);
    }

    job.status = 'completed';
    job.progress = 100;
    job.downloadedBytes = job.totalBytes;
    job.finishedAt = new Date().toISOString();
    logger.info(`OCR model download completed: ${model.id}`);
  } catch (error: any) {
    const cancelled = error instanceof DownloadCancelledError || job.controller.signal.aborted;
    job.status = cancelled ? 'cancelled' : 'failed';
    job.errorCode = cancelled ? 'cancelled' : classifyDownloadError(error);
    job.error = cancelled ? 'Download cancelled; partial files were kept for a later resume' : error?.message || String(error);
    job.finishedAt = new Date().toISOString();
    if (cancelled) {
      logger.info(`OCR model download cancelled: ${model.id}`);
    } else {
      logger.error(`OCR model download failed: ${model.id}: ${job.error}`);
    }
  } finally {
    activeOcrJobs.delete(model.id);
    queuePersistActiveDownloads();
    trimOcrJobHistory();
  }
}

function updateDownloadJob(job: DownloadJobState, progress: ModelDownloadProgress) {
  job.status = progress.stage === 'decompressing' ? 'decompressing' : progress.stage;
  job.currentFile = progress.filename;
  job.downloadedBytes = Math.min(
    progress.totalBytes,
    progress.completedBytes + progress.fileBytesDownloaded
  );
  job.totalBytes = progress.totalBytes;
  job.progress = progress.totalBytes > 0
    ? Math.min(100, Math.round((job.downloadedBytes / progress.totalBytes) * 100))
    : 0;
  const now = Date.now();
  if (job.lastProgressAt && job.lastProgressBytes !== undefined && now > job.lastProgressAt && progress.stage === 'downloading') {
    const elapsedMs = now - job.lastProgressAt;
    const deltaBytes = job.downloadedBytes - job.lastProgressBytes;
    if (deltaBytes >= 0) job.downloadRateMbps = deltaBytes * 8 / elapsedMs / 1000;
  }
  job.lastProgressAt = now;
  job.lastProgressBytes = job.downloadedBytes;
}

function updateOcrDownloadJob(job: DownloadJobState, completedBytes: number, file: OcrModelFile, fileDownloadedBytes: number) {
  const downloadedBytes = Math.min(file.sizeBytes, Math.max(0, fileDownloadedBytes));
  job.currentFile = file.path;
  job.downloadedBytes = Math.min(job.totalBytes, completedBytes + downloadedBytes);
  job.progress = job.totalBytes > 0
    ? Math.min(100, Math.round((job.downloadedBytes / job.totalBytes) * 100))
    : 0;
  const now = Date.now();
  if (job.lastProgressAt && job.lastProgressBytes !== undefined && now > job.lastProgressAt && job.status === 'downloading') {
    const elapsedMs = now - job.lastProgressAt;
    const deltaBytes = job.downloadedBytes - job.lastProgressBytes;
    if (deltaBytes >= 0) job.downloadRateMbps = deltaBytes * 8 / elapsedMs / 1000;
  }
  job.lastProgressAt = now;
  job.lastProgressBytes = job.downloadedBytes;
}

function classifyDownloadError(error: unknown): DownloadErrorCode {
  const message = error instanceof Error ? error.message : String(error);
  if (/hash mismatch/i.test(message)) return 'hash_mismatch';
  if (/decompress|zstd|解压/i.test(message)) return 'decompression';
  if (/HTTP|timeout|network|socket|proxy|ECONN|ENOTFOUND|fetch failed/i.test(message)) return 'network';
  return 'unknown';
}

export function getDownloadJobs(activeOnly = false): DownloadJob[] {
  const allJobs = [...Array.from(jobs.values()), ...Array.from(ocrJobs.values())];
  const selectedJobs = activeOnly
    ? allJobs.filter(job => ['queued', 'checking', 'downloading', 'decompressing'].includes(job.status))
    : allJobs;
  return selectedJobs
    .sort((a, b) => b.createdAt.localeCompare(a.createdAt))
    .map(toPublicJob);
}

export function getDownloadJob(id: string): DownloadJob {
  const job = jobs.get(id) || ocrJobs.get(id);
  if (!job) {
    throw new ModelManagerError(404, 'DOWNLOAD_NOT_FOUND', `Download job not found: ${id}`);
  }
  return toPublicJob(job);
}

export function cancelModelDownload(id: string): DownloadJob {
  const job = jobs.get(id) || ocrJobs.get(id);
  if (!job) {
    throw new ModelManagerError(404, 'DOWNLOAD_NOT_FOUND', `Download job not found: ${id}`);
  }
  if (!['queued', 'checking', 'downloading', 'decompressing'].includes(job.status)) {
    throw new ModelManagerError(409, 'DOWNLOAD_NOT_ACTIVE', 'Download is no longer active');
  }
  job.controller.abort();
  return toPublicJob(job);
}

function getActiveTranslationJobs(from: string, to: string): DownloadJobState[] {
  const prefix = `${from}\u0000${to}\u0000`;
  const matchingJobs: DownloadJobState[] = [];
  for (const [key, jobId] of activeJobs.entries()) {
    if (!key.startsWith(prefix)) continue;
    const job = jobs.get(jobId);
    if (job) matchingJobs.push(job);
  }
  return matchingJobs;
}

async function waitForTranslationJobsToStop(from: string, to: string, timeoutMs = 10000): Promise<void> {
  const prefix = `${from}\u0000${to}\u0000`;
  const deadline = Date.now() + timeoutMs;
  while ([...activeJobs.keys()].some(key => key.startsWith(prefix))) {
    if (Date.now() >= deadline) {
      throw new ModelManagerError(409, 'DOWNLOAD_IN_PROGRESS', 'Cannot remove a model while it is downloading');
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}

export async function removeModel(from: string, to: string): Promise<void> {
  assertLanguageCode(from, 'source');
  assertLanguageCode(to, 'target');

  const activeTranslationJobs = getActiveTranslationJobs(from, to);
  for (const job of activeTranslationJobs) {
    job.controller.abort();
  }

  if (activeTranslationJobs.length > 0) {
    logger.info(`Cancelling ${activeTranslationJobs.length} active download(s) before removing model: ${from} -> ${to}`);
    await waitForTranslationJobsToStop(from, to);
  }

  const directory = modelDirectory(from, to);
  try {
    await fs.rm(directory, { recursive: true, force: false });
  } catch (error: any) {
    if (error?.code === 'ENOENT') return;
    throw new ModelManagerError(500, 'MODEL_REMOVE_FAILED', `Failed to remove model: ${error.message}`);
  }
}

export async function refreshModelRecords(): Promise<ModelCatalog> {
  try {
    await refreshRecords();
  } catch (error: any) {
    throw new ModelManagerError(409, 'RECORDS_REFRESH_FAILED', error.message);
  }
  return getModelCatalog();
}
