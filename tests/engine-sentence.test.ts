import {expect, test} from 'bun:test';
import {TranslationEngine} from '../src/core/engine';

function setup(limit = 4) {
  const engine = new TranslationEngine() as any;
  let active = 0, peak = 0, calls = 0, released = false;
  const texts = new Map<number, string>();
  let next = 1;
  engine.tokenLimit = 20;
  engine.backend = {provider: 'cpu', maxConcurrentRuns: limit};
  engine.src = {encode: (text: string) => {const id = next++; texts.set(id, text); return [id];}, eos: 0, close() {expect(active).toBe(0);}};
  engine.dst = {eos: 0, decode: ([id]: number[]) => texts.get(id)!, close() {expect(active).toBe(0);}};
  engine.session = {
    run: async (feeds: any) => {
      active++; calls++; peak = Math.max(peak, active);
      const id = feeds.X.data[0];
      try {
        await new Promise(resolve => setTimeout(resolve, Number(id) % 3));
        expect(released).toBe(false);
        return {Y: {data: BigInt64Array.of(id, 0n), dispose() {}}};
      } finally {active--;}
    },
    release: async () => {expect(active).toBe(0); released = true;},
  };
  return {engine, stats: () => ({active, peak, calls, released})};
}

for (const limit of [1, 2, 4]) test(`long text preserves order and whitespace with inference limit ${limit}`, async () => {
  const {engine, stats} = setup(limit);
  const input = '  First sentence.\nSecond sentence.  Third sentence. Fourth sentence.  ';
  const request = engine.translateAsync(input);
  const close = engine.destroy();
  expect(await request).toBe(input);
  await close;
  expect(stats()).toEqual({active: 0, peak: limit, calls: 4, released: true});
});

test('recursive splitting and protected parts preserve text within the shared budget', async () => {
  const {engine, stats} = setup(4);
  const input = '  ' + 'abcdefghij'.repeat(25) + ' {0} ' + 'klmnopqrst'.repeat(25) + ' [1]  ';
  const requests = Promise.all(Array.from({length: 5}, () => engine.translateAsync(input)));
  const close = engine.destroy();
  expect(await requests).toEqual(Array(5).fill(input));
  await close;
  expect(stats().peak).toBeLessThanOrEqual(4);
  expect(stats().released).toBe(true);
});

test('a split failure stops new sentences and drains active work before closing', async () => {
  const {engine} = setup(2);
  let finish!: () => void, notify!: () => void, closed = false, calls = 0;
  const gate = new Promise<void>(resolve => finish = resolve);
  const entered = new Promise<void>(resolve => notify = resolve);
  engine.session.run = async () => {
    calls++;
    if (calls === 1) {notify(); throw new Error('sentence failed');}
    await gate;
    return {Y: {data: BigInt64Array.of(1n, 0n), dispose() {}}};
  };
  const result = engine.translateAsync('First sentence. Second sentence. Third sentence. Fourth sentence.').catch((error: Error) => error.message);
  await entered;
  const close = engine.destroy().then(() => closed = true);
  await new Promise(resolve => setTimeout(resolve, 0));
  expect(calls).toBe(2); expect(closed).toBe(false);
  finish(); expect(await result).toBe('sentence failed'); await close;
  expect(calls).toBe(2); expect(closed).toBe(true);
});

test('competing requests keep sentence workers serial within each request', async () => {
  const {engine} = setup(4);
  let calls = 0, finish!: () => void;
  const gate = new Promise<void>(resolve => finish = resolve);
  engine.session.run = async (feeds: any) => {
    calls++; await gate;
    return {Y: {data: BigInt64Array.of(feeds.X.data[0], 0n), dispose() {}}};
  };
  const input = 'First sentence. Second sentence. Third sentence. Fourth sentence.';
  const a = engine.translateAsync(input), b = engine.translateAsync(input);
  await Promise.resolve(); await Promise.resolve();
  expect(calls).toBe(2); finish();
  expect(await a).toBe(input); expect(await b).toBe(input);
  await engine.destroy(); expect(calls).toBe(8);
});
