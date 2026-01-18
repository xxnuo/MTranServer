import { describe, expect, test } from 'bun:test';
import { TranslationEngine } from '@/core/engine';

describe('TranslationEngine HTML handling', () => {
  test('long html skips plain text splitting', () => {
    const engine = new TranslationEngine() as any;
    engine.isReady = true;

    let usedLongText = false;
    let usedInternal = false;

    engine._translateLongText = () => {
      usedLongText = true;
      return 'split';
    };

    engine._translateInternal = (text: string, options: { html?: boolean }) => {
      usedInternal = true;
      expect(options.html).toBe(true);
      return text;
    };

    const html = `<p dir="auto">${'Hello world '.repeat(80)}<a href="https://example.com">example</a></p>`;
    const result = engine.translate(html, { html: true });

    expect(result).toBe(html);
    expect(usedInternal).toBe(true);
    expect(usedLongText).toBe(false);
  });
});
