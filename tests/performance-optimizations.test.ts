import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { mkdir, mkdtemp, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import protobuf from 'protobufjs';
import * as ort from 'onnxruntime-node';
import { Shortlist, checksum, generateCandidates, parseLexicon } from '../src/core/shortlist';
import { selectBackend } from '../src/core/backend';
import { TranslationEngine } from '../src/core/engine';
import * as metal from '../src/core/metal';
import { TranslateController } from '../src/controllers/translate.controller';

let directory: string | undefined;
afterEach(async () => {
  mock.restore();
  if (directory) await rm(directory, { recursive: true, force: true });
  directory = undefined;
});

function vocabBytes(size = 96, eos = 2) {
  const w = protobuf.Writer.create();
  for (let id = 0; id < size; id++) {
    const piece = protobuf.Writer.create().uint32(10).string(`p${id}`)
      .uint32(24).int32([5, 33, 77].includes(id) ? 6 : 1).finish();
    w.uint32(10).bytes(piece);
  }
  return w.uint32(18).bytes(protobuf.Writer.create().uint32(336).int32(eos).finish()).finish();
}

function lexBytes(sourceSize = 96) {
  const offsets = new Array<number>(sourceSize + 1).fill(0);
  const targets: number[] = [];
  for (let s = 0; s < sourceSize; s++) {
    offsets[s] = targets.length;
    targets.push((s * 7 + 13) % 96, (s * 11 + 29) % 96);
  }
  offsets[sourceSize] = targets.length;
  const buf = Buffer.alloc(48 + offsets.length * 8 + targets.length * 4);
  buf.writeBigUInt64LE(0xF11A48D5013417F5n, 0);
  buf.writeBigUInt64LE(12n, 16);
  buf.writeBigUInt64LE(2n, 24);
  buf.writeBigUInt64LE(BigInt(offsets.length), 32);
  buf.writeBigUInt64LE(BigInt(targets.length), 40);
  offsets.forEach((n, i) => buf.writeBigUInt64LE(BigInt(n), 48 + i * 8));
  targets.forEach((id, i) => buf.writeUInt32LE(id, 48 + offsets.length * 8 + i * 4));
  buf.writeBigUInt64LE(checksum(buf.subarray(16)), 8);
  return buf;
}

test('Shortlist bitset and candidatesBigInt match generateCandidates across shared and separate vocabs', async () => {
  await mkdir('.tmp', { recursive: true });
  directory = await mkdtemp(path.resolve('.tmp/perf-opt-test-'));
  const srcPath = path.join(directory, 'src.spm');
  const dstPath = path.join(directory, 'dst.spm');
  const lexPath = path.join(directory, 'lex.bin');
  const rawLex = lexBytes(96);
  await Promise.all([
    writeFile(srcPath, vocabBytes(96, 2)),
    writeFile(dstPath, vocabBytes(96, 3)),
    writeFile(lexPath, rawLex),
  ]);
  const parsed = parseLexicon(rawLex, 96, 96);
  const shared = await Shortlist.load(lexPath, srcPath, srcPath);
  const separate = await Shortlist.load(lexPath, srcPath, dstPath);
  for (const sample of [[0, 2], [15, 15, 88, 95], [1, 33, 64, 77]]) {
    const expShared = generateCandidates(parsed, sample, 96, true, 2, [5, 33, 77]);
    const expSeparate = generateCandidates(parsed, sample, 96, false, 3, [5, 33, 77]);
    expect(shared.candidates(sample)).toEqual(expShared);
    expect(Array.from(shared.candidatesBigInt(sample), Number)).toEqual(expShared);
    expect(Array.from(shared.candidatesBigInt(BigInt64Array.from(sample, BigInt)), Number)).toEqual(expShared);
    expect(separate.candidates(sample)).toEqual(expSeparate);
    expect(Array.from(separate.candidatesBigInt(sample), Number)).toEqual(expSeparate);
  }
  expect(() => shared.candidatesBigInt([])).toThrow('source');
  expect(() => shared.candidatesBigInt([96n])).toThrow('source');
});

test('selectBackend skipCpuSweep creates a single CPU session when no accelerator exists and still discovers GPUs', async () => {
  let created = 0;
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_m: any, opts: any) => {
    created++;
    return { provider: opts.executionProviders[0], release: mock(async () => {}) } as any;
  });
  const res = await selectBackend('fast.onnx', async () => {}, undefined, 'skip-sweep-cpu', false, true);
  expect(created).toBe(1);
  expect(res.info.provider).toBe('cpu');
  expect(res.info.measurements).toHaveLength(1);
  await res.session.release();

  created = 0;
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  const gpuRes = await selectBackend('fast.onnx', async (s: any) => { clock += s.provider === 'cpu' ? 10 : 0.1; }, undefined, 'skip-sweep-gpu', false, true);
  expect(gpuRes.info.provider).toBe('webgpu');
  expect(gpuRes.info.measurements.map(m => m.provider)).toEqual(['cpu', 'webgpu']);
  await gpuRes.session.release();
});

test('calibrated Metal worker serves one concurrent document while overflowing competing documents to CPU', async () => {
  const engine = new TranslationEngine() as any;
  let next = 1, gpuCalls = 0, cpuCalls = 0;
  const texts = new Map<number, string>();
  let releaseGpu!: () => void, enteredGpu!: () => void;
  let holdGpu = false;
  const gpuGate = new Promise<void>(r => releaseGpu = r);
  const gpuStarted = new Promise<void>(r => enteredGpu = r);
  const worker = {
    translateMany: async (rows: metal.MetalInput[]) => {
      gpuCalls++;
      if (holdGpu) { enteredGpu(); await gpuGate; }
      return rows.map(row => row.ids.slice(0, -1));
    },
    close: mock(async () => {}),
  };
  spyOn(metal, 'createMetalWorker').mockResolvedValue(worker as any);
  spyOn(metal, 'getMetalRuntime').mockReturnValue('/mock-gpu');
  engine.tokenLimit = 20;
  engine.backend = { provider: 'cpu', maxConcurrentRuns: 4 };
  engine.converted = { directory: '.', sourceModel: 'fake', identity: 'test' };
  engine.src = { encode(t: string) { const id = next++; texts.set(id, t); return [id]; }, eos: 0, close() {} };
  engine.dst = { size: 8192, eos: 0, decode: (ids: Iterable<number>) => texts.get([...ids][0])!, close() {} };
  engine.session = {
    run: async (feeds: any) => { cpuCalls++; await new Promise(r => setTimeout(r, 2)); return { Y: { type: 'int64', dims: [2], data: BigInt64Array.of(feeds.X.data[0], 0n), dispose() {} } }; },
    release: mock(async () => {}),
  };
  const doc = Array.from({ length: 16 }, (_, i) => `Sentence ${i}.\n`).join('');
  try {
    expect(await engine.translateAsync(doc)).toBe(doc);
    expect(engine.backend.documentAcceleration.selected).toBe(true);
    const baseGpu = gpuCalls, baseCpu = cpuCalls;
    holdGpu = true;
    const first = engine.translateAsync(doc);
    await gpuStarted;
    const second = engine.translateAsync(doc);
    expect(await second).toBe(doc);
    expect(cpuCalls).toBeGreaterThan(baseCpu);
    releaseGpu();
    expect(await first).toBe(doc);
    expect(gpuCalls).toBe(baseGpu + 1);
  } finally { releaseGpu?.(); await engine.destroy(); }
});

test('TranslateController.translateBatch dispatches batch items via translateBatchWithPivot', async () => {
  const services = await import('../src/services/index.js');
  const seen: string[][] = [];
  spyOn(services, 'translateBatchWithPivot').mockImplementation(async (_f, _t, texts) => {
    seen.push([...texts]);
    return texts.map(t => `tr:${t}`);
  });
  const controller = new TranslateController();
  const response = await controller.translateBatch({ from: 'en', to: 'zh-Hans', texts: ['a', 'b', 'c', 'd'] });
  expect(response.results).toEqual(['tr:a', 'tr:b', 'tr:c', 'tr:d']);
  expect(seen).toEqual([['a', 'b', 'c', 'd']]);
});
