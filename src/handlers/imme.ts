import { Request, Response } from 'express';
import { normalizeLanguageCode } from '../utils/index.js';
import * as logger from '../logger/index.js';
import { translateWithPivot } from '../services/index.js';

interface ImmeTranslation {
  detected_source_lang: string;
  text: string;
}

export function handleImmeTranslate(apiToken: string) {
  return async (req: Request, res: Response) => {
    if (apiToken) {
      const token = req.query.token as string || '';
      if (token !== apiToken) {
        res.status(401).json({ error: 'Unauthorized' });
        return;
      }
    }

    try {
      const { source_lang, target_lang, text_list } = req.body;

      if (!source_lang || !target_lang || !text_list || !Array.isArray(text_list)) {
        res.status(400).json({ error: 'Missing required fields: source_lang, target_lang, text_list' });
        return;
      }

      const sourceLang = normalizeLanguageCode(source_lang);
      const targetLang = normalizeLanguageCode(target_lang);

      logger.debug(`Imme request: ${sourceLang} -> ${targetLang}, count: ${text_list.length}`);

      const translations: ImmeTranslation[] = [];

      for (let i = 0; i < text_list.length; i++) {
        const text = text_list[i];

        if (!text || typeof text !== 'string') {
          logger.warn(`Skipping invalid text at index ${i}: ${typeof text}`);
          translations.push({
            detected_source_lang: source_lang,
            text: '',
          });
          continue;
        }

        logger.debug(
          `Imme translating [${i + 1}/${text_list.length}]: ${sourceLang} -> ${targetLang}, text length: ${text.length}, preview: ${text.substring(0, 50)}`
        );

        let result: string;
        try {
          result = await translateWithPivot(sourceLang, targetLang, text, false);
          logger.debug(`Imme translated [${i + 1}/${text_list.length}] success`);
        } catch (err) {
          logger.error(`Imme translation failed at index ${i} (${sourceLang} -> ${targetLang}): ${err}`);
          result = text;
        }

        translations.push({
          detected_source_lang: source_lang,
          text: result,
        });
      }

      res.json({ translations });
    } catch (error) {
      logger.error(`Imme translation failed: ${error}`);
      res.status(500).json({ error: `Translation failed: ${error}` });
    }
  };
}
