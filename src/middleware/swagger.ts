import { Request, Response, NextFunction, RequestHandler } from 'express';
import { readFile } from 'fs/promises';
import { swaggerAssets } from '@/assets/swagger.js';

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
    const buffer = await readFile(assetPath);
    res.send(buffer);
  } else {
    next();
  }
};
