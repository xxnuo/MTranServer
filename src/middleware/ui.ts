import { Request, Response, NextFunction, RequestHandler } from 'express';
import { readFile } from 'fs/promises';
import { dirname, resolve, isAbsolute } from 'path';
import { fileURLToPath } from 'url';
import { assets } from '@/assets/ui.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const mimeTypes: Record<string, string> = {
  '.html': 'text/html',
  '.css': 'text/css',
  '.js': 'application/javascript',
  '.json': 'application/json',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif': 'image/gif',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
};

export const uiStatic: RequestHandler = async (req: Request, res: Response, next: NextFunction) => {
  let filePath = req.path;

  if (filePath === '/' || filePath === '') {
    filePath = '/index.html';
  }

  const assetPath = assets[filePath];

  if (assetPath) {
    const ext = filePath.substring(filePath.lastIndexOf('.'));
    const mimeType = mimeTypes[ext] || 'application/octet-stream';
    res.setHeader('Content-Type', mimeType);

    const resolvedPath = isAbsolute(assetPath) ? assetPath : resolve(__dirname, assetPath);
    const buffer = await readFile(resolvedPath);
    res.send(buffer);
  } else {
    const indexPath = assets['/index.html'];
    if (indexPath) {
      res.setHeader('Content-Type', 'text/html');
      const resolvedIndexPath = isAbsolute(indexPath) ? indexPath : resolve(__dirname, indexPath);
      const buffer = await readFile(resolvedIndexPath);
      res.send(buffer);
    } else {
      next();
    }
  }
};
