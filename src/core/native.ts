import * as ort from 'onnxruntime-node';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import * as logger from '../logger/index.js';

export type InferenceSession = Pick<ort.InferenceSession, 'inputNames' | 'run' | 'release'> & {
  runRaw?(input: BigInt64Array, candidates: BigInt64Array | null, eos: bigint): Promise<BigInt64Array>;
  runBatch?(inputs: BigInt64Array[], candidates: (BigInt64Array | null)[], eos: bigint): Promise<BigInt64Array[]>;
};
export class NativeRunError extends Error {}
interface NativeBinding {
  ssruVersion?: number;
  init(model: string, library: string, cpuThreads: number): Promise<object>;
  run(handle: object, input: BigInt64Array, candidates: BigInt64Array | null, eos: bigint): Promise<BigInt64Array>;
  runBatch?(handle: object, inputs: BigInt64Array[], candidates: (BigInt64Array | null)[], eos: bigint): Promise<BigInt64Array[]>;
  close(handle: object): Promise<void>;
}

let loadedBinding: NativeBinding | undefined;
function loadBinding(): NativeBinding | undefined {
  if (loadedBinding) return loadedBinding;
  if (!['win32', 'linux', 'darwin'].includes(process.platform) || !['x64', 'arm64'].includes(process.arch)) return;
  const filename = `onnx-async-${process.platform}-${process.arch}.node`;
  const candidates = [new URL(`./${filename}`, import.meta.url), new URL(`../../dist/${filename}`, import.meta.url)];
  const addon = candidates.find(url => existsSync(url));
  if (!addon) return;
  try {
    const require = createRequire(import.meta.url);
    return loadedBinding = require(fileURLToPath(addon)) as NativeBinding;
  } catch (error) {
    logger.warn(`Native ONNX binding unavailable; using standard runtime: ${error}`);
  }
}

export function hasNativeSsru() { return loadBinding()?.ssruVersion === 1; }
export function hasNativeAddon() { return !!loadBinding(); }

export async function createNativeSession(model: string, inputNames: readonly string[], cpuThreads: number, requireSsru = false): Promise<InferenceSession | undefined> {
  const binding = loadBinding();
  if (!binding || (requireSsru && binding.ssruVersion !== 1)) return;
  try {
    const require = createRequire(import.meta.url);
    const libraryName = process.platform === 'win32' ? 'onnxruntime.dll'
      : process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1';
    const library = path.resolve(path.dirname(require.resolve('onnxruntime-node')), '../bin/napi-v6', process.platform, process.arch, libraryName);
    const handle = await binding.init(model, library, cpuThreads);
    let closed: Promise<void> | undefined;
    const runRaw = async (input: BigInt64Array, candidates: BigInt64Array | null, eos: bigint) => {
      if (closed) throw new Error('ONNX session is closing');
      try { return await binding.run(handle, input, candidates, eos); }
      catch (error) { throw new NativeRunError(String(error)); }
    };
    const runBatch = async (inputs: BigInt64Array[], candidates: (BigInt64Array | null)[], eos: bigint): Promise<BigInt64Array[]> => {
      if (closed) throw new Error('ONNX session is closing');
      if (inputs.length === 0) return [];
      if (inputs.length !== candidates.length) throw new Error('Batch inputs and candidates length mismatch');
      if (typeof binding.runBatch === 'function') {
        try { return await binding.runBatch(handle, inputs, candidates, eos); }
        catch (error) { throw new NativeRunError(String(error)); }
      }
      return Promise.all(inputs.map((input, i) => runRaw(input, candidates[i] ?? null, eos)));
    };
    return {
      inputNames: [...inputNames],
      release: () => closed ??= binding.close(handle),
      runRaw,
      runBatch,
      run: async feeds => {
        if (closed) throw new Error('ONNX session is closing');
        const input = feeds.X.data, ids = inputNames.includes('candidate_ids') ? feeds.candidate_ids?.data : null, eos = feeds.eos_id.data;
        if (!(input instanceof BigInt64Array) || (ids !== null && !(ids instanceof BigInt64Array)) || !(eos instanceof BigInt64Array) || eos.length !== 1)
          throw new Error('Native translation requires int64 input, candidates and EOS');
        const output = await runRaw(input, ids, eos[0]);
        return { Y: new ort.Tensor('int64', output, [output.length]) };
      },
    };
  } catch (error) {
    logger.warn(`Native ONNX parallel execution unavailable; using standard runtime: ${error}`);
    return undefined;
  }
}
