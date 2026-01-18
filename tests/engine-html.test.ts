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
});
