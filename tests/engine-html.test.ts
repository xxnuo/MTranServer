import { describe, expect, test } from 'bun:test';
import { TranslationEngine } from '../src/core/engine';

describe('ONNX text handling', () => {
  test('HTML preserves markup, attributes, protected content and escapes translated text', async () => {
    const engine = new TranslationEngine() as any;
    engine.translateText = async (s: string) => s.replace('Hallo', 'Hello & <world>');
    const html = '<p title="Hallo">Hallo <b translate="no">Hallo</b><script>Hallo</script></p>';
    expect(await engine.translateHTML(html)).toBe('<p title="Hallo">Hello &amp; &lt;world&gt; <b translate="no">Hallo</b><script>Hallo</script></p>');
  });
  test('placeholders survive without being passed to the model', async () => {
    const engine = new TranslationEngine() as any;
    engine.translatePlain = async (s: string) => s.toUpperCase();
    expect(await engine.translateText('hello {0} and [1]')).toBe('HELLO {0} AND [1]');
  });
  test('long text is split even when token count fits, without dropping sentences', async () => {
    const engine = new TranslationEngine() as any;
    engine.tokenLimit = 20;
    engine.src = { encode: (s: string) => [1], eos: 0 };
    engine.dst = { eos: 0, decode: () => 'Hello!' };
    engine.session = { run: async () => ({Y:{data:BigInt64Array.of(1n,0n),dispose(){}}}) };
    expect(await engine.translatePlain('Hallo! '.repeat(10))).toBe('Hello! '.repeat(10));
  });
  test('closing waits for queued requests and rejects new ones', async () => {
    const engine = new TranslationEngine() as any;
    let release = false;
    engine.session = { release: async () => { release = true; } };
    engine.translateText = async (s: string) => { await new Promise(r => setTimeout(r, 10)); expect(release).toBe(false); return s; };
    const request = engine.translateAsync('text');
    const closing = engine.destroy();
    await expect(engine.translateAsync('late')).rejects.toThrow('closing');
    expect(await request).toBe('text'); await closing; expect(release).toBe(true);
  });
  test('CPU requests respect the measured concurrency and drain after an error before closing', async () => {
    const engine = new TranslationEngine() as any;
    let active = 0, peak = 0, released = false;
    engine.backend = { provider: 'cpu', maxConcurrentRuns: 2 };
    engine.session = { release: async () => { expect(active).toBe(0); released = true; } };
    engine.translateText = async (text: string) => {
      active++; peak = Math.max(peak, active);
      try {
        await new Promise(resolve => setTimeout(resolve, 5));
        expect(released).toBe(false);
        if (text === '2') throw new Error('request failed');
        return text;
      } finally { active--; }
    };
    const requests = Promise.allSettled(Array.from({ length: 8 }, (_, i) => engine.translateAsync(String(i))));
    const closing = engine.destroy();
    expect(engine.busy).toBe(true);
    const results = await requests;
    await closing;
    expect(peak).toBe(2);
    expect(results.filter(result => result.status === 'rejected')).toHaveLength(1);
    expect(results.filter(result => result.status === 'fulfilled').map(result => result.value)).toEqual(['0', '1', '3', '4', '5', '6', '7']);
    expect(engine.busy).toBe(false);
    expect(released).toBe(true);
  });
});
