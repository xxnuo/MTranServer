import fs from 'node:fs/promises';
import { createReadStream, existsSync } from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { Worker } from 'node:worker_threads';
import { lock } from 'proper-lockfile';
import * as logger from '../logger/index.js';

export const CONVERSION_VERSION = 'marian-qdecoder-k9-v1';
export const CONVERSION_PARAMETERS = { strategy: 'qdecoder', topK: 9, int4BlockSize: 64, int4AccuracyLevel: 4, int8WeightType: 'QInt8', perTensor: true, reduceRange: false, opset: 11, irVersion: 10 } as const;
export interface ConvertedModel { directory: string; identity: string; model: string; sourceVocab: string; targetVocab: string }
export interface SourceFiles { model: string; vocab_src: string; vocab_trg: string }
interface Manifest { parameters: typeof CONVERSION_PARAMETERS; version: string; identity: string; source: Record<string, string>; files: Record<string, string>; records: unknown }
const pending = new Map<string, Promise<ConvertedModel>>();
let conversionQueue: Promise<unknown> = Promise.resolve();
export async function fileHash(file: string) {
  const hash = createHash('sha256');
  for await (const chunk of createReadStream(file)) hash.update(chunk);
  return hash.digest('hex');
}
async function exists(file: string) { try { await fs.access(file); return true; } catch { return false; } }
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
export function ensureConverted(root: string, pair: string, loadSource: () => Promise<{ files: SourceFiles; records: unknown }>): Promise<ConvertedModel> {
  if (!/^[A-Za-z0-9-]+_[A-Za-z0-9-]+$/.test(pair)) return Promise.reject(new Error('Invalid language pair'));
  const target = path.resolve(root, pair);
  const existing = pending.get(target); if (existing) return existing;
  const task = prepare(target, loadSource).finally(() => pending.delete(target));
  pending.set(target, task); return task;
}
async function prepare(target: string, loadSource: () => Promise<{ files: SourceFiles; records: unknown }>): Promise<ConvertedModel> {
  await fs.mkdir(path.dirname(target), { recursive: true });
  const unlock = await lock(target, { realpath: false, stale: 30000, update: 5000, retries: { forever: true, minTimeout: 100, maxTimeout: 1000 } });
  const previous = target + '.previous', staging = target + '.staging';
  try {
    if (!await exists(target) && await exists(previous)) await fs.rename(previous, target);
    if (await exists(target)) await fs.rm(previous, { recursive: true, force: true });
    await fs.rm(staging, { recursive: true, force: true });
    const { files, records } = await loadSource();
    const source = { model: await fileHash(files.model), sourceVocab: await fileHash(files.vocab_src), targetVocab: await fileHash(files.vocab_trg) };
    const identity = createHash('sha256').update(JSON.stringify({ version: CONVERSION_VERSION, parameters: CONVERSION_PARAMETERS, source })).digest('hex');
    const result = (): ConvertedModel => ({ directory: target, identity, model: path.join(target, 'model.onnx'), sourceVocab: path.join(target, 'source.spm'), targetVocab: path.join(target, 'target.spm') });
    try {
      const manifest: Manifest = JSON.parse(await fs.readFile(path.join(target, 'manifest.json'), 'utf8'));
      if (manifest.identity === identity && ['model.onnx', 'source.spm', 'target.spm'].every(name => typeof manifest.files?.[name] === 'string')) {
        const valid = await Promise.all(['model.onnx', 'source.spm', 'target.spm'].map(async name => await fileHash(path.join(target, name)) === manifest.files[name]));
        if (valid.every(Boolean)) { logger.debug(`Reusing converted model ${path.basename(target)}`); return result(); }
      }
    } catch (e: any) { if (e.code && e.code !== 'ENOENT') throw e; }
    logger.info(`Converting model ${path.basename(target)} (${CONVERSION_VERSION})`);
    await fs.mkdir(staging);
    await fs.copyFile(files.vocab_src, path.join(staging, 'source.spm'));
    await fs.copyFile(files.vocab_trg, path.join(staging, 'target.spm'));
    await runWorker(files.model, path.join(staging, 'source.spm'), path.join(staging, 'target.spm'), path.join(staging, 'model.onnx'));
    // Source files may also be managed by another application.
    if (await fileHash(files.model) !== source.model || await fileHash(files.vocab_src) !== source.sourceVocab || await fileHash(files.vocab_trg) !== source.targetVocab) throw new Error('Source changed during conversion');
    const outputs: Record<string, string> = {};
    for (const name of ['model.onnx', 'source.spm', 'target.spm']) outputs[name] = await fileHash(path.join(staging, name));
    const manifest: Manifest = { version: CONVERSION_VERSION, parameters: CONVERSION_PARAMETERS, identity, source, records, files: outputs };
    await fs.writeFile(path.join(staging, 'manifest.json'), JSON.stringify(manifest, null, 2));
    if (await exists(target)) await fs.rename(target, previous);
    try { await fs.rename(staging, target); } catch (error) { if (await exists(previous)) await fs.rename(previous, target); throw error; }
    await fs.rm(previous, { recursive: true, force: true });
    logger.info(`Converted model ready: ${target}`);
    return result();
  } finally { try { await fs.rm(staging, { recursive: true, force: true }); } finally { await unlock(); } }
}
