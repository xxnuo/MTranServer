import * as ort from 'onnxruntime-node';
import { existsSync } from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import * as logger from '../logger/index.js';

export type InferenceSession = Pick<ort.InferenceSession, 'inputNames' | 'run' | 'release'>;
export class NativeRunError extends Error {}
interface NativeBinding {
  init(model: string, library: string): Promise<object>;
  run(handle: object, input: BigInt64Array, candidates: BigInt64Array, eos: bigint): Promise<BigInt64Array>;
  close(handle: object): Promise<void>;
}

export async function createNativeSession(model: string): Promise<InferenceSession | undefined> {
  if (!['linux', 'darwin'].includes(process.platform)) return;
  const filename = `onnx-async-${process.platform}-${process.arch}.node`;
  const candidates = [new URL(`./${filename}`, import.meta.url), new URL(`../../dist/${filename}`, import.meta.url)];
  const addon = candidates.find(url => existsSync(url));
  if (!addon) return;
  try {
    const require = createRequire(import.meta.url);
    const binding = require(fileURLToPath(addon)) as NativeBinding;
    const libraryName = process.platform === 'darwin' ? 'libonnxruntime.1.dylib' : 'libonnxruntime.so.1';
    const library = path.resolve(path.dirname(require.resolve('onnxruntime-node')), '../bin/napi-v6', process.platform, process.arch, libraryName);
    const handle = await binding.init(model, library);
    let closed: Promise<void> | undefined;
    return {
      inputNames: ['X', 'eos_id', 'candidate_ids'],
      release: () => closed ??= binding.close(handle),
      run: async feeds => {
        if (closed) throw new Error('ONNX session is closing');
        const input = feeds.X.data, ids = feeds.candidate_ids.data, eos = feeds.eos_id.data;
        if (!(input instanceof BigInt64Array) || !(ids instanceof BigInt64Array) || !(eos instanceof BigInt64Array) || eos.length !== 1)
          throw new Error('Native translation requires int64 input, candidates and EOS');
        let output: BigInt64Array;
        try { output = await binding.run(handle, input, ids, eos[0]); }
        catch (error) {
          throw new NativeRunError(String(error));
        }
        return { Y: new ort.Tensor('int64', output, [output.length]) };
      },
    };
  } catch (error) {
    logger.warn(`Native ONNX parallel execution unavailable; using standard runtime: ${error}`);
    return undefined;
  }
}
