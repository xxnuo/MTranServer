import { Request, Response, NextFunction } from 'express';
import * as logger from '@/logger/index.js';

export function requestLogger() {
  return (req: Request, res: Response, next: NextFunction) => {
    const start = Date.now();

    res.on('finish', () => {
      const duration = ((Date.now() - start) / 1000).toFixed(2);
      const ip = req.headers['x-real-ip'] || req.headers['x-forwarded-for'] || req.ip;
      logger.important(`${req.method} ${res.statusCode} ${req.path} ${duration}s ${res.get('content-length') || 0}b - ${ip}`);
    });

    next();
  };
}
