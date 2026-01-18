import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import protobuf from 'protobufjs';
import * as ort from 'onnxruntime-node';
import * as backend from '../src/core/backend';
import { TranslationEngine } from '../src/core/engine';
import { Tokenizer } from '../src/core/tokenizer';
import { Shortlist, checksum, vocabularyInfo } from '../src/core/shortlist';
import { Shortlist as ExperimentalShortlist } from '../scripts/onnx-shortlist/shortlist';

let directory: string | undefined;
afterEach(async () => {
  mock.restore();
  if (directory) await rm(directory, { recursive: true, force: true });
  directory = undefined;
});

function vocab(label = 'piece', eos = 24, size = 64) {
  const w = protobuf.Writer.create();
  for (let id = 0; id < size; id++) {
    const piece = protobuf.Writer.create().uint32(10).string(`${label}${id}`)
      .uint32(24).int32([7, 30, 60].includes(id) ? 6 : 1).finish();
    w.uint32(10).bytes(piece);
  }
  return w.uint32(18).bytes(protobuf.Writer.create().uint32(336).int32(eos).finish()).finish();
}

async function fixture() {
  const root = path.resolve(import.meta.dir, '../.tmp');
  await mkdir(root, { recursive: true });
  directory = await mkdtemp(path.join(root, 'shortlist-engine-'));
  const model = { directory, identity: 'shortlist-unit', model: 'mock.onnx',
    sourceVocab: path.join(directory, 'source.spm'), targetVocab: path.join(directory, 'target.spm'), lex: path.join(directory, 'lex.bin') };
  const offsets = Array.from({ length: 65 }, (_, id) => id > 50 ? 1 : 0);
  const lex = Buffer.alloc(48 + offsets.length * 8 + 4);
  [0xF11A48D5013417F5n, 0n, 1n, 50n, 65n, 1n].forEach((n, i) => lex.writeBigUInt64LE(n, i * 8));
  offsets.forEach((n, i) => lex.writeBigUInt64LE(BigInt(n), 48 + i * 8));
  lex.writeUInt32LE(42, 48 + offsets.length * 8);
  lex.writeBigUInt64LE(checksum(lex.subarray(16)), 8);
  await Promise.all([writeFile(model.sourceVocab, vocab()), writeFile(model.targetVocab, vocab()), writeFile(model.lex, lex)]);
  return model;
}

function tokenizers() {
  const src = { size: 64, eos: 24, encode: () => Array.from({ length: 40 }, (_, i) => i < 16 ? 50 : 51), close: mock(() => {}) };
  const dst = { size: 64, eos: 24, decode: () => 'translated', close: mock(() => {}) };
  spyOn(Tokenizer, 'load').mockResolvedValueOnce(src as any).mockResolvedValueOnce(dst as any);
  return { src, dst };
}

function selection(session: any) {
  const tuning = { executionMode: 'sequential' as const, interOpNumThreads: 1, graphOptimizationLevel: 'all' as const,
    enableCpuMemArena: true, enableMemPattern: true, spinning: true, dynamicBlockBase: 0 };
  return { session, info: { provider: 'webgpu', cpuThreads: 1, tuning, maxConcurrentRuns: 1, measurements: [] },
    fallbackCpuThreads: 2, fallbackCpuTuning: tuning };
}

test('shared loader reuses experimental implementation, hashes vocab contents and retains lexicon, EOS and all byte IDs', async () => {
  const model = await fixture();
  expect(Shortlist).toBe(ExperimentalShortlist);
  const shared = await Shortlist.load(model.lex, model.sourceVocab, model.targetVocab);
  expect(shared.candidates([50, 50])).toEqual([0, 1, 7, 24, 30, 42, 50, 60]);
  expect(() => shared.candidates([])).toThrow('source');
  expect(() => shared.candidates([64])).toThrow('source');
  await writeFile(model.sourceVocab, vocab('different'));
  const separate = await Shortlist.load(model.lex, model.sourceVocab, model.targetVocab);
  expect(separate.candidates([50])).toEqual([0, 1, 2, 7, 24, 30, 42, 60]);
  await writeFile(model.sourceVocab, vocab('different', 24, 63));
  await expect(Shortlist.load(model.lex, model.sourceVocab, model.targetVocab)).rejects.toThrow('source lexicon/vocabulary mismatch');
  await writeFile(model.sourceVocab, vocab());
  const corrupt = await readFile(model.lex); corrupt[16] ^= 1;
  await writeFile(model.lex, corrupt);
  await expect(Shortlist.load(model.lex, model.sourceVocab, model.targetVocab)).rejects.toThrow('checksum');
});

test('vocabulary metadata reads non-default EOS and rejects truncated trainer messages', () => {
  expect(vocabularyInfo(vocab())).toEqual({ size: 64, eos: 24, byteIds: [7, 30, 60] });
  expect(() => vocabularyInfo(Uint8Array.of(18, 5, 8, 1))).toThrow();
});

test.each([undefined, ['X', 'eos_id']])('legacy graph with inputNames %j skips lexicon loading and candidate feeds', async inputNames => {
  tokenizers();
  const session = { inputNames, release: mock(async () => {}), run: mock(async (feeds: any) => {
    expect(Object.keys(feeds)).toEqual(['X', 'eos_id']);
    return { Y: { data: BigInt64Array.of(42n, 24n), dispose() {} } };
  }) };
  spyOn(backend, 'selectBackend').mockImplementation(async (_model, probe) => {
    await probe(session as any, true); await probe(session as any);
    return selection(session);
  });
  const engine = new TranslationEngine();
  try {
    await engine.init({ directory: '.', identity: 'old', model: 'mock.onnx', sourceVocab: 'missing-src', targetVocab: 'missing-dst' });
    expect(await engine.translateAsync('text')).toBe('translated');
    expect(session.run).toHaveBeenCalledTimes(4);
  } finally { await engine.destroy(); }
});

test.each(['device lost', 'invalid output', 'no EOS'])('shortlist feeds cover short/long probes, requests and CPU fallback after %s, disposing all candidates', async failure => {
  const model = await fixture(); tokenizers();
  let probing = true;
  const calls: Array<{ input: number[]; candidates: number[]; dispose: ReturnType<typeof spyOn> }> = [];
  const outputDisposals: Array<ReturnType<typeof mock>> = [];
  const session = (provider: string) => ({ inputNames: ['X', 'eos_id', 'candidate_ids'], release: mock(async () => {}), run: mock(async (feeds: any) => {
    expect(feeds.candidate_ids.type).toBe('int64');
    calls.push({ input: Array.from(feeds.X.data, Number), candidates: Array.from(feeds.candidate_ids.data, Number),
      dispose: spyOn(feeds.candidate_ids, 'dispose') });
    if (!probing && provider === 'webgpu' && failure === 'device lost') throw new Error(failure);
    const dispose = mock(() => {}); outputDisposals.push(dispose);
    const data = !probing && provider === 'webgpu' && failure === 'invalid output' ? Float32Array.of(42)
      : probing || (provider === 'webgpu' && failure === 'no EOS') ? BigInt64Array.of(42n) : BigInt64Array.of(42n, 24n);
    return { Y: { data, dispose } };
  }) });
  const gpu = session('webgpu'), cpu = session('cpu');
  spyOn(backend, 'selectBackend').mockImplementation(async (_model, probe) => {
    await probe(gpu as any, true); await probe(gpu as any);
    probing = false;
    return selection(gpu);
  });
  const create = spyOn(ort.InferenceSession, 'create').mockResolvedValue(cpu as any);
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(await engine.translateAsync('text')).toBe('translated');
    expect(engine.backend?.provider).toBe('cpu');
    expect(calls.map(call => call.input.length)).toEqual([17, 17, 33, 41, 41]);
    expect(calls[0].candidates).not.toContain(51);
    expect(calls[2].candidates).toContain(51);
    expect(calls[3].candidates).toEqual(calls[4].candidates);
    for (const call of calls) {
      expect(call.candidates).toEqual([...new Set(call.candidates)].sort((a, b) => a - b));
      for (const id of [7, 24, 30, 42, 60]) expect(call.candidates).toContain(id);
      expect(call.dispose).toHaveBeenCalledTimes(1);
    }
    for (const dispose of outputDisposals) expect(dispose).toHaveBeenCalledTimes(1);
    expect(create.mock.calls[0][1]).toMatchObject({ executionProviders: ['cpu'], intraOpNumThreads: 2 });
    expect(gpu.release).toHaveBeenCalledTimes(1);
  } finally { await engine.destroy(); }
});

test.each(['missing', 'corrupt'])('new graph rejects %s lexicon before inference and failed init can be destroyed without closing tokenizers twice', async kind => {
  const model = await fixture(), { src, dst } = tokenizers();
  src.close.mockImplementationOnce(() => {}).mockImplementation(() => { throw new Error('double delete'); });
  dst.close.mockImplementationOnce(() => {}).mockImplementation(() => { throw new Error('double delete'); });
  if (kind === 'corrupt') await writeFile(model.lex, Buffer.alloc(48));
  const session = { inputNames: ['X', 'eos_id', 'candidate_ids'], run: mock(async () => { throw new Error('must not run'); }) };
  spyOn(backend, 'selectBackend').mockImplementation(async (_model, probe) => {
    await probe(session as any, true);
    return selection(session);
  });
  const engine = new TranslationEngine();
  await expect(engine.init({ ...model, lex: kind === 'missing' ? undefined : model.lex })).rejects.toThrow(kind === 'missing' ? 'ConvertedModel.lex' : 'bad magic');
  await engine.destroy(); await engine.destroy();
  expect(session.run).not.toHaveBeenCalled();
  expect(src.close).toHaveBeenCalledTimes(1);
  expect(dst.close).toHaveBeenCalledTimes(1);
});
