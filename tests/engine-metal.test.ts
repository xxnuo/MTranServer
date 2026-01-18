import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { TranslationEngine } from '../src/core/engine';
import * as metal from '../src/core/metal';

const deviceTest = test.skipIf(process.platform !== 'darwin' || process.arch !== 'arm64');
afterEach(() => mock.restore());

function setup(delay = 0) {
  const engine = new TranslationEngine() as any;
  let next = 1, cpuCalls = 0, gpuCalls = 0, closed = false;
  const texts = new Map<number, string>();
  const worker = {
    translateMany: async (rows: metal.MetalInput[]) => {
      gpuCalls++;
      if (delay) await new Promise(resolve => setTimeout(resolve, delay));
      return rows.map(row => row.ids.slice(0, -1));
    },
    close: mock(async () => { closed = true; }),
  };
  spyOn(metal, 'createMetalWorker').mockResolvedValue(worker as any);
  engine.tokenLimit = 20;
  engine.backend = { provider: 'cpu', maxConcurrentRuns: 4 };
  engine.converted = { directory: '.', sourceModel: 'fake', identity: 'test' };
  engine.src = { encode(text: string) { const id = next++; texts.set(id, text); return [id]; }, eos: 0, close() {} };
  engine.dst = { eos: 0, decode: ([id]: number[]) => texts.get(id)!, close() {} };
  engine.session = {
    run: async (feeds: any) => { cpuCalls++; await new Promise(resolve => setTimeout(resolve, 2)); return { Y: { data: BigInt64Array.of(feeds.X.data[0], 0n), dispose() {} } }; },
    release: mock(async () => {}),
  };
  const text = '  ' + Array.from({ length: 16 }, (_, i) => `Sentence ${i}.\n`).join('') + '  ';
  return { engine, worker, text, stats: () => ({ cpuCalls, gpuCalls, closed }) };
}

deviceTest('Metal document calibration preserves whitespace, caches decisions and leaves short requests on CPU', async () => {
  const { engine, text, stats } = setup();
  try {
    expect(await engine.translateAsync(text)).toBe(text);
    expect(engine.backend.documentAcceleration.selected).toBe(true);
    const calibrated = stats();
    expect(await engine.translateAsync(text)).toBe(text);
    expect(stats().cpuCalls).toBe(calibrated.cpuCalls);
    expect(stats().gpuCalls).toBe(calibrated.gpuCalls + 1);
    expect(await engine.translateAsync('short')).toBe('short');
    expect(stats().gpuCalls).toBe(calibrated.gpuCalls + 1);
  } finally { await engine.destroy(); }
});

deviceTest('slower Metal closes the worker and retains the CPU decision', async () => {
  const { engine, text, stats, worker } = setup(25);
  try {
    expect(await engine.translateAsync(text)).toBe(text);
    expect(engine.backend.documentAcceleration.selected).toBe(false);
    expect(worker.close).toHaveBeenCalledTimes(1);
    const previous = stats().gpuCalls;
    expect(await engine.translateAsync(text)).toBe(text);
    expect(stats().gpuCalls).toBe(previous);
  } finally { await engine.destroy(); }
});

deviceTest('Metal failure after calibration falls back to CPU and disables further GPU attempts', async () => {
  const { engine, worker, text, stats } = setup();
  const original = worker.translateMany; let calls = 0;
  worker.translateMany = async rows => { if (++calls >= 4) throw new Error('GPU lost'); return original(rows); };
  try {
    expect(await engine.translateAsync(text)).toBe(text);
    expect(engine.backend.documentAcceleration.error).toContain('GPU lost');
    const previous = stats().gpuCalls;
    expect(await engine.translateAsync(text)).toBe(text);
    expect(stats().gpuCalls).toBe(previous);
    expect(worker.close).toHaveBeenCalledTimes(1);
  } finally { await engine.destroy(); }
});

deviceTest('destroy drains accepted Metal work before closing its worker', async () => {
  const { engine, worker, text, stats } = setup();
  const original = worker.translateMany; let calls = 0;
  let release!: () => void, entered!: () => void;
  const gate = new Promise<void>(resolve => release = resolve), started = new Promise<void>(resolve => entered = resolve);
  worker.translateMany = async rows => { if (++calls === 4) { entered(); await gate; } return original(rows); };
  const request = engine.translateAsync(text);
  await started;
  const close = engine.destroy();
  expect(stats().closed).toBe(false);
  release(); expect(await request).toBe(text); await close;
  expect(worker.close).toHaveBeenCalledTimes(1);
});

deviceTest('a competing request invalidates Metal calibration even when it ends before the probe', async () => {
  const { engine, worker, text } = setup();
  const original = worker.translateMany; let calls = 0;
  let release!: () => void, entered!: () => void;
  const gate = new Promise<void>(resolve => release = resolve), started = new Promise<void>(resolve => entered = resolve);
  worker.translateMany = async rows => { if (++calls === 1) { entered(); await gate; } return original(rows); };
  try {
    const request = engine.translateAsync(text); await started;
    expect(await engine.translateAsync('short')).toBe('short'); release();
    expect(await request).toBe(text);
    expect(engine.backend.documentAcceleration).toBeUndefined();
    expect(await engine.translateAsync(text)).toBe(text);
    expect(engine.backend.documentAcceleration.selected).toBe(true);
  } finally { release(); await engine.destroy(); }
});

deviceTest('Metal preserves placeholders around separate document fragments and recursive splits', async () => {
  const { engine, text } = setup();
  const input = text + ' {0} [1] ' + text + 'abcdefghij'.repeat(40);
  try { expect(await engine.translateAsync(input)).toBe(input); expect(engine.backend.documentAcceleration.selected).toBe(true); }
  finally { await engine.destroy(); }
});
