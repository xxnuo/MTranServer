import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import * as ort from 'onnxruntime-node';
import * as backend from '../src/core/backend';
import * as native from '../src/core/native';
import * as cache from '../src/converter/ssru-cache';
import { TranslationEngine } from '../src/core/engine';
import { Tokenizer } from '../src/core/tokenizer';
import { Shortlist } from '../src/core/shortlist';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import protobuf from 'protobufjs';
import schema from '../src/converter/schema.json';

const tuning = { executionMode: 'sequential', interOpNumThreads: 1, graphOptimizationLevel: 'all',
  enableCpuMemArena: true, enableMemPattern: true, spinning: true, dynamicBlockBase: 0 } as const;
const model = { directory: '.', identity: 'fast-identity', model: 'ordinary.onnx', sourceModel: 'source.bin',
  sourceVocab: 'src', targetVocab: 'dst', lex: 'lex' };
const custom = { model: 'independent-ssru.onnx', identity: 'ssru-cache-identity' };
const oldSwitch = process.env.MT_NATIVE_SSRU;
afterEach(() => {
  mock.restore();
  if (oldSwitch === undefined) delete process.env.MT_NATIVE_SSRU;
  else process.env.MT_NATIVE_SSRU = oldSwitch;
});
const output = (ids = [7n, 0n]) => ({ Y: new ort.Tensor('int64', BigInt64Array.from(ids), [ids.length]) });
const malformedKinds = ['rank', 'type', 'empty', 'length', 'negative', 'vocab', 'unsafe', 'after-eos', 'missing', 'shape'] as const;
function malformed(kind: typeof malformedKinds[number], inputLength: number): any {
  if (kind === 'rank') return { Y: new ort.Tensor('int64', BigInt64Array.of(7n, 0n), [1, 2]) };
  if (kind === 'type') return { Y: new ort.Tensor('float32', Float32Array.of(7, 0), [2]) };
  if (kind === 'empty') return output([]);
  if (kind === 'length') return output(Array(2 + 3 * inputLength).fill(0n));
  if (kind === 'missing') return {};
  if (kind === 'shape') return { Y: { type: 'int64', dims: [3], data: BigInt64Array.of(7n, 0n), dispose() {} } };
  return output(kind === 'after-eos' ? [7n, 0n, 64n] : [kind === 'negative' ? -1n : kind === 'vocab' ? 64n : 2n ** 63n - 1n, 0n]);
}
function deferred() {
  let resolve!: () => void;
  const promise = new Promise<void>(r => { resolve = r; });
  return { promise, resolve };
}
function setup(provider = 'cpu', shortlist = true) {
  process.env.MT_NATIVE_SSRU = '1';
  const original = { inputNames: shortlist ? ['X', 'eos_id', 'candidate_ids'] : ['X', 'eos_id'],
    release: mock(async () => {}), run: mock(async (_feeds: any) => output()) };
  const candidate = { ...original, release: mock(async () => {}), run: mock(async (_feeds: any) => output()) };
  const src = { size: 64, encode: () => Array.from({ length: 40 }, (_, i) => i + 2), eos: 0, close: mock(() => {}) };
  const dst = { ...src, decode: () => 'result', close: mock(() => {}) };
  spyOn(Tokenizer, 'load').mockResolvedValueOnce(src as any).mockResolvedValueOnce(dst as any);
  spyOn(Shortlist, 'load').mockResolvedValue({ candidates: () => [0, 2, 7, 8] } as any);
  const select = spyOn(backend, 'selectBackend').mockImplementation(async (file, probe) => {
    expect(file).toBe(model.model);
    await probe(original as any);
    return { session: original, info: { provider, cpuThreads: 2, tuning, maxConcurrentRuns: 1, measurements: [] },
      fallbackCpuThreads: 2, fallbackCpuTuning: tuning } as any;
  });
  const capability = spyOn(native, 'hasNativeSsru').mockReturnValue(true);
  const convert = spyOn(cache, 'ensureNativeSsru').mockResolvedValue(custom);
  const create = spyOn(native, 'createNativeSession').mockResolvedValue(candidate as any);
  return { original, candidate, src, dst, select, capability, convert, create };
}

test.each([undefined, '1', 'true', 'false', ''])('Fast CPU selects SSRU by default (switch=%j)', async flag => {
  const { capability, convert, create } = setup();
  if (flag === undefined) delete process.env.MT_NATIVE_SSRU; else process.env.MT_NATIVE_SSRU = flag;
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(capability).toHaveBeenCalledTimes(1); expect(convert).toHaveBeenCalledWith(model);
    expect(create).toHaveBeenCalledWith(custom.model, ['X', 'eos_id', 'candidate_ids'], 1, true);
    expect(engine.backend?.runtime).toBe('native-ssru');
  } finally { await engine.destroy(); }
});

test('SSRU switch 0 leaves the existing Fast native path unchanged', async () => {
  const { capability, convert, create } = setup();
  process.env.MT_NATIVE_SSRU = '0';
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(capability).not.toHaveBeenCalled(); expect(convert).not.toHaveBeenCalled();
    expect(create).toHaveBeenCalledWith(model.model, ['X', 'eos_id', 'candidate_ids'], 1);
    expect(engine.backend?.runtime).toBe('native-async');
  } finally { await engine.destroy(); }
});

test.each([['cuda', true], ['dml', true], ['cpu', false]] as const)('SSRU requires CPU and shortlist (%s, %s)', async (provider, shortlist) => {
  const { convert, capability } = setup(provider, shortlist);
  delete process.env.MT_NATIVE_SSRU;
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(convert).not.toHaveBeenCalled(); expect(capability).not.toHaveBeenCalled();
    expect(engine.backend?.runtime).not.toBe('native-ssru');
  } finally { await engine.destroy(); }
});

test.each([false, true])('missing SSRU capability falls back before conversion (missing addon=%s)', async missingAddon => {
  const { convert, capability, create, original } = setup();
  capability.mockReturnValue(false);
  if (missingAddon) create.mockResolvedValue(undefined);
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(convert).not.toHaveBeenCalled();
    expect(create).toHaveBeenCalledWith(model.model, ['X', 'eos_id', 'candidate_ids'], 1);
    expect(engine.backend?.runtime).toBe(missingAddon ? undefined : 'native-async');
    expect(engine.backend?.fallbackReason).toContain('SSRU addon unavailable');
    expect(await engine.translateAsync('hello')).toBe('result');
    if (missingAddon) expect(original.release).not.toHaveBeenCalled();
  } finally { await engine.destroy(); }
});

test('supported SSRU is selected after ordinary Fast and successful probes, keeping the public model identity', async () => {
  const { original, candidate, convert, capability, create } = setup();
  const lengths: number[] = [];
  candidate.run.mockImplementation(async feeds => { lengths.push(feeds.X.data.length); return output(); });
  const engine = new TranslationEngine();
  try {
    await engine.init(model, { 'cpu-threads': 2, 'max-concurrent-runs': 2 });
    expect(convert).toHaveBeenCalledWith(model);
    expect(create).toHaveBeenCalledWith(custom.model, ['X', 'eos_id', 'candidate_ids'], 2, true);
    expect(engine.backend?.runtime).toBe('native-ssru'); expect(engine.identity).toBe(model.identity);
    expect(engine.backend?.cpuThreads).toBe(2); expect(engine.backend?.maxConcurrentRuns).toBeLessThanOrEqual(2);
    expect(original.release).toHaveBeenCalledTimes(1); expect(capability).toHaveBeenCalledTimes(1);
    expect(lengths).toEqual([17, 33, 41]);
    expect(await engine.translateAsync('hello')).toBe('result');
  } finally { await engine.destroy(); }
  expect(candidate.release).toHaveBeenCalledTimes(1);
});

test('SSRU defaults to one CPU thread independently of ordinary graph tuning', async () => {
  const { create } = setup(); const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(create).toHaveBeenCalledWith(custom.model, ['X', 'eos_id', 'candidate_ids'], 1, true);
    expect(engine.backend?.cpuThreads).toBe(1);
  } finally { await engine.destroy(); }
});

test.each(malformedKinds)('SSRU init rejects malformed %s output and retains ordinary Fast', async kind => {
  const { candidate, original } = setup(); const engine = new TranslationEngine();
  candidate.run.mockImplementation(async feeds => malformed(kind, feeds.X.data.length));
  try {
    await engine.init(model);
    expect(engine.backend?.runtime).toBeUndefined(); expect(original.release).not.toHaveBeenCalled();
    expect(engine.backend?.fallbackReason).toContain('Native SSRU validation failed');
    expect(candidate.release).toHaveBeenCalledTimes(1);
    expect(await engine.translateAsync('hello')).toBe('result');
  } finally { await engine.destroy(); }
});

test('SSRU full-sentence probe validates output independently of bounded timing probes', async () => {
  const { candidate } = setup(); const engine = new TranslationEngine();
  candidate.run.mockImplementation(async feeds => feeds.X.data.length === 41 ? malformed('vocab', 41) : output());
  try { await engine.init(model); expect(engine.backend?.runtime).toBeUndefined(); }
  finally { await engine.destroy(); }
  expect(candidate.release).toHaveBeenCalledTimes(1);
});

test.each([...malformedKinds, 'fault'] as const)('initialized SSRU %s fault falls back to ordinary Fast and drains destroy', async kind => {
  const { candidate, original, src, dst } = setup(); const engine = new TranslationEngine();
  await engine.init(model, { 'max-concurrent-runs': 2 });
  expect(engine.backend?.runtime).toBe('native-ssru');
  const entered = deferred(), resume = deferred();
  const tensors: any[] = [];
  candidate.run.mockImplementation(async feeds => {
    entered.resolve(); await resume.promise;
    if (kind === 'fault') throw new Error('unexpected binding failure');
    const result = malformed(kind, feeds.X.data.length);
    for (const tensor of Object.values(result) as any[]) tensors.push(spyOn(tensor, 'dispose'));
    return result;
  });
  const fallback = { ...original, run: mock(async () => output()), release: mock(async () => {}) };
  const create = spyOn(backend, 'createSession').mockResolvedValue(fallback as any);
  const requests = Array.from({ length: 4 }, () => engine.translateAsync('hello'));
  await entered.promise; const close = engine.destroy(); resume.resolve();
  expect(await Promise.all(requests)).toEqual(Array(4).fill('result')); await close;
  expect(create).toHaveBeenCalledTimes(1); expect(create).toHaveBeenCalledWith(model.model, 'cpu', 2, tuning);
  expect(engine.backend?.runtime).toBeUndefined(); expect(engine.backend?.fallbackReason).toContain(kind === 'fault' ? 'binding failure' : 'Invalid translation output');
  expect(candidate.release).toHaveBeenCalledTimes(1); expect(fallback.release).toHaveBeenCalledTimes(1);
  expect(src.close).toHaveBeenCalledTimes(1); expect(dst.close).toHaveBeenCalledTimes(1);
  for (const dispose of tensors) expect(dispose).toHaveBeenCalledTimes(1);
});

test('a valid native output at the generation bound without EOS remains a normal generation error', async () => {
  const { candidate } = setup(); const engine = new TranslationEngine(); await engine.init(model);
  const create = spyOn(backend, 'createSession');
  candidate.run.mockImplementation(async feeds => output(Array(1 + 3 * feeds.X.data.length).fill(7n)));
  try {
    await expect(engine.translateAsync('hello')).rejects.toThrow('Translation exceeded generation limit');
    expect(create).not.toHaveBeenCalled(); expect(engine.backend?.runtime).toBe('native-ssru');
  } finally { await engine.destroy(); }
});

test('an old SSRU result stays native after another request has published Fast fallback', async () => {
  const { candidate, original } = setup(); const engine = new TranslationEngine();
  await engine.init(model); engine.backend!.maxConcurrentRuns = 2;
  const both = deferred(), drain = deferred(), retiring = deferred(), drained = deferred(); let calls = 0;
  candidate.run.mockImplementation(async feeds => {
    if (++calls === 1) { await both.promise; throw new native.NativeRunError('first run failed'); }
    both.resolve(); await drain.promise; drained.resolve(); return malformed('rank', feeds.X.data.length);
  });
  candidate.release.mockImplementation(async () => { retiring.resolve(); await drained.promise; });
  const fallback = { ...original, run: mock(async () => output()), release: mock(async () => {}) };
  const create = spyOn(backend, 'createSession').mockResolvedValue(fallback as any);
  const requests = [engine.translateAsync('first'), engine.translateAsync('second')];
  await retiring.promise;
  expect(engine.backend?.runtime).toBeUndefined();
  const close = engine.destroy(); drain.resolve();
  expect(await Promise.all(requests)).toEqual(['result', 'result']); await close;
  expect(create).toHaveBeenCalledTimes(1); expect(fallback.run).toHaveBeenCalledTimes(2);
  expect(candidate.release).toHaveBeenCalledTimes(1);
});

test('queued SSRU reference executing on ordinary Fast classifies malformed output as ordinary', async () => {
  const { candidate, original } = setup(); const engine = new TranslationEngine(); await engine.init(model);
  const state = engine as any;
  engine.backend!.maxConcurrentRuns = 1; state.inferRunning = 1;
  const input = new ort.Tensor('int64', BigInt64Array.of(2n, 0n), [2]);
  const eos = new ort.Tensor('int64', BigInt64Array.of(0n), [1]);
  const pending = state.infer(candidate, input, eos).catch((error: Error) => error);
  const actual = { ...original, release: mock(async () => {}), run: mock(async () => malformed('rank', 2)) };
  state.session = actual;
  state.inferWaiters.shift()!();
  try {
    const error = await pending;
    expect(error.message).toBe('Invalid translation output'); expect(error).not.toBeInstanceOf(native.NativeRunError);
    expect(actual.run).toHaveBeenCalledTimes(1); expect(engine.backend?.runtime).toBe('native-ssru');
  } finally { input.dispose(); eos.dispose(); await candidate.release(); await engine.destroy(); }
});

test.each(['conversion', 'init-unavailable', 'init-error', 'probe', 'empty', 'invalid', 'full-sentence-no-eos'])('SSRU %s failure retains the selected ordinary graph', async failure => {
  const { original, candidate, convert, create } = setup();
  if (failure === 'conversion') convert.mockRejectedValue(new Error('conversion failed'));
  if (failure === 'init-unavailable') create.mockResolvedValue(undefined);
  if (failure === 'init-error') create.mockRejectedValue(new Error('init failed'));
  if (failure === 'probe') candidate.run.mockRejectedValue(new native.NativeRunError('probe failed'));
  if (failure === 'empty') candidate.run.mockImplementation(async () => output([]));
  if (failure === 'invalid') candidate.run.mockImplementation(async () => ({ Y: new ort.Tensor('float32', Float32Array.of(7), [1]) }) as any);
  if (failure === 'full-sentence-no-eos') candidate.run.mockImplementation(async () => output([7n]));
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(engine.backend?.runtime).toBeUndefined(); expect(original.release).not.toHaveBeenCalled();
    expect(await engine.translateAsync('hello')).toBe('result');
    if (failure === 'conversion') expect(create).not.toHaveBeenCalled();
    if (['probe', 'empty', 'invalid', 'full-sentence-no-eos'].includes(failure)) expect(candidate.release).toHaveBeenCalledTimes(1);
  } finally { await engine.destroy(); }
  expect(original.release).toHaveBeenCalledTimes(1);
});

test.each([false, true])('bounded probes allow no EOS and full-sentence completion follows Fast (Fast lacks EOS=%s)', async lacksEos => {
  const { original, candidate } = setup();
  original.run.mockImplementation(async feeds => output(lacksEos || feeds.X.data.length < 41 ? [7n] : [7n, 0n]));
  candidate.run.mockImplementation(async feeds => output(lacksEos || feeds.X.data.length < 41 ? [8n] : [8n, 0n]));
  const engine = new TranslationEngine();
  try {
    await engine.init(model);
    expect(engine.backend?.runtime).toBe('native-ssru');
    if (lacksEos) await expect(engine.translateAsync('hello')).rejects.toThrow('generation limit');
    else expect(await engine.translateAsync('hello')).toBe('result');
  } finally { await engine.destroy(); }
});

test('enabled SSRU run failure recreates only ordinary Fast and drains accepted work during close', async () => {
  const { original, candidate, src, dst } = setup();
  const engine = new TranslationEngine();
  await engine.init(model, { 'max-concurrent-runs': 2 });
  const entered = deferred(), resume = deferred(); let active = 0;
  candidate.run.mockImplementation(async () => {
    active++; entered.resolve(); await resume.promise; active--;
    throw new native.NativeRunError('SSRU runtime fault');
  });
  const fallback = { ...original, release: mock(async () => { expect(active).toBe(0); }), run: mock(async () => output()) };
  const create = spyOn(backend, 'createSession').mockResolvedValue(fallback as any);
  const requests = Array.from({ length: 8 }, () => engine.translateAsync('hello'));
  await entered.promise;
  const closing = engine.destroy();
  await expect(engine.translateAsync('late')).rejects.toThrow('closing');
  expect(candidate.release).not.toHaveBeenCalled(); expect(src.close).not.toHaveBeenCalled();
  resume.resolve();
  expect(await Promise.all(requests)).toEqual(Array(8).fill('result')); await closing;
  expect(create).toHaveBeenCalledTimes(1);
  expect(create).toHaveBeenCalledWith(model.model, 'cpu', 2, tuning);
  expect(candidate.release).toHaveBeenCalledTimes(1); expect(fallback.release).toHaveBeenCalledTimes(1);
  expect(src.close).toHaveBeenCalledTimes(1); expect(dst.close).toHaveBeenCalledTimes(1);
  expect(engine.backend?.runtime).toBeUndefined(); expect(engine.backend?.maxConcurrentRuns).toBe(1);
});

test('queued inferences holding retired SSRU use the replacement while an old run drains', async () => {
  const { original, candidate } = setup(); const engine = new TranslationEngine();
  await engine.init(model, { 'max-concurrent-runs': 1 });
  const entered = deferred(), fail = deferred(); let calls = 0;
  candidate.run.mockImplementation(async () => {
    calls++; entered.resolve(); await fail.promise; throw new native.NativeRunError('retire SSRU');
  });
  const fallback = { ...original, release: mock(async () => {}), run: mock(async () => output()) };
  spyOn(backend, 'createSession').mockResolvedValue(fallback as any);
  const first = engine.translateAsync('hello'); await entered.promise;
  const feeds = { X: new ort.Tensor('int64', BigInt64Array.of(2n, 0n), [2]),
    eos_id: new ort.Tensor('int64', BigInt64Array.of(0n), [1]),
    candidate_ids: new ort.Tensor('int64', BigInt64Array.of(0n, 2n, 7n), [3]) };
  // Hold one engine slot until fallback publishes, reproducing a queued stale session reference.
  (engine as any).inferRunning++;
  const queued = (engine as any).runInference(candidate, feeds);
  fail.resolve();
  while (!(engine as any).fallback) await Promise.resolve();
  await (engine as any).fallback;
  (engine as any).inferRunning--;
  (engine as any).inferWaiters.shift()?.();
  try {
    const result = await queued; result.outputs.Y.dispose(); expect(result.wasNative).toBe(false);
    expect(await first).toBe('result'); expect(calls).toBe(1);
    expect(fallback.run).toHaveBeenCalledTimes(2);
  } finally { Object.values(feeds).forEach(t => t.dispose()); await engine.destroy(); }
});

test.each(['conversion', 'init', 'probe'])('destroy waits for SSRU %s before closing the published session', async phase => {
  const { original, candidate, convert, create } = setup();
  const entered = deferred(), resume = deferred();
  if (phase === 'conversion') convert.mockImplementation(async () => { entered.resolve(); await resume.promise; return custom; });
  if (phase === 'init') create.mockImplementation(async () => { entered.resolve(); await resume.promise; return candidate as any; });
  if (phase === 'probe') candidate.run.mockImplementation(async () => { entered.resolve(); await resume.promise; return output(); });
  const engine = new TranslationEngine(); const init = engine.init(model); await entered.promise;
  const close = engine.destroy(); expect(engine.destroy()).toBe(close);
  expect(original.release).not.toHaveBeenCalled(); expect(candidate.release).not.toHaveBeenCalled();
  resume.resolve(); await init; await close;
  expect(candidate.release).toHaveBeenCalledTimes(1); expect(original.release).toHaveBeenCalledTimes(1);
});

test('parallel SSRU requests own independent mutable input, EOS, candidate and output buffers', async () => {
  const { candidate } = setup(); const engine = new TranslationEngine();
  await engine.init(model, { 'max-concurrent-runs': 2 });
  const feeds: any[] = [], outputs: BigInt64Array[] = [];
  candidate.run.mockImplementation(async input => {
    for (const previous of feeds) for (const name of ['X', 'eos_id', 'candidate_ids'])
      expect(input[name].data.buffer).not.toBe(previous[name]);
    feeds.push(Object.fromEntries(Object.entries(input).map(([name, tensor]) => [name, (tensor as ort.Tensor).data.buffer])));
    await Promise.resolve(); const result = output(); outputs.push(result.Y.data as BigInt64Array); return result;
  });
  try {
    expect(await Promise.all(Array.from({ length: 8 }, () => engine.translateAsync('hello')))).toEqual(Array(8).fill('result'));
    expect(new Set(outputs.map(value => value.buffer)).size).toBe(8);
  } finally { await engine.destroy(); }
});

test('native wrapper requires SSRU version exactly 1 before init and preserves legacy calls', async () => {
  const source = await readFile(new URL('../src/core/native.ts', import.meta.url), 'utf8');
  const code = source.replace("import { existsSync } from 'node:fs';", 'const existsSync = () => true;')
    .replace("import { createRequire } from 'node:module';", 'const createRequire = () => globalThis.ssruRequire;')
    .replace("import * as logger from '../logger/index.js';", 'const logger = {warn(){}};')
    .replaceAll('import.meta.url', JSON.stringify(import.meta.url));
  const moduleCode = new Bun.Transpiler({ loader: 'ts' }).transformSync(code);
  const script = `
    import assert from 'node:assert/strict';
    for (const version of [undefined, 0, '1', 1, 2]) {
      let initialized = 0, closed = 0;
      globalThis.ssruRequire = Object.assign(() => ({ssruVersion:version, init:async()=>{initialized++;return {};}, close:async()=>{closed++;}}), {resolve:()=>'/fake/onnxruntime-node/index.js'});
      const native = await import('data:text/javascript;base64,' + Buffer.from(${JSON.stringify(moduleCode)} + '\\n// version ' + typeof version + version).toString('base64'));
      assert.equal(native.hasNativeSsru(), version === 1);
      const session = await native.createNativeSession('custom.onnx',['X','eos_id','candidate_ids'],1,true);
      assert.equal(!!session, version === 1); assert.equal(initialized, version === 1 ? 1 : 0);
      if (session) { await session.release(); await session.release(); assert.equal(closed,1); }
      const ordinary = await native.createNativeSession('ordinary.onnx',['X','eos_id'],1);
      assert(ordinary); await ordinary.release();
    }
  `;
  const child = Bun.spawn([process.execPath, '--eval', script], { cwd: path.resolve(import.meta.dir, '..'), stdout: 'pipe', stderr: 'pipe' });
  const [out, err, exit] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
  expect(exit, out + err).toBe(0);
});

test.skipIf(!['linux', 'darwin'].includes(process.platform))('enabled SSRU addon recovers from lifecycle faults and drains success/failure during close', async () => {
  const root = path.resolve(import.meta.dir, '..');
  const compiler = (process.env.CXX ? [process.env.CXX] : ['c++', 'g++', 'clang++'])
    .find(command => spawnSync(command, ['--version'], { stdio: 'ignore' }).status === 0);
  const include = [process.env.NODE_INCLUDE_DIR, '/usr/include/node', '/usr/local/include/node',
    '/opt/homebrew/include/node', path.resolve(path.dirname(process.execPath), '../include/node')]
    .find(directory => directory && existsSync(path.join(directory, 'node_api.h')));
  if (!compiler || !include) throw new Error('SSRU lifecycle test requires C++ compiler and Node headers');
  await mkdir(path.join(root, '.tmp'), { recursive: true });
  const directory = await mkdtemp(path.join(root, '.tmp/ssru-lifecycle-test-'));
  try {
    const binary = path.join(directory, 'ssru-instrumented.node'), model = path.join(directory, 'custom.onnx');
    const build = Bun.spawn([compiler, '-std=c++20', '-O1', '-shared', '-fPIC', '-pthread', '-I', include,
      '-I', path.join(root, 'native/onnx-async'), '-DMTRAN_NATIVE_SSRU', '-DONNX_ASYNC_TEST',
      path.join(root, 'native/onnx-async/addon.cc'), path.join(root, 'native/onnx-async/ssru_op.cc'), path.join(root, 'native/ssru/ssru.cc'),
      ...(process.platform === 'darwin' ? ['-undefined', 'dynamic_lookup'] : ['-ldl']), '-o', binary],
    { stdout: 'pipe', stderr: 'pipe' });
    const [out, err, exit] = await Promise.all([new Response(build.stdout).text(), new Response(build.stderr).text(), build.exited]);
    expect(exit, out + err).toBe(0);
    const initializers: any[] = [];
    const add = (name: string, dims: number[], fill = 0) => initializers.push({ name, dims, dataType: 1,
      rawData: Buffer.from(new Float32Array(dims.reduce((a, b) => a * b, 1)).fill(fill).buffer) });
    add('Wemb', [12, 4]); add('decoder_ff_logit_out_b', [1, 12]);
    for (const name of ['rnn_W', 'rnn_Wf', 'context_Wq', 'context_Wo', 'ffn_W1']) add('decoder_l1_' + name, [4, 4]);
    for (const name of ['rnn_bf', 'context_bq', 'context_bo', 'ffn_b1']) add('decoder_l1_' + name, [1, 4]);
    for (const name of ['rnn_ffn_ln', 'context_Wo_ln', 'ffn_ffn_ln']) {
      add('decoder_l1_' + name + '_scale', [1, 4], 1); add('decoder_l1_' + name + '_bias', [1, 4]);
    }
    const names = initializers.map(t => t.name).sort();
    add('K', [1, 1, 4, 2]); add('V', [1, 1, 2, 4]);
    const value = (name: string, size: number | string) => ({ name, type: { tensorType: { elemType: 7,
      shape: { dim: [typeof size === 'number' ? { dimValue: size } : { dimParam: size }] } } } });
    const type = protobuf.Root.fromJSON(schema).lookupType('onnx.ModelProto');
    await writeFile(model, type.encode(type.create({ irVersion: 10, opsetImport: [{ domain: '', version: 11 }, { domain: 'com.mtran', version: 1 }],
      graph: { name: 'ssru-lifecycle', initializer: initializers,
        input: [value('X', 2), value('eos_id', 1), value('candidate_ids', 'c')], output: [value('Y', 'n')],
        node: [{ name: 'decode', opType: 'SsruDecode', domain: 'com.mtran', input: ['X', 'eos_id', 'candidate_ids', 'K', 'V', ...names], output: ['Y'],
          attribute: [...Object.entries({ dim: 4, heads: 1, depth: 1, ffn_depth: 1 }).map(([name, i]) => ({ name, type: 2, i })),
            { name: 'weight_names', type: 8, strings: names.map(name => Buffer.from(name)) }] }] } })).finish());
    const library = path.join(path.dirname(require.resolve('onnxruntime-node/package.json')), 'bin/napi-v6', process.platform, process.arch,
      process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1');
    const script = `
      const assert=require('node:assert/strict'), addon=require(${JSON.stringify(binary)});
      const model=${JSON.stringify(model)}, library=${JSON.stringify(library)};
      (async()=>{
        assert.equal(addon.ssruVersion,1);
        const handle=await addon.init(model,library,1);
        const run=()=>addon.run(handle,BigInt64Array.of(2n,0n),BigInt64Array.of(0n,2n),0n);
        assert.deepEqual(await run(),BigInt64Array.of(0n));
        for(const method of ['init','run','close']) {
          addon._testConfigure('promise');
          assert.throws(method==='init'?()=>addon.init(model,library,1):method==='run'?run:()=>addon.close(handle));
          assert.deepEqual(await run(),BigInt64Array.of(0n));
        }
        for(const fault of ['async_work','queue']) {
          addon._testConfigure(fault); await assert.rejects(addon.init(model,library,1));
          addon._testConfigure(fault); await assert.rejects(run());
          assert.deepEqual(await run(),BigInt64Array.of(0n));
        }
        let settled=0;
        const jobs=Array.from({length:32},(_,i)=>{
          const ids=BigInt64Array.of(i%5===0?99n:0n,2n);
          return addon.run(handle,BigInt64Array.of(2n,0n),ids,0n).then(
            result=>{settled++;assert.notEqual(i%5,0);assert.deepEqual(result,BigInt64Array.of(0n));},
            error=>{settled++;assert.equal(i%5,0);assert.match(error.message,/candidate/);}
          );
        });
        const close=addon.close(handle).then(()=>assert.equal(settled,32));
        await assert.rejects(run(),/closing|closed/);
        await Promise.all([...jobs,close]); await addon.close(handle);
        const fresh=await addon.init(model,library,1); await addon.close(fresh);
      })().catch(error=>{console.error(error);process.exitCode=1;});
    `;
    const child = Bun.spawn(['node', '--unhandled-rejections=strict', '--eval', script], { cwd: root, stdout: 'pipe', stderr: 'pipe' });
    const [stdout, stderr, code] = await Promise.all([new Response(child.stdout).text(), new Response(child.stderr).text(), child.exited]);
    expect(code, stdout + stderr).toBe(0);
  } finally { await rm(directory, { recursive: true, force: true }); }
}, 60000);
