import { setConfig, getConfig, Config } from '@/config/index.js';
import { initRecords, downloadModel } from '@/models/index.js';
import { translateWithPivot, cleanupAllEngines } from '@/services/index.js';
import { detectLanguage } from '@/services/detector.js';
import { normalizeLanguageCode } from '@/utils/index.js';

export interface MTranConfig extends Partial<Config> {}

export class MTran {
  constructor(config?: MTranConfig) {
    if (config) {
      setConfig(config);
    }
  }

  /**
   * Initialize the translation engine (load records, etc.)
   */
  async init(): Promise<void> {
    await initRecords();
  }

  /**
   * Translate text
   * @param from Source language code (or 'auto')
   * @param to Target language code
   * @param text Text to translate
   * @param html Whether the text is HTML (default: false)
   */
  async translate(from: string, to: string, text: string, html: boolean = false): Promise<string> {
    const normalizedFrom = from === 'auto' ? 'auto' : normalizeLanguageCode(from);
    const normalizedTo = normalizeLanguageCode(to);
    return translateWithPivot(normalizedFrom, normalizedTo, text, html);
  }

  /**
   * Detect language of the text
   * @param text Text to analyze
   */
  async detect(text: string): Promise<string | null> {
    return detectLanguage(text);
  }

  /**
   * Ensure model is downloaded for a language pair
   * @param from Source language
   * @param to Target language
   */
  async downloadModel(from: string, to: string): Promise<void> {
    const normalizedFrom = normalizeLanguageCode(from);
    const normalizedTo = normalizeLanguageCode(to);
    await downloadModel(normalizedTo, normalizedFrom);
  }

  /**
   * Clean up all loaded engines and release memory
   */
  async close(): Promise<void> {
    cleanupAllEngines();
  }

  /**
   * Get current configuration
   */
  getConfig(): Config {
    return getConfig();
  }
}
