import { Request, Response } from 'express';
import { normalizeLanguageCode } from '@/utils/index.js';
import * as logger from '@/logger/index.js';
import { translateWithPivot } from '@/services/index.js';

export async function handleTranslate(req: Request, res: Response) {
  try {
    const { from, to, text, html } = req.body;

    if (!from || !to || !text) {
      res.status(400).json({ error: 'Missing required fields: from, to, text' });
      return;
    }

    const normalizedFrom = normalizeLanguageCode(from);
    const normalizedTo = normalizeLanguageCode(to);

    logger.debug(
      `Translation request: ${normalizedFrom} -> ${normalizedTo}, text length: ${text.length}`
    );

    const result = await translateWithPivot(normalizedFrom, normalizedTo, text, html || false);

    logger.debug(`Translation completed: ${normalizedFrom} -> ${normalizedTo}`);

    res.json({ result });
  } catch (error) {
    logger.error(`Translation failed: ${error}`);
    res.status(500).json({ error: `Translation failed: ${error}` });
  }
}

export async function handleTranslateBatch(req: Request, res: Response) {
  try {
    const { from, to, texts, html } = req.body;

    if (!from || !to || !texts || !Array.isArray(texts)) {
      res.status(400).json({ error: 'Missing required fields: from, to, texts (array)' });
      return;
    }

    const normalizedFrom = normalizeLanguageCode(from);
    const normalizedTo = normalizeLanguageCode(to);

    logger.debug(
      `Batch translation request: ${normalizedFrom} -> ${normalizedTo}, count: ${texts.length}`
    );

    const results: string[] = [];

    for (let i = 0; i < texts.length; i++) {
      try {
        const result = await translateWithPivot(
          normalizedFrom,
          normalizedTo,
          texts[i],
          html || false
        );
        results.push(result);
      } catch (error) {
        logger.error(`Batch translation failed at index ${i}: ${error}`);
        res.status(500).json({ error: `Translation failed at index ${i}: ${error}` });
        return;
      }
    }

    logger.debug(
      `Batch translation completed: ${normalizedFrom} -> ${normalizedTo}, count: ${texts.length}`
    );

    res.json({ results });
  } catch (error) {
    logger.error(`Batch translation failed: ${error}`);
    res.status(500).json({ error: `Batch translation failed: ${error}` });
  }
}
