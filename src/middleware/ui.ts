import { Request, Response, NextFunction, RequestHandler } from 'express';
import { readFile } from 'fs/promises';
import { assets } from '@/assets/ui.js';

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

export const UI: RequestHandler = async (req: Request, res: Response, next: NextFunction) => {
  let filePath = req.path;

  if (filePath.startsWith('/ui/')) {
    filePath = filePath.substring(3);
  }

  if (filePath === '/' || filePath === '') {
    filePath = '/index.html';
  }

  const assetPath = assets[filePath];

  if (assetPath) {
    const ext = filePath.substring(filePath.lastIndexOf('.'));
    const mimeType = mimeTypes[ext] || 'application/octet-stream';

    res.setHeader('Content-Type', mimeType);
    const buffer = await readFile(assetPath);
    res.send(buffer);
  } else {
    const indexPath = assets['/index.html'];
    if (indexPath) {
      res.setHeader('Content-Type', 'text/html');
      const buffer = await readFile(indexPath);
      res.send(buffer);
    } else {
      next();
    }
  }
};
