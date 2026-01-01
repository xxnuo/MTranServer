import { Request, Response } from 'express';
import { getSupportedLanguages, getLanguagePairs } from '../models/index.js';

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
