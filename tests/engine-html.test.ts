import { describe, expect, test } from 'bun:test';
import { TranslationEngine } from '@/core/engine';

describe('TranslationEngine HTML handling', () => {
  test('plain text html requests fall back to plain text mode', () => {
    const engine = new TranslationEngine() as any;
    engine.isReady = true;

    engine._translateInternal = (text: string, options: { html?: boolean }) => {
      expect(options.html).toBe(false);
      expect(text).toContain('<= 4.4.6');
      expect(text).toContain('&downloadable_file_urls');
      expect(text).toContain('MT_PLACEHOLDER_0_TOKEN');
      return text;
    };

    const advisory = 'Authenticated (admin+) Arbitrary File Download vulnerability discovered in Download Monitor WordPress plugin (versions <= 4.4.6). The plugin allows arbitrary files via the &downloadable_file_urls[0] parameter data.';
    const result = engine.translate(advisory, { html: true });

    expect(result).toBe(advisory);
  });

  test('valid html sanitizes plain text angle brackets and ampersands', () => {
    const engine = new TranslationEngine() as any;
    engine.isReady = true;

    engine._translateInternal = (text: string, options: { html?: boolean }) => {
      expect(options.html).toBe(true);
      expect(text).toContain('&lt;= 4.4.6');
      expect(text).toContain('&amp;downloadable_file_urls<mt0 />');
      return text;
    };

    const html = '<p>Authenticated vulnerability (versions <= 4.4.6) via &downloadable_file_urls[0] and wp-config.php</p>';
    const result = engine.translate(html, { html: true });

    expect(result).toContain('&lt;= 4.4.6');
    expect(result).toContain('&amp;downloadable_file_urls[0]');
  });

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
