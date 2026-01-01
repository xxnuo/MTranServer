import { Request, Response } from 'express';
import { normalizeLanguageCode } from '../utils/index.js';
import * as logger from '../logger/index.js';
import { translateWithPivot } from '../services/index.js';

interface DeeplTranslation {
  detected_source_language: string;
  text: string;
}

const bcp47ToDeeplLang: Record<string, string> = {
  'no': 'NB',
  'zh-Hans': 'ZH',
  'zh-CN': 'ZH-CN',
  'zh-Hant': 'ZH-TW',
  'zh-TW': 'ZH-TW',
};

function convertBCP47ToDeeplLang(bcp47Lang: string): string {
  return bcp47ToDeeplLang[bcp47Lang] || bcp47Lang.toUpperCase();
}

export function handleDeeplTranslate(apiToken: string) {
  return async (req: Request, res: Response) => {
    if (apiToken) {
      const authHeader = req.headers['authorization'];
      let token = '';

      if (authHeader && typeof authHeader === 'string') {
        if (authHeader.startsWith('DeepL-Auth-Key ')) {
          token = authHeader.replace('DeepL-Auth-Key ', '');
        } else {
          token = authHeader.replace('Bearer ', '');
        }
      } else {
        token = req.query.token as string || '';
      }

      if (token !== apiToken) {
        res.status(401).json({ error: 'Unauthorized' });
        return;
      }
    }

    try {
      const { text, source_lang, target_lang, tag_handling } = req.body;

      if (!text || !target_lang) {
        res.status(400).json({ error: 'Missing required fields: text, target_lang' });
        return;
      }

      const textArray = Array.isArray(text) ? text : [text];
      const sourceLang = source_lang ? normalizeLanguageCode(source_lang) : 'auto';
      const targetLang = normalizeLanguageCode(target_lang);
      const isHTML = tag_handling === 'html' || tag_handling === 'xml';

      const translations: DeeplTranslation[] = [];

      for (let i = 0; i < textArray.length; i++) {
        const result = await translateWithPivot(sourceLang, targetLang, textArray[i], isHTML);

        const detectedLang = source_lang || convertBCP47ToDeeplLang(sourceLang);

        translations.push({
          detected_source_language: detectedLang,
          text: result,
        });
      }

      res.json({ translations });
    } catch (error) {
      logger.error(`DeepL translation failed: ${error}`);
      res.status(500).json({ error: `Translation failed: ${error}` });
    }
  };
}
