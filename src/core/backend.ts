import * as ort from 'onnxruntime-node';
import { availableParallelism } from 'node:os';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';
import * as logger from '../logger/index.js';

export interface SessionTuning {
  executionMode: 'sequential' | 'parallel';
  interOpNumThreads: number;
  graphOptimizationLevel: 'all' | 'extended';
  enableCpuMemArena: boolean;
  enableMemPattern: boolean;
  spinning: boolean;
  dynamicBlockBase: number;
}
export interface BackendMeasurement {
  provider: string;
  cpuThreads: number;
  tuning?: SessionTuning;
  concurrency?: number;
  medianMs?: number;
  samples?: number;
  requestsPerSecond?: number;
  shortProbeMs?: number;
  skippedReason?: string;
  error?: string;
}
export interface BackendInfo {
  provider: string;
  cpuThreads: number;
  tuning: SessionTuning;
  maxConcurrentRuns: number;
  measurements: BackendMeasurement[];
  fallbackReason?: string;
  selectionCached?: boolean;
}
const defaults: SessionTuning = {
  executionMode: 'sequential', interOpNumThreads: 1, graphOptimizationLevel: 'all',
  enableCpuMemArena: true, enableMemPattern: true, spinning: true, dynamicBlockBase: 0,
};
const selections = new Map<string, { info: BackendInfo; fallbackCpuThreads: number; fallbackCpuTuning: SessionTuning }>();

export function createSession(model: string, provider: string, cpuThreads: number, tuning: SessionTuning = defaults) {
  return ort.InferenceSession.create(model, {
    executionProviders: provider === 'cpu' ? ['cpu'] : [provider, 'cpu'],
    intraOpNumThreads: cpuThreads, interOpNumThreads: tuning.interOpNumThreads,
    executionMode: provider === 'dml' ? 'sequential' : tuning.executionMode,
    graphOptimizationLevel: tuning.graphOptimizationLevel,
    enableCpuMemArena: tuning.enableCpuMemArena,
    enableMemPattern: ['dml', 'webgpu'].includes(provider) ? false : tuning.enableMemPattern,
    extra: { session: {
      dynamic_block_base: String(tuning.dynamicBlockBase),
      intra_op: { allow_spinning: tuning.spinning ? '1' : '0' },
      inter_op: { allow_spinning: tuning.spinning ? '1' : '0' },
    } },
  });
}
async function release(session?: ort.InferenceSession) {
  try { await session?.release(); }
  catch (error) { logger.debug(`ONNX session release failed: ${error}`); }
}

export async function selectBackend(model: string, probe: (session: ort.InferenceSession, short?: boolean) => Promise<void>, cpuThreads?: number, identity?: string) {
  if (cpuThreads !== undefined && (!Number.isInteger(cpuThreads) || cpuThreads < 0)) throw new Error('cpu-threads must be a non-negative integer');
  const cacheKey = identity ? `${identity}:${cpuThreads ?? 'auto'}` : undefined;
  const cached = cacheKey ? selections.get(cacheKey) : undefined;
  if (cached) {
    let session: ort.InferenceSession | undefined;
    try {
      session = await createSession(model, cached.info.provider, cached.info.cpuThreads, cached.info.tuning);
      await probe(session);
      return { ...cached, session, info: { ...cached.info, selectionCached: true } };
    } catch {
      await release(session);
      selections.delete(cacheKey!);
    }
  }
  const budget = availableParallelism();
  const automaticThreads = budget <= 32 ? Array.from({ length: budget }, (_, i) => i + 1)
    : [...new Set([1, 2, 4, 6, ...Array.from({ length: Math.floor(Math.log2(budget)) - 2 }, (_, i) => 2 ** (i + 3)), budget])];
  const threads = cpuThreads === undefined ? [0, ...automaticThreads] : [cpuThreads];
  const measurements: BackendMeasurement[] = [];
  let selected: BackendMeasurement | undefined, cpu: BackendMeasurement | undefined;
  let shortCpuMs = Infinity;
  const measure = async (provider: string, cpuThreads: number, tuning: SessionTuning) => {
    let session: ort.InferenceSession | undefined;
    const measurement: BackendMeasurement = { provider, cpuThreads, tuning: { ...tuning } };
    try {
      session = await createSession(model, provider, cpuThreads, tuning);
      if (provider !== 'cpu' || !cpu) {
        await probe(session, true);
        const started = performance.now();
        await probe(session, true);
        measurement.shortProbeMs = performance.now() - started;
        if (provider === 'cpu') shortCpuMs = measurement.shortProbeMs;
        else if (measurement.shortProbeMs > shortCpuMs * 4) {
          measurement.skippedReason = 'More than four times slower than CPU on the short input';
          return;
        }
      }
      await probe(session);
      if (provider === 'cpu') await probe(session);
      const times: number[] = [];
      let totalMs = 0;
      while (times.length < 5 || (totalMs < 80 && times.length < 15)) {
        const started = performance.now();
        await probe(session);
        const elapsed = performance.now() - started;
        times.push(elapsed); totalMs += elapsed;
        if (provider !== 'cpu' && cpu && elapsed > cpu.medianMs! * 4) break;
      }
      measurement.samples = times.length;
      measurement.medianMs = times.sort((a, b) => a - b)[Math.floor(times.length / 2)];
      const threshold = provider === 'cpu' && cpu?.cpuThreads === cpuThreads ? .9 : .97;
      if (!selected || measurement.medianMs < selected.medianMs! * threshold) selected = measurement;
      if (provider === 'cpu' && (!cpu || measurement.medianMs < cpu.medianMs! * threshold)) cpu = measurement;
    } catch (error) {
      measurement.error = String(error);
      logger.debug(`ONNX backend ${provider} unavailable: ${error}`);
    } finally {
      // Release between candidates so idle thread pools cannot distort the next measurement.
      await release(session);
      measurements.push(measurement);
    }
  };
  await measure('cpu', cpuThreads ?? 1, defaults);
  const require = createRequire(import.meta.url);
  const nativeDir = path.resolve(path.dirname(require.resolve('onnxruntime-node')), '../bin/napi-v6', process.platform, process.arch);
  for (const backend of ort.listSupportedBackends()) {
    if (backend.name === 'cpu') continue;
    if (!backend.bundled && process.platform === 'linux'
      && ['cuda', 'tensorrt'].includes(backend.name)
      && !existsSync(path.join(nativeDir, `libonnxruntime_providers_${backend.name}.so`))) {
      measurements.push({ provider: backend.name, cpuThreads: cpu?.cpuThreads ?? 1, error: 'Optional provider library is not installed' });
      continue;
    }
    await measure(backend.name, cpuThreads ?? 1, {
      ...defaults, ...(['dml', 'webgpu'].includes(backend.name) ? { enableMemPattern: false } : {}),
    });
  }
  for (const count of threads) if (count !== (cpuThreads ?? 1)) await measure('cpu', count, defaults);
  if (cpu && cpuThreads === undefined && budget > 32) {
    const best = cpu.cpuThreads || budget;
    for (const count of new Set([Math.floor(best * .75), Math.ceil(best * 1.25)])) {
      if (count > 0 && count <= budget && !threads.includes(count)) await measure('cpu', count, defaults);
    }
  }
  if (cpu) {
    await measure('cpu', cpu.cpuThreads, { ...cpu.tuning!, dynamicBlockBase: 4 });
    for (const inter of [2, 4].filter(n => n <= budget)) {
      const count = cpuThreads ?? Math.max(1, Math.min(cpu.cpuThreads || budget, Math.floor(budget / inter)));
      await measure('cpu', count, { ...cpu.tuning!, executionMode: 'parallel', interOpNumThreads: inter });
    }
  }
  if (!selected || !cpu) throw new Error(`No usable ONNX backend: ${JSON.stringify(measurements)}`);
  let winner = selected as BackendMeasurement;
  const fallback = cpu as BackendMeasurement;
  let session: ort.InferenceSession | undefined;
  let fallbackReason: string | undefined;
  try {
    try {
      session = await createSession(model, winner.provider, winner.cpuThreads, winner.tuning);
      await probe(session);
    } catch (error) {
      await release(session); session = undefined;
      if (winner.provider === 'cpu') throw error;
      fallbackReason = String(error);
      measurements.push({ provider: winner.provider, cpuThreads: winner.cpuThreads, error: fallbackReason });
      winner = fallback;
      session = await createSession(model, 'cpu', winner.cpuThreads, winner.tuning);
      await probe(session);
    }
    const activeSession = session;
    let maxConcurrentRuns = 1;
    if (winner.provider === 'cpu') {
      const poolSize = Math.max(1, Number.parseInt(process.env.UV_THREADPOOL_SIZE ?? '4', 10) || 4);
      // Runs share this session's native thread pools; they do not each allocate another pool.
      const limit = Math.min(poolSize, budget);
      let bestRate = 1000 / winner.medianMs!;
      for (let concurrency = 2; concurrency <= limit; concurrency++) {
        const times: number[] = [];
        await Promise.all(Array.from({ length: concurrency }, () => probe(activeSession)));
        for (let i = 0; i < 3; i++) {
          const started = performance.now();
          await Promise.all(Array.from({ length: concurrency }, () => probe(activeSession)));
          times.push(performance.now() - started);
        }
        const medianMs = times.sort((a, b) => a - b)[1], requestsPerSecond = concurrency * 1000 / medianMs;
        measurements.push({ ...winner, concurrency, medianMs, requestsPerSecond });
        if (requestsPerSecond > bestRate * 1.03) { bestRate = requestsPerSecond; maxConcurrentRuns = concurrency; }
      }
    }
    const info: BackendInfo = { provider: winner.provider, cpuThreads: winner.cpuThreads, tuning: winner.tuning!, maxConcurrentRuns, measurements,
      ...(fallbackReason ? { fallbackReason } : {}) };
    const result = { info, fallbackCpuThreads: fallback.cpuThreads, fallbackCpuTuning: fallback.tuning! };
    if (cacheKey) {
      if (selections.size >= 32) selections.delete(selections.keys().next().value!);
      selections.set(cacheKey, result);
    }
    logger.info(`ONNX selected ${info.provider}, CPU threads=${info.cpuThreads}, mode=${info.tuning.executionMode}, concurrency=${maxConcurrentRuns}, probe=${winner.medianMs!.toFixed(2)}ms`);
    return { session, ...result };
  } catch (error) { await release(session); throw error; }
}
