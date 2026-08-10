import express, { NextFunction, Request, Response } from 'express';
import multer from 'multer';
import { getConfig } from '@/config/index.js';
import { recognizeImageWithTranslations, translateImage } from '@/services/image-translator.js';
import { recognizeImage } from '@/services/ocr.js';

const upload = multer({
  storage: multer.memoryStorage(),
  limits: {
    fileSize: 12 * 1024 * 1024,
    files: 1,
  },
  fileFilter: (_req, file, cb) => {
    if (!/^image\/(png|jpe?g|webp|gif|tiff?)$/i.test(file.mimetype)) {
      cb(new Error('Only image files are supported'));
      return;
    }
    cb(null, true);
  },
});

function getToken(req: Request): string {
  const headerToken = req.headers.authorization?.replace(/^Bearer\s+/i, '');
  const queryToken = typeof req.query.api_token === 'string' ? req.query.api_token : '';
  const queryToken2 = typeof req.query.token === 'string' ? req.query.token : '';
  const xApiToken = typeof req.headers['x-api-token'] === 'string' ? req.headers['x-api-token'] : '';
  return headerToken || queryToken || queryToken2 || xApiToken || '';
}

function requireApiToken(req: Request, res: Response, next: NextFunction) {
  const apiToken = getConfig().apiToken;
  if (!apiToken || getToken(req) === apiToken) {
    next();
    return;
  }
  res.status(401).json({ error: 'Unauthorized', requestId: req.id || '-' });
}

function getImage(req: Request): Buffer {
  const file = req.file;
  if (file?.buffer?.length) return file.buffer;

  const body = req.body as Record<string, unknown>;
  const value = typeof body.imageBase64 === 'string' ? body.imageBase64 : '';
  if (!value) {
    const error: any = new Error('Missing image file');
    error.status = 400;
    error.code = 'MISSING_IMAGE';
    throw error;
  }
  const base64 = value.replace(/^data:image\/[a-z0-9.+-]+;base64,/i, '');
  return Buffer.from(base64, 'base64');
}

function getLang(value: unknown, fallback: string): string {
  return typeof value === 'string' && value.trim() ? value.trim() : fallback;
}

function asyncHandler(handler: (req: Request, res: Response) => Promise<void>) {
  return (req: Request, res: Response, next: NextFunction) => {
    handler(req, res).catch(next);
  };
}

export function registerOcrRoutes(app: express.Express) {
  const router = express.Router();

  router.post(
    '/recognize',
    upload.single('image'),
    asyncHandler(async (req, res) => {
      const body = req.body as Record<string, unknown>;
      const image = getImage(req);
      const from = getLang(body.from, 'auto');
      const to = getLang(body.to, 'zh-Hans');
      const translate = body.translate === 'true' || body.translate === true;
      const result = translate
        ? await recognizeImageWithTranslations(image, from, to)
        : await recognizeImage(image);
      res.json(result);
    })
  );

  router.post(
    '/translate-image',
    upload.single('image'),
    asyncHandler(async (req, res) => {
      const body = req.body as Record<string, unknown>;
      const image = getImage(req);
      const from = getLang(body.from, 'auto');
      const to = getLang(body.to, 'zh-Hans');
      const result = await translateImage(image, from, to);

      if (req.query.format === 'json' || body.format === 'json') {
        res.json({
          mimeType: result.mimeType,
          imageBase64: result.image.toString('base64'),
          ocr: result.ocr,
          lines: result.lines,
        });
        return;
      }

      res.type(result.mimeType);
      res.setHeader('Cache-Control', 'no-store');
      res.send(result.image);
    })
  );

  app.use('/ocr', requireApiToken, router);
}
