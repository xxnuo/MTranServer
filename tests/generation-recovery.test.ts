import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { TranslationEngine } from '../src/core/engine';
import * as logger from '../src/logger';

afterEach(() => mock.restore());

function engineWithModel(shouldExhaust: (text: string) => boolean) {
  spyOn(logger, 'warn').mockImplementation(() => {});
  const attempts: string[] = [];
  const disposals: ReturnType<typeof mock>[] = [];
  const engine = new TranslationEngine({ sourceLang: 'en', targetLang: 'en' }) as any;
  engine.src = { encode: (s: string) => Array.from(s, c => c.codePointAt(0)!), eos: 2 };
  engine.dst = { eos: 2, decode: (ids: number[]) => String.fromCodePoint(...ids) };
  engine.session = {
    run: async ({ X }: any) => {
      const text = String.fromCodePoint(...Array.from(X.data as BigInt64Array, Number).slice(0, -1));
      attempts.push(text);
      const tokens = Array.from(shouldExhaust(text) ? 'BROKEN' : text.toUpperCase(), c => BigInt(c.codePointAt(0)!));
      if (!shouldExhaust(text)) tokens.push(2n);
      const dispose = mock(() => {});
      disposals.push(dispose);
      return { Y: { data: BigInt64Array.from(tokens), dispose } };
    },
  };
  return { engine, attempts, disposals };
}

test('generation limit retries smaller natural fragments and returns only completed translations', async () => {
  const { engine, attempts, disposals } = engineWithModel(s => s.length > 12);
  expect(await engine.translateAsync('  alpha beta gamma delta  ')).toBe('  ALPHA BETA GAMMA DELTA  ');
  expect(attempts.length).toBeGreaterThan(1);
  expect(attempts.length).toBeLessThanOrEqual(65);
  for (const dispose of disposals) expect(dispose).toHaveBeenCalledTimes(1);
});

test('pure separators and numbers are preserved without invoking the model', async () => {
  const { engine, attempts } = engineWithModel(() => true);
  for (const text of ['--------------------', '1,234.56%', '。。。。。。', '2026-11-03']) {
    expect(await engine.translateAsync(text)).toBe(text);
  }
  expect(attempts).toHaveLength(0);
});

test('line endings, tabs and placeholders survive generation recovery', async () => {
  const { engine } = engineWithModel(s => s.length > 12);
  const text = 'alpha beta gamma delta\r\n--------------------\nhello\t{0} [1]';
  expect(await engine.translateAsync(text)).toBe('ALPHA BETA GAMMA DELTA\r\n--------------------\nHELLO\t{0} [1]');
});

test('irreducible generation failure remains an explicit error with bounded work', async () => {
  const { engine, attempts } = engineWithModel(() => true);
  await expect(engine.translateAsync('alpha beta gamma delta')).rejects.toThrow('Translation exceeded generation limit');
  expect(attempts.length).toBeGreaterThan(1);
  expect(attempts.length).toBeLessThanOrEqual(65);
});

test('retry attempts are shared by sibling fragments instead of resetting on each split', async () => {
  const { engine, attempts } = engineWithModel(s => Array.from(s).length > 2);
  const text = Array.from({ length: 64 }, (_, i) => 'a' + String.fromCodePoint(0x300 + i)).join('');
  await expect(engine.translateAsync(text)).rejects.toThrow('Translation exceeded generation limit');
  expect(attempts).toHaveLength(65);
});

test('identical completed fragments are reused only inside the current request', async () => {
  const { engine, attempts } = engineWithModel(s => s.length > 12);
  for (let i = 0; i < 2; i++) expect(await engine.translateAsync('alpha beta alpha beta')).toBe('ALPHA BETA ALPHA BETA');
  expect(attempts.filter(s => s === 'alpha beta')).toHaveLength(2);
  expect(attempts).toHaveLength(4);
});

test('backend faults are not retried as generation failures', async () => {
  const { engine, attempts } = engineWithModel(() => false);
  const run = mock(async () => { attempts.push('failed'); throw new Error('device unavailable'); });
  engine.session.run = run;
  await expect(engine.translateAsync('alpha beta gamma delta')).rejects.toThrow('device unavailable');
  expect(run).toHaveBeenCalledTimes(1);
});

test('recovered English fragments remain separated after sentence punctuation', async () => {
  const { engine } = engineWithModel(s => s.length > 2);
  engine.dst.decode = () => 'Done.';
  expect(await engine.translateAsync('甲乙丙丁')).toBe('Done. Done.');
});

test('grapheme recovery keeps combining marks and surrogate pairs together', async () => {
  const { engine, attempts } = engineWithModel(s => Array.from(s).length > 2);
  engine.options.targetLang = 'zh';
  expect(await engine.translateAsync('a\u0301b\u0301')).toBe('A\u0301B\u0301');
  expect(attempts.slice(1)).toEqual(['a\u0301', 'b\u0301']);
});
