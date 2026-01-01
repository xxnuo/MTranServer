import { Request, Response } from 'express';
import { getSupportedLanguages, getLanguagePairs } from '@/models/index.js';
import { detectLanguage, detectLanguageWithConfidence } from '@/services/detector.js';

export async function handleLanguages(req: Request, res: Response) {
  try {
    const languages = getSupportedLanguages();
    const pairs = getLanguagePairs();

    res.json({
      languages,
      pairs,
    });
  } catch (error) {
    res.status(500).json({ error: `Failed to get languages: ${error}` });
  }
}

export async function handleDetectLanguage(req: Request, res: Response) {
  try {
    const { text, minConfidence } = req.body;

    if (!text || typeof text !== 'string') {
      res.status(400).json({ error: 'Text is required' });
      return;
    }

    if (minConfidence !== undefined) {
      const result = await detectLanguageWithConfidence(text, minConfidence);
      res.json(result);
    } else {
      const language = await detectLanguage(text);
      res.json({ language });
    }
  } catch (error) {
    res.status(500).json({ error: `Language detection failed: ${error}` });
  }
}
