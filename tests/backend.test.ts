import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import * as ort from 'onnxruntime-node';
import { availableParallelism } from 'node:os';
import { createSession, selectBackend } from '../src/core/backend';
import { TranslationEngine } from '../src/core/engine';
import { Tokenizer } from '../src/core/tokenizer';

afterEach(() => mock.restore());

test('CPU-only mode skips GPU discovery and does not reuse a cached accelerator', async () => {
  const previous = process.env.MT_CPU_ONLY;
  const backends = spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => ({
    provider: options.executionProviders[0], release: async () => {},
  }) as any);
  const probe = async (session: any) => { clock += session.provider === 'cpu' ? 5 : .1; };
  try {
    process.env.MT_CPU_ONLY = 'false';
    const gpu = await selectBackend('model.onnx', probe, 1, 'cpu-only-test');
    expect(gpu.info.provider).toBe('webgpu');
    expect(gpu.info.maxConcurrentRuns).toBe(1);
    await gpu.session.release();
    backends.mockClear();
    process.env.MT_CPU_ONLY = 'true';
    const cpu = await selectBackend('model.onnx', probe, 1, 'cpu-only-test');
    expect(cpu.info.provider).toBe('cpu');
    expect(cpu.info.measurements.every(m => m.provider === 'cpu')).toBe(true);
    expect(backends).not.toHaveBeenCalled();
    await cpu.session.release();
  } finally {
    if (previous === undefined) delete process.env.MT_CPU_ONLY;
    else process.env.MT_CPU_ONLY = previous;
  }
});

test('calibration accepts bounded generation without EOS but real translation rejects incomplete output', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  spyOn(Tokenizer, 'load').mockResolvedValue({ encode: () => [10, 11], eos: 0, close() {} } as any);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  const inputs: number[][] = [];
  const dispose = mock(() => {}), release = mock(async () => {});
  spyOn(ort.InferenceSession, 'create').mockImplementation(async () => ({
    release, run: async ({ X }: any) => {
      inputs.push(Array.from(X.data, Number));
      clock += 5;
      return { Y: { data: BigInt64Array.of(12n, 13n), dispose } };
    },
  }) as any);
  const engine = new TranslationEngine();
  try {
    await engine.init({ directory: '.', model: 'model.onnx', sourceVocab: 'src', targetVocab: 'dst', identity: 'bounded-probe' }, { 'cpu-threads': 1 });
    expect(engine.backend?.provider).toBe('cpu');
    expect(inputs.every(ids => JSON.stringify(ids) === '[10,11,0]')).toBe(true);
    await expect(engine.translateAsync('test')).rejects.toThrow('Translation exceeded generation limit');
    expect(dispose).toHaveBeenCalledTimes(inputs.length);
  } finally { await engine.destroy(); }
});

test('calibration still rejects malformed model output and releases its tensors', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  spyOn(Tokenizer, 'load').mockResolvedValue({ encode: () => [10], eos: 0, close() {} } as any);
  const dispose = mock(() => {});
  spyOn(ort.InferenceSession, 'create').mockResolvedValue({
    release: async () => {}, run: async () => ({ Y: { data: Float32Array.of(1), dispose } }),
  } as any);
  const engine = new TranslationEngine();
  try {
    await expect(engine.init({ directory: '.', model: 'model.onnx', sourceVocab: 'src', targetVocab: 'dst', identity: 'malformed-probe' }, { 'cpu-threads': 1 }))
      .rejects.toThrow('Invalid translation output');
    expect(dispose).toHaveBeenCalled();
  } finally { await engine.destroy(); }
});

test('automatic selection measures CPU and accelerator candidates and releases slower sessions', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  const sessions: any[] = [];
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => {
    const session = { provider: options.executionProviders[0], threads: options.intraOpNumThreads, release: mock(async () => {}) };
    sessions.push(session);
    return session as any;
  });
  const selected = await selectBackend('model.onnx', async (session: any) => {
    clock += session.provider === 'cpu' ? 200 / Math.max(1, session.threads) : 0.001;
  });
  expect(selected.info.provider).toBe('webgpu');
  expect(selected.info.measurements.filter(m => m.provider === 'cpu').length).toBeGreaterThan(0);
  expect(selected.info.measurements.some(m => m.cpuThreads === 0)).toBe(true);
  expect(selected.info.measurements.some(m => m.cpuThreads === availableParallelism())).toBe(true);
  for (const session of sessions) expect(session.release).toHaveBeenCalledTimes(session === selected.session ? 0 : 1);
  await selected.session.release();
});

test('unloadable and nonfunctional accelerators are rejected while CPU remains available', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([
    { name: 'cpu', bundled: true }, { name: 'coreml', bundled: true }, { name: 'webgpu', bundled: true },
  ]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  const release = mock(async () => {});
  const create = spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => {
    const provider = options.executionProviders[0];
    if (provider === 'coreml') throw new Error('No device');
    return { provider, release } as any;
  });
  const selected = await selectBackend('model.onnx', async (session: any) => {
    if (session.provider === 'webgpu') throw new Error('Unsupported operator');
    clock += 5;
  }, 1);
  expect(selected.info.provider).toBe('cpu');
  expect(selected.info.measurements.filter(m => m.error)).toHaveLength(2);
  expect(release).toHaveBeenCalledTimes(create.mock.calls.length - 2);
  await selected.session.release();
});

test('model identity reuses backend selection and failed cached candidates trigger reselection', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  const create = spyOn(ort.InferenceSession, 'create').mockResolvedValue({ release: async () => {} } as any);
  let probes = 0;
  spyOn(performance, 'now').mockImplementation(() => probes);
  const probe = async () => { probes++; };
  const first = await selectBackend('model.onnx', probe, 1, 'cached-test-model');
  const calibrationProbes = probes;
  expect(calibrationProbes).toBeGreaterThan(4);
  await first.session.release();
  const cached = await selectBackend('model.onnx', probe, 1, 'cached-test-model');
  expect(probes).toBe(calibrationProbes + 1);
  expect(cached.info.selectionCached).toBe(true);
  expect(cached.info.tuning).toEqual(first.info.tuning);
  expect(first.info.maxConcurrentRuns).toBe(1);
  expect(cached.info.maxConcurrentRuns).toBe(1);
  expect(create.mock.calls.at(-1)?.[1]?.executionMode).toBe(first.info.tuning.executionMode);
  await cached.session.release();
  create.mockRejectedValueOnce(new Error('Cached device unavailable'));
  const recovered = await selectBackend('model.onnx', probe, 1, 'cached-test-model');
  expect(probes).toBe(calibrationProbes * 2 + 1);
  expect(recovered.info.selectionCached).not.toBe(true);
  expect(recovered.info.maxConcurrentRuns).toBe(1);
  await recovered.session.release();
  const changedModel = await selectBackend('model.onnx', probe, 1, 'different-model-identity');
  expect(probes).toBe(calibrationProbes * 3 + 1);
  await changedModel.session.release();
});

test('native session settings retain CPU auto threads and DirectML safety constraints', async () => {
  const create = spyOn(ort.InferenceSession, 'create').mockResolvedValue({ release: async () => {} } as any);
  await createSession('model.onnx', 'dml', 0, {
    executionMode: 'parallel', interOpNumThreads: 2, graphOptimizationLevel: 'all',
    enableCpuMemArena: true, enableMemPattern: true, spinning: false, dynamicBlockBase: 4,
  });
  expect(create.mock.calls[0][1]).toMatchObject({
    executionProviders: ['dml', 'cpu'], intraOpNumThreads: 0, executionMode: 'sequential', enableMemPattern: false,
    extra: { session: { dynamic_block_base: '4', intra_op: { allow_spinning: '0' } } },
  });
  await expect(selectBackend('model.onnx', async () => {}, -1)).rejects.toThrow('non-negative integer');
});

test.each(['1', '4', '128'])('calibration keeps faster native settings without concurrent probes with UV_THREADPOOL_SIZE=%s', async poolSize => {
  const previous = process.env.UV_THREADPOOL_SIZE;
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => ({
    cost: options.extra.session.dynamic_block_base === '4' ? 5 : 20,
    release: async () => {},
  }) as any);
  let active = 0, peak = 0;
  try {
    process.env.UV_THREADPOOL_SIZE = poolSize;
    const selected = await selectBackend('model.onnx', async (session: any) => {
      active++;
      peak = Math.max(peak, active);
      try {
        clock += session.cost;
        await Promise.resolve();
      } finally { active--; }
    }, 2);
    expect(selected.info.tuning.dynamicBlockBase).toBe(4);
    expect(selected.info.tuning.graphOptimizationLevel).toBe('all');
    expect(selected.info.tuning.spinning).toBe(true);
    expect(selected.fallbackCpuTuning).toEqual(selected.info.tuning);
    expect(selected.info.maxConcurrentRuns).toBe(1);
    expect(peak).toBe(1);
    expect(selected.info.measurements.every(m => m.concurrency === undefined && m.requestsPerSecond === undefined)).toBe(true);
    await selected.session.release();
  } finally {
    if (previous === undefined) delete process.env.UV_THREADPOOL_SIZE;
    else process.env.UV_THREADPOOL_SIZE = previous;
  }
});

test('clearly slower accelerators stop after the short probe', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0, longGpuProbes = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => ({
    provider: options.executionProviders[0], release: async () => {},
  }) as any);
  const selected = await selectBackend('model.onnx', async (session: any, short) => {
    if (session.provider === 'webgpu' && !short) longGpuProbes++;
    clock += session.provider === 'webgpu' ? 100 : 5;
  }, 1);
  expect(selected.info.provider).toBe('cpu');
  expect(selected.info.measurements.find(m => m.provider === 'webgpu')).toMatchObject({ shortProbeMs: 100, skippedReason: expect.any(String) });
  expect(longGpuProbes).toBe(0);
  await selected.session.release();
});

test('an accelerator that disappears after calibration falls back to the measured CPU settings', async () => {
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0, gpuCreates = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => {
    const provider = options.executionProviders[0];
    if (provider === 'webgpu' && ++gpuCreates === 2) throw new Error('device disappeared');
    return { provider, release: async () => {} } as any;
  });
  const selected = await selectBackend('model.onnx', async (session: any) => { clock += session.provider === 'cpu' ? 5 : .1; }, 1);
  expect(selected.info.provider).toBe('cpu');
  expect(selected.info.tuning).toEqual(selected.fallbackCpuTuning);
  expect(selected.info.fallbackReason).toContain('device disappeared');
  expect(selected.info.maxConcurrentRuns).toBe(1);
  await selected.session.release();
});

test.each(['device lost', 'invalid output'])('accelerator %s retries the request on CPU and releases failed resources', async failure => {
  const engine = new TranslationEngine() as any;
  const dispose = mock(() => {}), release = mock(async () => {});
  engine.src = { encode: () => [10], eos: 0, close() {} };
  engine.dst = { eos: 0, decode: () => 'Hello', close() {} };
  engine.modelPath = 'model.onnx';
  engine.fallbackCpuThreads = 2;
  engine.backend = { provider: 'webgpu', cpuThreads: 1, measurements: [] };
  engine.session = { release, run: async () => {
    if (failure === 'device lost') throw new Error(failure);
    return { Y: { data: Float32Array.of(1), dispose } };
  } };
  const create = spyOn(ort.InferenceSession, 'create').mockResolvedValue({
    release: async () => {}, run: async () => ({ Y: { data: BigInt64Array.of(1n, 0n), dispose } }),
  } as any);
  expect(await engine.translateAsync('Hallo')).toBe('Hello');
  expect(engine.backend.provider).toBe('cpu');
  expect(engine.backend.fallbackReason).toContain(failure === 'device lost' ? failure : 'Invalid translation output');
  expect(create.mock.calls[0][1]?.executionProviders).toEqual(['cpu']);
  expect(create.mock.calls[0][1]?.intraOpNumThreads).toBe(2);
  expect(release).toHaveBeenCalledTimes(1);
  expect(dispose).toHaveBeenCalledTimes(failure === 'device lost' ? 1 : 2);
  await engine.destroy();
});

test('a native document backend calibrates its CPU graph without loading legacy GPU providers', async () => {
  const backends = spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }, { name: 'webgpu', bundled: true }]);
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock);
  spyOn(ort.InferenceSession, 'create').mockImplementation(async (_model: any, options: any) => ({ provider: options.executionProviders[0], release: async () => {} }) as any);
  const selected = await selectBackend('model.onnx', async () => { clock += 5; }, 1, 'native-document-cpu-graph', true);
  try {
    expect(selected.info.provider).toBe('cpu');
    expect(selected.info.measurements.every(m => m.provider === 'cpu')).toBe(true);
    expect(backends).not.toHaveBeenCalled();
  } finally { await selected.session.release(); }
});
