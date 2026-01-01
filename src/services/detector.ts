import { createRequire } from 'module';
import path from 'path';
import { fileURLToPath } from 'url';
import * as logger from '../logger/index.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

export interface TextSegment {
  text: string;
  language: string;
  start: number;
  end: number;
  confidence: number;
}

const DEFAULT_CONFIDENCE_THRESHOLD = 0.5;
const MAXIMUM_LANGUAGES_IN_ONE_TEXT = 2;

let cldModule: any = null;
let initPromise: Promise<void> | null = null;

async function initCLD(): Promise<void> {
  if (cldModule) return;

  if (initPromise) {
    return initPromise;
  }

  initPromise = (async () => {
    try {
      logger.debug('Initializing CLD language detector');

      const require = createRequire(import.meta.url);
      const cldWorkerPath = path.resolve(__dirname, '../lib/cld/cld-worker.js');

      (global as any).Module = {
        print: (msg: string) => logger.debug(`[CLD]: ${msg}`),
        printErr: (msg: string) => logger.error(`[CLD Error]: ${msg}`),
        locateFile: (filePath: string) => {
          if (filePath.endsWith('.wasm')) {
            return path.resolve(__dirname, '../lib/cld', filePath);
          }
          return filePath;
        },
        noExitRuntime: true
      };

      const cldModule_ = require(cldWorkerPath);

      await new Promise<void>((resolve) => {
        if (cldModule_.onRuntimeInitialized) {
          cldModule_.onRuntimeInitialized = () => {
            logger.debug('CLD runtime initialized');
            resolve();
          };
        } else {
          resolve();
        }
      });

      cldModule = cldModule_;
      logger.debug('CLD language detector initialized');
    } catch (error) {
      logger.error(`Failed to initialize CLD: ${error}`);
      throw error;
    }
  })();

  return initPromise;
}

function detectLanguageWithCLD(text: string, isHTML: boolean = false) {
  if (!cldModule) {
    throw new Error('CLD module not initialized');
  }

  const result = cldModule.LanguageInfo.O(text, !isHTML);

  const languages = Array(3).fill(0).map((_, i) => {
    const lang = result.U(i);
    return {
      languageCode: lang.P(),
      percent: lang.da()
    };
  }).filter(l => l.languageCode !== 'un' || l.percent > 0);

  const output = {
    language: result.P(),
    confident: result.ba(),
    languages,
    percentScore: languages[0]?.percent || 0
  };

  cldModule.destroy(result);

  return output;
}

function bcp47Normalize(code: string): string {
  switch (code) {
    case 'zh':
      return 'zh-Hans';
    default:
      return code.toLowerCase();
  }
}

export async function detectLanguage(text: string): Promise<string> {
  if (!text) {
    return '';
  }

  await initCLD();

  try {
    const result = detectLanguageWithCLD(text);
    return bcp47Normalize(result.language);
  } catch (error) {
    logger.error(`Language detection failed: ${error}`);
    return '';
  }
}

export async function detectLanguageWithConfidence(
  text: string,
  minConfidence: number = DEFAULT_CONFIDENCE_THRESHOLD
): Promise<{ language: string; confidence: number }> {
  if (!text) {
    return { language: '', confidence: 0 };
  }

  await initCLD();

  try {
    const result = detectLanguageWithCLD(text);
    const confidence = result.percentScore / 100;

    if (confidence < minConfidence) {
      return { language: '', confidence };
    }

    return {
      language: bcp47Normalize(result.language),
      confidence
    };
  } catch (error) {
    logger.error(`Language detection with confidence failed: ${error}`);
    return { language: '', confidence: 0 };
  }
}

function hasMixedScripts(text: string): boolean {
  let hasCJK = false;
  let hasLatin = false;

  for (const char of text) {
    const code = char.charCodeAt(0);

    if (
      (code >= 0x4e00 && code <= 0x9fff) ||
      (code >= 0x3040 && code <= 0x309f) ||
      (code >= 0x30a0 && code <= 0x30ff) ||
      (code >= 0xac00 && code <= 0xd7af)
    ) {
      hasCJK = true;
    } else if ((code >= 0x0041 && code <= 0x005a) || (code >= 0x0061 && code <= 0x007a)) {
      hasLatin = true;
    }

    if (hasCJK && hasLatin) {
      return true;
    }
  }

  return false;
}

export async function detectMultipleLanguages(text: string): Promise<TextSegment[]> {
  return detectMultipleLanguagesWithThreshold(text, DEFAULT_CONFIDENCE_THRESHOLD);
}

export async function detectMultipleLanguagesWithThreshold(
  text: string,
  threshold: number
): Promise<TextSegment[]> {
  if (!text) {
    return [];
  }

  await initCLD();

  const fallbackLang = await detectLanguage(text);
  const effectiveFallback = fallbackLang || 'en';

  if (!hasMixedScripts(text)) {
    logger.debug(`DetectMultipleLanguages: no mixed scripts, using single language: ${effectiveFallback}`);
    return [{
      text,
      language: effectiveFallback,
      start: 0,
      end: text.length,
      confidence: 1.0
    }];
  }

  logger.debug(`DetectMultipleLanguages: mixed scripts detected, fallback=${effectiveFallback}, threshold=${threshold.toFixed(2)}`);

  const segments: TextSegment[] = [];

  const segmenterAny = new (Intl as any).Segmenter(undefined, { granularity: 'sentence' });
  const sentenceSegments = Array.from(segmenterAny.segment(text)) as Array<{segment: string, index: number}>;

  for (const { segment, index } of sentenceSegments) {
    try {
      const result = detectLanguageWithCLD(segment);
      const detectedLang = bcp47Normalize(result.language);
      const confidence = result.percentScore / 100;

      segments.push({
        text: segment,
        language: confidence >= threshold ? detectedLang : effectiveFallback,
        start: index,
        end: index + segment.length,
        confidence
      });
    } catch (error) {
      logger.warn(`Failed to detect language for segment: ${error}`);
      segments.push({
        text: segment,
        language: effectiveFallback,
        start: index,
        end: index + segment.length,
        confidence: 0
      });
    }
  }

  const mergedSegments = mergeAdjacentSegments(segments, text);
  const limitedSegments = limitLanguages(mergedSegments, text, MAXIMUM_LANGUAGES_IN_ONE_TEXT);

  logger.debug(`DetectMultipleLanguages: ${sentenceSegments.length} sentences -> ${mergedSegments.length} merged -> ${limitedSegments.length} final segments`);

  return limitedSegments;
}

function mergeAdjacentSegments(segments: TextSegment[], originalText: string): TextSegment[] {
  if (segments.length <= 1) {
    return segments;
  }

  const merged: TextSegment[] = [];
  let current = segments[0];

  for (let i = 1; i < segments.length; i++) {
    const next = segments[i];
    if (current.language === next.language) {
      current.text = originalText.substring(current.start, next.end);
      current.end = next.end;
      if (next.confidence > current.confidence) {
        current.confidence = next.confidence;
      }
    } else {
      merged.push(current);
      current = next;
    }
  }
  merged.push(current);

  return merged;
}

function limitLanguages(
  segments: TextSegment[],
  originalText: string,
  maxLangs: number
): TextSegment[] {
  if (segments.length <= 1) {
    return segments;
  }

  const langBytes = new Map<string, number>();
  for (const seg of segments) {
    langBytes.set(seg.language, (langBytes.get(seg.language) || 0) + (seg.end - seg.start));
  }

  if (langBytes.size <= maxLangs) {
    return segments;
  }

  const sorted = Array.from(langBytes.entries())
    .sort((a, b) => b[1] - a[1]);

  const keepLangs = new Set(sorted.slice(0, maxLangs).map(([lang]) => lang));
  const primaryLang = sorted[0][0];

  for (const seg of segments) {
    if (!keepLangs.has(seg.language)) {
      seg.language = primaryLang;
    }
  }

  const result = mergeAdjacentSegments(segments, originalText);
  logger.debug(`limitLanguages: reduced to ${maxLangs} languages, ${result.length} segments`);

  return result;
}
