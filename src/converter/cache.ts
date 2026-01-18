import fs from 'node:fs/promises';
import { createReadStream, existsSync } from 'node:fs';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { Worker } from 'node:worker_threads';
import { lock } from 'proper-lockfile';
import * as logger from '../logger/index.js';
import { VERSION } from '../version/index.js';

export const CONVERSION_VERSION = 'marian-qall-k9-v1';
export const CONVERSION_PARAMETERS = { strategy: 'qall', topK: 9, int4BlockSize: 64, int4AccuracyLevel: 4, int8WeightType: 'QInt8', perTensor: true, reduceRange: false, opset: 11, irVersion: 10 } as const;

export interface ConvertedModel { directory: string; identity: string; model: string; sourceVocab: string; targetVocab: string }
export interface SourceFiles { model: string; vocab_src: string; vocab_trg: string; lex?: string }
export interface ConversionSource { files: SourceFiles; records: unknown }

interface ManifestFile {
  path: string;
  role: string;
  description: string;
  origin: 'mozilla' | 'mtranserver';
  required: boolean;
  sha256?: string;
}
interface Manifest {
  schemaVersion: 1;
  mtranserverVersion: string;
  identity: string;
  sourceRevision: string;
  converterRevision?: string;
  conversion?: { version: string; parameters: typeof CONVERSION_PARAMETERS };
  records: unknown;
  files: ManifestFile[];
}

const pending = new Map<string, Promise<ConvertedModel>>();
let conversionQueue: Promise<unknown> = Promise.resolve();

export async function fileHash(file: string) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}

async function exists(file: string) { try { await fs.access(file); return true; } catch { return false; } }

function hashJson(value: unknown) {
  return createHash('sha256').update(JSON.stringify(value)).digest('hex');
}

const CONVERTER_REVISION = hashJson({ version: CONVERSION_VERSION, parameters: CONVERSION_PARAMETERS });

function normalizedRecords(records: unknown) {
  if (!Array.isArray(records)) return records;
  return [...records].sort((a, b) => JSON.stringify(a).localeCompare(JSON.stringify(b)));
}

function sourceRevision(records: unknown) {
  const conversionInputs = Array.isArray(records)
    ? records.filter(record => ['model', 'vocab', 'srcvocab', 'trgvocab'].includes(record?.fileType))
    : records;
  return hashJson(normalizedRecords(conversionInputs));
}

function runWorker(model: string, sourceVocab: string, targetVocab: string, output: string) {
  const task = conversionQueue.then(() => new Promise<void>((resolve, reject) => {
    const bundled = new URL('./converter-worker.js', import.meta.url);
    const source = new URL('../../scripts/converter-worker.mjs', import.meta.url);
    const worker = new Worker(existsSync(bundled) ? bundled : source, { execArgv: process.execArgv.filter(arg => !arg.startsWith('--input-type')), workerData: { model, sourceVocab, targetVocab, output } });
    let responded = false;
    worker.once('message', message => { responded = true; if (message.error) reject(new Error(message.error)); else resolve(); });
    worker.once('error', reject);
    worker.once('exit', code => { if (code !== 0 || !responded) reject(new Error(`Conversion worker exited with ${code}`)); });
  }));
  conversionQueue = task.catch(() => {});
  return task;
}

function recordFiles(records: unknown): ManifestFile[] {
  if (!Array.isArray(records)) return [];
  const descriptions: Record<string, string> = {
    model: 'Mozilla Marian source model',
    lex: 'Mozilla lexical shortlist',
    vocab: 'Shared SentencePiece vocabulary',
    srcvocab: 'Source-language SentencePiece vocabulary',
    trgvocab: 'Target-language SentencePiece vocabulary',
  };
  return records.flatMap(record => {
    const compressedName = record?.attachment?.filename;
    if (typeof compressedName !== 'string') return [];
    const filename = compressedName.replace(/\.zst$/, '');
    if (path.basename(filename) !== filename) throw new Error('Invalid model filename');
    return [{
      path: filename,
      role: `source-${record.fileType ?? 'file'}`,
      description: descriptions[record.fileType] ?? `Mozilla ${record.fileType ?? 'model'} file`,
      origin: 'mozilla' as const,
      required: true,
      ...(typeof record.decompressedHash === 'string' ? { sha256: record.decompressedHash } : {}),
    }];
  });
}

function result(target: string, identity: string, files: SourceFiles): ConvertedModel {
  return { directory: target, identity, model: path.join(target, 'model.onnx'), sourceVocab: files.vocab_src, targetVocab: files.vocab_trg };
}

function compatible(manifest: Manifest, revision: string) {
  if (manifest.schemaVersion !== 1 || manifest.sourceRevision !== revision) return false;
  if (manifest.mtranserverVersion === VERSION) return true;
  if (manifest.converterRevision === CONVERTER_REVISION) return true;
  return manifest.conversion ? hashJson(manifest.conversion) === CONVERTER_REVISION : false;
}

async function hasRequiredFiles(target: string, manifest: Manifest, source: SourceFiles) {
  if (!Array.isArray(manifest.files)) return false;
  const required = manifest.files.filter(file => file?.required);
  if (!required.length || required.some(file => typeof file.path !== 'string' || path.basename(file.path) !== file.path)) return false;
  const files = [
    ...required.map(file => path.join(target, file.path)),
    path.join(target, 'model.onnx'),
    source.model,
    source.vocab_src,
    source.vocab_trg,
  ];
  return (await Promise.all([...new Set(files)].map(exists))).every(Boolean);
}

async function replaceFile(staged: string, destination: string) {
  const previous = destination + '.previous';
  await fs.rm(previous, { force: true });
  if (await exists(destination)) await fs.rename(destination, previous);
  try {
    await fs.rename(staged, destination);
    await fs.rm(previous, { force: true });
  } catch (error) {
    if (await exists(previous)) await fs.rename(previous, destination);
    throw error;
  }
}

async function writeManifest(target: string, manifest: Manifest) {
  const staged = path.join(target, `.manifest.${randomUUID()}.tmp`);
  await fs.writeFile(staged, JSON.stringify(manifest, null, 2) + '\n');
  await replaceFile(staged, path.join(target, 'manifest.json'));
}

export function ensureConverted(root: string, pair: string, source: ConversionSource, repairSource: () => Promise<SourceFiles>): Promise<ConvertedModel> {
  if (!/^[A-Za-z0-9-]+_[A-Za-z0-9-]+$/.test(pair)) return Promise.reject(new Error('Invalid language pair'));
  const target = path.resolve(root, pair);
  const existing = pending.get(target);
  if (existing) return existing;
  const task = prepare(target, source, repairSource).finally(() => pending.delete(target));
  pending.set(target, task);
  return task;
}

async function prepare(target: string, source: ConversionSource, repairSource: () => Promise<SourceFiles>): Promise<ConvertedModel> {
  await fs.mkdir(target, { recursive: true });
  const unlock = await lock(target, { stale: 30000, update: 5000, retries: { forever: true, minTimeout: 100, maxTimeout: 1000 } });
  const revision = sourceRevision(source.records);
  let stagedModel = '';
  try {
    let repairedFiles: SourceFiles | undefined;
    try {
      const manifest: Manifest = JSON.parse(await fs.readFile(path.join(target, 'manifest.json'), 'utf8'));
      if (compatible(manifest, revision)) {
        if (!await hasRequiredFiles(target, manifest, source.files)) repairedFiles = await repairSource();
        if (await hasRequiredFiles(target, manifest, repairedFiles ?? source.files)) {
          if (manifest.mtranserverVersion !== VERSION || manifest.converterRevision !== CONVERTER_REVISION || manifest.conversion) {
            manifest.mtranserverVersion = VERSION;
            manifest.converterRevision = CONVERTER_REVISION;
            delete manifest.conversion;
            await writeManifest(target, manifest);
          }
          logger.debug(`Reusing converted model ${path.basename(target)}`);
          return result(target, manifest.identity, repairedFiles ?? source.files);
        }
      }
    } catch (error: any) {
      if (error.code && error.code !== 'ENOENT') logger.warn(`Invalid model manifest, repairing: ${error}`);
    }

    const files = repairedFiles ?? await repairSource();
    for (const file of [files.model, files.vocab_src, files.vocab_trg]) {
      if (!await exists(file)) throw new Error(`Required source file not found: ${file}`);
    }
    const sourceHashes = {
      model: await fileHash(files.model),
      sourceVocab: await fileHash(files.vocab_src),
      targetVocab: await fileHash(files.vocab_trg),
    };
    const identity = hashJson({ version: CONVERSION_VERSION, parameters: CONVERSION_PARAMETERS, source: sourceHashes });
    logger.info(`Converting model ${path.basename(target)} (${CONVERSION_VERSION})`);
    stagedModel = path.join(target, `.model.${randomUUID()}.onnx`);
    await runWorker(files.model, files.vocab_src, files.vocab_trg, stagedModel);
    if (await fileHash(files.model) !== sourceHashes.model || await fileHash(files.vocab_src) !== sourceHashes.sourceVocab || await fileHash(files.vocab_trg) !== sourceHashes.targetVocab) {
      throw new Error('Source changed during conversion');
    }
    const outputHash = await fileHash(stagedModel);
    const manifest: Manifest = {
      schemaVersion: 1,
      mtranserverVersion: VERSION,
      identity,
      sourceRevision: revision,
      converterRevision: CONVERTER_REVISION,
      records: normalizedRecords(source.records),
      files: [
        ...recordFiles(source.records),
        { path: 'model.onnx', role: 'inference-model', description: 'MTranServer generated ONNX inference model', origin: 'mtranserver', required: true, sha256: outputHash },
        { path: 'manifest.json', role: 'manifest', description: 'Model cache metadata and file role index', origin: 'mtranserver', required: true },
      ],
    };
    await replaceFile(stagedModel, path.join(target, 'model.onnx'));
    stagedModel = '';
    await writeManifest(target, manifest);
    logger.info(`Converted model ready: ${target}`);
    return result(target, identity, files);
  } finally {
    if (stagedModel) await fs.rm(stagedModel, { force: true });
    await unlock();
  }
}
