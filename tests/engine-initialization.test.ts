import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import * as ort from 'onnxruntime-node';
import { TranslationEngine } from '../src/core/engine';
import { Tokenizer } from '../src/core/tokenizer';
import type { ConvertedModel } from '../src/converter/cache';

afterEach(() => mock.restore());

function mockRuntime(output: () => BigInt64Array) {
  let clock = 0;
  spyOn(performance, 'now').mockImplementation(() => clock++);
  spyOn(ort, 'listSupportedBackends').mockReturnValue([{ name: 'cpu', bundled: true }]);
  const inputs: bigint[][] = [];
  const release = mock(async () => {});
  spyOn(ort.InferenceSession, 'create').mockImplementation(async () => ({
    release,
    run: async ({ X }: { X: ort.Tensor }) => {
      inputs.push(Array.from(X.data as BigInt64Array));
      return { Y: new ort.Tensor('int64', output(), [output().length]) };
    },
  }) as unknown as ort.InferenceSession);
  const close = mock(() => {});
  spyOn(Tokenizer, 'load').mockResolvedValue({
    encode: () => [10, 11], eos: 2, decode: () => 'All right', close,
  } as unknown as Tokenizer);
  return { inputs, release, close };
}

const model = (identity: string) => ({ model: 'model.onnx', sourceVocab: 'src.spm', targetVocab: 'dst.spm', identity }) as ConvertedModel;

test('cold initialization does not repeat a short first request to fill calibration inputs', async () => {
  const { inputs } = mockRuntime(() => BigInt64Array.of(10n, 2n));
  const engine = new TranslationEngine();
  try {
    await engine.init(model('short-first-request'), { 'cpu-threads': 1 }, '好的');
    expect(inputs.length).toBeGreaterThan(0);
    for (const input of inputs) expect(input).toEqual([10n, 11n, 2n]);
    expect(await engine.translateAsync('好的')).toBe('All right');
  } finally { await engine.destroy(); }
});

test('calibration accepts bounded generation but actual requests still reject missing EOS', async () => {
  const { release } = mockRuntime(() => BigInt64Array.of(10n, 10n));
  const engine = new TranslationEngine();
  try {
    await engine.init(model('bounded-calibration'), { 'cpu-threads': 1 }, '哈哈哈哈');
    await expect(engine.translateAsync('哈哈哈哈')).rejects.toThrow('Translation exceeded generation limit');
  } finally { await engine.destroy(); }
  expect(release).toHaveBeenCalled();
});

test('initialization still rejects invalid backend output and closes tokenizers', async () => {
  const { close } = mockRuntime(() => BigInt64Array.of(2n));
  spyOn(ort.InferenceSession, 'create').mockResolvedValue({
    release: async () => {},
    run: async () => ({ Y: new ort.Tensor('float32', Float32Array.of(1), [1]) }),
  } as unknown as ort.InferenceSession);
  const engine = new TranslationEngine();
  await expect(engine.init(model('invalid-calibration'), { 'cpu-threads': 1 }, '好的')).rejects.toThrow('Invalid translation output');
  expect(close).toHaveBeenCalledTimes(2);
});
