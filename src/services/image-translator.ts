import { readFile } from 'fs/promises';
import sharp from 'sharp';
import { OCR_FONT_ASSETS, OCR_FONT_FAMILY } from '@/assets/fonts.js';
import { translateWithPivot } from './engine.js';
import { recognizeImage, type OcrBox, type OcrLine, type OcrResult } from './ocr.js';

export interface ImageTranslationLine extends OcrLine {
  translatedText: string;
}

export interface ImageTranslationResult {
  image: Buffer;
  mimeType: string;
  ocr: OcrResult;
  lines: ImageTranslationLine[];
}

interface Rect {
  left: number;
  top: number;
  width: number;
  height: number;
}

let fontCssPromise: Promise<string> | null = null;

function clamp(value: number, min: number, max: number) {
  return Math.min(max, Math.max(min, value));
}

function expandBox(box: OcrBox, imageWidth: number, imageHeight: number): Rect {
  const padX = clamp(Math.round(box.width * 0.08), 3, 14);
  const padY = clamp(Math.round(box.height * 0.2), 3, 12);
  const left = clamp(Math.floor(box.x - padX), 0, imageWidth - 1);
  const top = clamp(Math.floor(box.y - padY), 0, imageHeight - 1);
  const right = clamp(Math.ceil(box.x + box.width + padX), left + 1, imageWidth);
  const bottom = clamp(Math.ceil(box.y + box.height + padY), top + 1, imageHeight);
  return { left, top, width: right - left, height: bottom - top };
}

async function loadFontCss(): Promise<string> {
  if (!fontCssPromise) {
    fontCssPromise = (async () => {
      const parts = await Promise.all(OCR_FONT_ASSETS.map(async (asset) => {
        const buffer = await readFile(asset.path);
        return `
@font-face {
  font-family: '${OCR_FONT_FAMILY}';
  font-style: normal;
  font-weight: 400;
  src: url(data:font/woff2;base64,${buffer.toString('base64')}) format('woff2');
  unicode-range: ${asset.unicodeRange};
}`;
      }));
      return parts.join('\n');
    })();
  }
  return fontCssPromise;
}

function escapeXml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&apos;');
}

function textWidth(text: string, fontSize: number): number {
  let width = 0;
  for (const char of text) {
    if (/[\u3400-\u9fff\u3040-\u30ff\uff00-\uffef]/.test(char)) {
      width += fontSize;
    } else if (/\s/.test(char)) {
      width += fontSize * 0.32;
    } else {
      width += fontSize * 0.56;
    }
  }
  return width;
}

function wrapText(text: string, maxWidth: number, fontSize: number): string[] {
  const containsCjk = /[\u3400-\u9fff\u3040-\u30ff]/.test(text);
  const tokens = containsCjk ? Array.from(text) : text.split(/(\s+)/).filter(Boolean);
  const lines: string[] = [];
  let current = '';

  for (const token of tokens) {
    const candidate = current ? `${current}${containsCjk ? '' : ''}${token}` : token;
    if (current && textWidth(candidate, fontSize) > maxWidth) {
      lines.push(current.trim());
      current = token;
    } else {
      current = candidate;
    }
  }

  if (current.trim()) {
    lines.push(current.trim());
  }
  return lines.length ? lines : [text];
}

function fitText(text: string, box: OcrBox) {
  const maxWidth = Math.max(24, box.width * 0.94);
  const maxHeight = Math.max(12, box.height * 1.1);
  const start = clamp(Math.round(box.height * 0.78), 11, 34);

  for (let fontSize = start; fontSize >= 8; fontSize -= 1) {
    const lines = wrapText(text, maxWidth, fontSize);
    const lineHeight = fontSize * 1.18;
    if (lines.length * lineHeight <= maxHeight) {
      return { fontSize, lineHeight, lines };
    }
  }

  const fontSize = 8;
  return { fontSize, lineHeight: 9.5, lines: wrapText(text, maxWidth, fontSize).slice(0, 4) };
}

async function sampleBackgroundColor(image: Buffer, box: OcrBox, imageWidth: number, imageHeight: number): Promise<string> {
  const rect = expandBox(box, imageWidth, imageHeight);
  try {
    const stats = await sharp(image)
      .extract({ left: rect.left, top: rect.top, width: rect.width, height: rect.height })
      .blur(10)
      .stats();
    const [r, g, b] = stats.channels;
    const luminance = ((r?.mean || 255) * 0.299) + ((g?.mean || 255) * 0.587) + ((b?.mean || 255) * 0.114);
    return luminance > 145 ? '#111827' : '#f9fafb';
  } catch {
    return '#111827';
  }
}

async function createCleanBase(image: Buffer, boxes: OcrBox[], width: number, height: number): Promise<Buffer> {
  if (boxes.length === 0) {
    return sharp(image).png().toBuffer();
  }

  const averageHeight = boxes.reduce((total, box) => total + box.height, 0) / boxes.length;
  const blurRadius = clamp(Math.round(averageHeight * 0.8), 10, 28);
  const blurred = await sharp(image).blur(blurRadius).png().toBuffer();

  const patches = await Promise.all(boxes.slice(0, 120).map(async (box) => {
    const rect = expandBox(box, width, height);
    const patch = await sharp(blurred)
      .extract({ left: rect.left, top: rect.top, width: rect.width, height: rect.height })
      .png()
      .toBuffer();
    return { input: patch, left: rect.left, top: rect.top };
  }));

  return sharp(image).composite(patches).png().toBuffer();
}

async function createTextLayer(lines: ImageTranslationLine[], image: Buffer, width: number, height: number): Promise<Buffer> {
  const fontCss = await loadFontCss();
  const texts = await Promise.all(lines.map(async (line) => {
    const fitted = fitText(line.translatedText, line.box);
    const color = await sampleBackgroundColor(image, line.box, width, height);
    const x = line.box.x + line.box.width / 2;
    const totalHeight = fitted.lines.length * fitted.lineHeight;
    const firstY = line.box.y + Math.max(fitted.fontSize, (line.box.height - totalHeight) / 2 + fitted.fontSize);

    const tspans = fitted.lines.map((part, index) => (
      `<tspan x="${x.toFixed(1)}" dy="${index === 0 ? 0 : fitted.lineHeight.toFixed(1)}">${escapeXml(part)}</tspan>`
    )).join('');

    return `<text x="${x.toFixed(1)}" y="${firstY.toFixed(1)}" text-anchor="middle" dominant-baseline="alphabetic" font-family="${OCR_FONT_FAMILY}" font-size="${fitted.fontSize}" fill="${color}" paint-order="stroke" stroke="${color === '#111827' ? '#ffffff' : '#111827'}" stroke-opacity="0.18" stroke-width="${Math.max(0.4, fitted.fontSize * 0.035).toFixed(1)}">${tspans}</text>`;
  }));

  const svg = `
<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}">
  <defs>
    <style><![CDATA[
${fontCss}
text {
  letter-spacing: 0;
}
    ]]></style>
  </defs>
  ${texts.join('\n')}
</svg>`;

  return Buffer.from(svg);
}

export async function translateImage(image: Buffer, from: string, to: string): Promise<ImageTranslationResult> {
  const metadata = await sharp(image).metadata();
  if (!metadata.width || !metadata.height) {
    throw new Error('Unable to read image dimensions');
  }

  const normalized = await sharp(image)
    .rotate()
    .resize({
      width: 2400,
      height: 2400,
      fit: 'inside',
      withoutEnlargement: true,
    })
    .png()
    .toBuffer();
  const normalizedMeta = await sharp(normalized).metadata();
  const width = normalizedMeta.width || metadata.width;
  const height = normalizedMeta.height || metadata.height;

  const ocr = await recognizeImage(normalized);
  const translatedLines: ImageTranslationLine[] = [];
  for (const line of ocr.lines) {
    const text = line.text.trim();
    if (!text) continue;
    const translatedText = await translateWithPivot(from, to, text, false);
    translatedLines.push({ ...line, translatedText });
  }

  const cleanBase = await createCleanBase(normalized, translatedLines.map(line => line.box), width, height);
  const textLayer = await createTextLayer(translatedLines, cleanBase, width, height);
  const output = await sharp(cleanBase)
    .composite([{ input: textLayer, left: 0, top: 0 }])
    .png()
    .toBuffer();

  return {
    image: output,
    mimeType: 'image/png',
    ocr,
    lines: translatedLines,
  };
}

export async function recognizeImageWithTranslations(image: Buffer, from: string, to: string) {
  const ocr = await recognizeImage(image);
  const lines: ImageTranslationLine[] = [];
  for (const line of ocr.lines) {
    const text = line.text.trim();
    if (!text) continue;
    lines.push({
      ...line,
      translatedText: await translateWithPivot(from, to, text, false),
    });
  }
  return { ...ocr, lines };
}
