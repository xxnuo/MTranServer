import fs from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { createHash, randomUUID } from 'node:crypto';
import path from 'node:path';
import type { ModelConfig, Weight } from './release.js';
import { fileHash, runWorker, type ConvertedModel } from './cache.js';

const revision = 'marian-metal-q8-v2';
const pending = new Map<string, Promise<string>>();

export async function writeMetalModel(filename: string, config: ModelConfig, weights: Map<string, Weight>, sourceEos: number, targetEos: number) {
  const embedding = weights.get(config['tied-embeddings-all'] ? 'Wemb' : 'decoder_Wemb');
  if (!embedding || !Number.isInteger(sourceEos) || !Number.isInteger(targetEos) || sourceEos < 0 || targetEos < 0 || targetEos >= embedding.dims[0]) throw new Error('Invalid Metal model vocabulary');
  const integer = (value: number) => {
    if (!Number.isSafeInteger(value) || value < 0 || value > 0xffff_ffff) throw new Error('Invalid Metal model integer');
    const bytes = Buffer.alloc(4); bytes.writeUInt32LE(value); return bytes;
  };
  const file = await fs.open(filename, 'wx');
  try {
    const header = [config['dim-emb'], config['transformer-heads'], config['enc-depth'], config['dec-depth'], config['transformer-ffn-depth'], embedding.dims[0], targetEos, Number(!!config['tied-embeddings-all']), sourceEos, weights.size];
    await file.writeFile(Buffer.concat([Buffer.from('MTRANMX2'), ...header.map(integer)]));
    for (const [name, weight] of weights) {
      if (!/^[A-Za-z0-9_]+$/.test(name) || !weight.dims.length || weight.dims.reduce((a, b) => a * b, 1) !== weight.values.length) throw new Error('Invalid Metal weight');
      const label = Buffer.from(name);
      await file.writeFile(Buffer.concat([integer(label.length), label, integer(weight.dims.length), ...weight.dims.map(integer)]));
      await file.writeFile(Buffer.from(weight.values.buffer, weight.values.byteOffset, weight.values.byteLength));
    }
  } finally { await file.close(); }
}

export function prepareMetalModel(model: ConvertedModel, sourceEos: number, targetEos: number): Promise<string> {
  const key = createHash('sha256').update(JSON.stringify([revision, model.identity, sourceEos, targetEos])).digest('hex');
  const filename = path.join(model.directory, `metal-${key}.bin`);
  const existing = pending.get(filename); if (existing) return existing;
  const task = (async () => {
    if (!model.sourceModel || !model.sourceModelSha256) throw new Error('Metal needs a verified original Marian model');
    const checksum = `${filename}.sha256`;
    if (existsSync(filename) && existsSync(checksum) && await fileHash(filename) === (await fs.readFile(checksum, 'utf8')).trim()) return filename;
    if (await fileHash(model.sourceModel) !== model.sourceModelSha256) throw new Error('Metal source model checksum mismatch');
    const temporary = `${filename}.${randomUUID()}.tmp`;
    try {
      await runWorker(model.sourceModel, model.sourceVocab, model.targetVocab, temporary, false, undefined, { sourceEos, targetEos });
      if (await fileHash(model.sourceModel) !== model.sourceModelSha256) throw new Error('Source changed during Metal conversion');
      await fs.writeFile(`${temporary}.sha256`, await fileHash(temporary));
      await fs.rename(temporary, filename);
      await fs.rename(`${temporary}.sha256`, checksum);
      return filename;
    } finally { await Promise.all([temporary, `${temporary}.sha256`].map(file => fs.rm(file, { force: true }))); }
  })().finally(() => pending.delete(filename));
  pending.set(filename, task); return task;
}
