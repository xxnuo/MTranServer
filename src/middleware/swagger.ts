import { Request, Response, NextFunction, RequestHandler } from 'express';
import { readFile } from 'fs/promises';
import { dirname, resolve, isAbsolute } from 'path';
import { fileURLToPath } from 'url';
import { swaggerAssets } from '@/assets/swagger.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const mimeTypes: Record<string, string> = {
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.png': 'image/png',
};

export const swaggerStatic: RequestHandler = async (req: Request, res: Response, next: NextFunction) => {
  const filePath = req.path;
  const assetPath = swaggerAssets[filePath];

  if (assetPath) {
    const ext = filePath.substring(filePath.lastIndexOf('.'));
    const mimeType = mimeTypes[ext] || 'application/octet-stream';
    res.setHeader('Content-Type', mimeType);

    const resolvedPath = isAbsolute(assetPath) ? assetPath : resolve(__dirname, assetPath);
    const buffer = await readFile(resolvedPath);
    res.send(buffer);
  } else {
    next();
  }
};
