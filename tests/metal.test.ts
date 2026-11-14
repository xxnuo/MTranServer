import { afterEach, expect, spyOn, test } from 'bun:test';
import path from 'node:path';
import fs from 'node:fs/promises';
import { MetalWorker } from '../src/core/metal';
import { writeMetalModel } from '../src/converter/metal';

const fixture = path.resolve('tests/fixtures/metal-worker.mjs');
const workers: MetalWorker[] = [];
function worker(mode = 'normal') { const instance = new MetalWorker(process.execPath, fixture, mode); workers.push(instance); return instance; }
afterEach(async () => { await Promise.all(workers.splice(0).map(w => w.close())); });

test('Metal protocol handles fragmented responses and drains accepted jobs on close', async () => {
  const w = worker();
  const first = w.translate([{ ids: [8, 9, 0], candidates: [0, 8, 9] }], true);
  const second = w.translate([{ ids: [2, 0], candidates: [] }], false);
  const closing = w.close();
  expect(await first).toEqual([[8, 9]]);
  expect(await second).toEqual([[2]]);
  await closing;
  await expect(w.translate([{ ids: [1, 0], candidates: [] }], false)).rejects.toThrow('closing');
});

test('Metal batches respect padded token budget and restore sentence order', async () => {
  const w = worker();
  const rows = Array.from({ length: 37 }, (_, i) => ({ ids: [...Array(511 - i).fill(i + 1), 0], candidates: [] }));
  expect(await w.translateMany(rows, false)).toEqual(rows.map(r => r.ids.slice(0, -1)));
});

for (const mode of ['crash', 'truncated', 'wrong-count', 'invalid-token']) test(`Metal ${mode} rejects queued work and closes`, async () => {
  const w = worker(mode), row = [{ ids: [2, 0], candidates: [] }];
  const results = await Promise.allSettled([w.translate(row, false), w.translate(row, false)]);
  expect(results.every(r => r.status === 'rejected')).toBe(true);
  await w.close();
});

test('invalid Metal input is rejected before serialization without poisoning the worker', async () => {
  const w = worker();
  await expect(w.translate([{ ids: [-1], candidates: [] }], false)).rejects.toThrow('integer');
  await expect(w.translate([], false)).rejects.toThrow('batch');
  expect(await w.translate([{ ids: [3, 0], candidates: [] }], false)).toEqual([[3]]);
});

test('Metal initialization deadline rejects without an uncaught timer exception', async () => {
  const original = globalThis.setTimeout;
  const timer = spyOn(globalThis, 'setTimeout').mockImplementation(((fn: (...args: any[]) => void, ms: number, ...args: any[]) => original(fn, ms === 30_000 ? 30 : ms, ...args)) as any);
  try { const w = worker('no-ready'); await expect(w.ready).rejects.toThrow('timed out'); await w.close(); }
  finally { timer.mockRestore(); }
});

test('Metal export rejects malformed weights and never overwrites an existing file', async () => {
  await fs.mkdir(path.resolve('.tmp'), { recursive: true });
  const directory = await fs.mkdtemp(path.resolve('.tmp/metal-export-'));
  const filename = path.join(directory, 'model.bin');
  const config = { 'dim-emb': 64, 'transformer-heads': 1, 'enc-depth': 1, 'dec-depth': 1, 'transformer-ffn-depth': 1, 'tied-embeddings-all': true };
  try {
    await fs.writeFile(filename, 'keep');
    const weights = new Map([['Wemb', { dims: [4, 64], values: new Float32Array(256) }]]);
    await expect(writeMetalModel(filename, config, weights, 0, 0)).rejects.toThrow();
    expect(await fs.readFile(filename, 'utf8')).toBe('keep');
    await fs.rm(filename);
    weights.set('bad', { dims: [2], values: new Float32Array(1) });
    await expect(writeMetalModel(filename, config, weights, 0, 0)).rejects.toThrow('weight');
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test('Metal recycles shape caches without dropping queued requests', async () => {
  const w = worker();
  const inputs = Array.from({ length: 140 }, (_, i) => [{ ids: [i + 1, 0], candidates: [] }]);
  expect(await Promise.all(inputs.map(rows => w.translate(rows, false)))).toEqual(inputs.map(rows => [rows[0].ids.slice(0, -1)]));
  await w.close();
});

test('Metal refuses a source model that no longer matches the ONNX conversion', async () => {
  const { prepareMetalModel } = await import('../src/converter/metal');
  await fs.mkdir(path.resolve('.tmp'), { recursive: true });
  const directory = await fs.mkdtemp(path.resolve('.tmp/metal-checksum-'));
  const sourceModel = path.join(directory, 'source.bin');
  try {
    await fs.writeFile(sourceModel, 'changed weights');
    const model = { directory, model: 'unused.onnx', identity: 'checksum', sourceModel, sourceModelSha256: '0'.repeat(64), sourceVocab: '', targetVocab: '' };
    await expect(prepareMetalModel(model, 0, 0)).rejects.toThrow('checksum mismatch');
    expect(await fs.readFile(sourceModel, 'utf8')).toBe('changed weights');
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});
