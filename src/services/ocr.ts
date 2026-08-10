import fs from 'fs';
import path from 'path';
import {
  PaddleOcrService,
  V5_MOBILE_MODEL,
  V6_SMALL_MODEL,
  V6_TINY_MODEL,
  type FlattenedPaddleOcrResult,
} from 'ppu-paddle-ocr';
import { getConfig } from '@/config/index.js';
import * as logger from '@/logger/index.js';

export interface OcrBox {
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface OcrItem {
  text: string;
  confidence: number;
  box: OcrBox;
}

export interface OcrLine {
  text: string;
  confidence: number;
  box: OcrBox;
  items: OcrItem[];
}

export interface OcrResult {
  text: string;
  confidence: number;
  model: string;
  items: OcrItem[];
  lines: OcrLine[];
}

interface ServiceState {
  service: PaddleOcrService;
  model: string;
}

let servicePromise: Promise<ServiceState> | null = null;

function toArrayBuffer(buffer: Buffer): ArrayBuffer {
  return buffer.buffer.slice(buffer.byteOffset, buffer.byteOffset + buffer.byteLength) as ArrayBuffer;
}

function modelFile(root: string, relativePath: string): string | null {
  const fullPath = path.join(root, ...relativePath.split('/'));
  return fs.existsSync(fullPath) ? fullPath : null;
}

function findLocalModel() {
  const root = path.join(getConfig().modelDir, 'ocr');

  const v6Root = path.join(root, 'pp-ocrv6-tiny');
  const v6Detection = modelFile(v6Root, 'PP-OCRv6/det/PP-OCRv6_det_tiny.onnx');
  const v6Recognition = modelFile(v6Root, 'PP-OCRv6/rec/PP-OCRv6_rec_tiny.onnx');
  if (v6Detection && v6Recognition) {
    return {
      name: 'pp-ocrv6-tiny-local',
      model: {
        detection: v6Detection,
        recognition: v6Recognition,
        charactersDictionary: V6_TINY_MODEL.charactersDictionary,
      },
    };
  }

  const v5Root = path.join(root, 'pp-ocrv5-mobile');
  const v5Detection = modelFile(v5Root, 'PP-OCRv5/det/ch_PP-OCRv5_det_mobile.onnx');
  const v5Recognition = modelFile(v5Root, 'PP-OCRv5/rec/ch_PP-OCRv5_rec_mobile.onnx');
  if (v5Detection && v5Recognition) {
    return {
      name: 'pp-ocrv5-mobile-local',
      model: {
        detection: v5Detection,
        recognition: v5Recognition,
        charactersDictionary: V5_MOBILE_MODEL.charactersDictionary,
      },
    };
  }

  return null;
}

async function createService(): Promise<ServiceState> {
  const local = findLocalModel();
  const candidates = [
    local,
    {
      name: 'pp-ocrv6-small-preset',
      model: V6_SMALL_MODEL,
    },
  ].filter(Boolean) as Array<{ name: string; model: any }>;

  let lastError: unknown = null;
  for (const candidate of candidates) {
    const service = new PaddleOcrService({
      model: candidate.model,
      recognition: {
        strategy: 'per-line',
        minimumConfidence: 0.45,
      } as any,
      detection: {
        maxSideLength: 'auto',
        minimumAreaThreshold: 24,
        paddingHorizontal: 0.45,
        paddingVertical: 0.3,
      },
      session: {
        executionProviders: ['cpu'],
        graphOptimizationLevel: 'all',
        executionMode: 'sequential',
      },
      processing: {
        engine: 'opencv',
      },
      debugging: {
        verbose: false,
        debug: false,
      },
    });

    try {
      await service.initialize();
      logger.info(`OCR service initialized with ${candidate.name}`);
      return { service, model: candidate.name };
    } catch (error) {
      lastError = error;
      logger.warn(`Failed to initialize OCR model ${candidate.name}: ${error instanceof Error ? error.message : String(error)}`);
      await service.destroy().catch(() => undefined);
    }
  }

  throw lastError instanceof Error ? lastError : new Error('Failed to initialize OCR service');
}

async function getService(): Promise<ServiceState> {
  if (!servicePromise) {
    servicePromise = createService();
  }
  return servicePromise;
}

function mergeBoxes(boxes: OcrBox[]): OcrBox {
  const left = Math.min(...boxes.map(box => box.x));
  const top = Math.min(...boxes.map(box => box.y));
  const right = Math.max(...boxes.map(box => box.x + box.width));
  const bottom = Math.max(...boxes.map(box => box.y + box.height));
  return { x: left, y: top, width: right - left, height: bottom - top };
}

function groupLines(items: OcrItem[]): OcrLine[] {
  const sorted = [...items].sort((a, b) => {
    const ay = a.box.y + a.box.height / 2;
    const by = b.box.y + b.box.height / 2;
    if (Math.abs(ay - by) > Math.max(a.box.height, b.box.height) * 0.45) return ay - by;
    return a.box.x - b.box.x;
  });

  const groups: OcrItem[][] = [];
  for (const item of sorted) {
    const centerY = item.box.y + item.box.height / 2;
    const group = groups.find(line => {
      const lineBox = mergeBoxes(line.map(part => part.box));
      const lineCenterY = lineBox.y + lineBox.height / 2;
      return Math.abs(centerY - lineCenterY) <= Math.max(item.box.height, lineBox.height) * 0.55;
    });
    if (group) {
      group.push(item);
    } else {
      groups.push([item]);
    }
  }

  return groups.map(group => {
    const ordered = [...group].sort((a, b) => a.box.x - b.box.x);
    const text = ordered.map(item => item.text).join(hasCjk(ordered.map(item => item.text).join('')) ? '' : ' ');
    return {
      text,
      confidence: ordered.reduce((total, item) => total + item.confidence, 0) / ordered.length,
      box: mergeBoxes(ordered.map(item => item.box)),
      items: ordered,
    };
  });
}

function hasCjk(text: string): boolean {
  return /[\u3400-\u9fff]/.test(text);
}

export async function recognizeImage(image: Buffer): Promise<OcrResult> {
  const { service, model } = await getService();
  const result = await service.recognize(toArrayBuffer(image), {
    flatten: true,
    strategy: 'per-line',
    noCache: true,
  }) as FlattenedPaddleOcrResult;

  const items = result.results
    .filter(item => item.text.trim())
    .map(item => ({
      text: item.text.trim(),
      confidence: item.confidence,
      box: {
        x: Math.max(0, Math.round(item.box.x)),
        y: Math.max(0, Math.round(item.box.y)),
        width: Math.max(1, Math.round(item.box.width)),
        height: Math.max(1, Math.round(item.box.height)),
      },
    }));

  const lines = groupLines(items);
  return {
    text: lines.map(line => line.text).join('\n'),
    confidence: result.confidence,
    model,
    items,
    lines,
  };
}

export async function cleanupOcrService(): Promise<void> {
  if (!servicePromise) return;
  const state = await servicePromise.catch(() => null);
  servicePromise = null;
  await state?.service.destroy().catch(() => undefined);
}
