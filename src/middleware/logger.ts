import { Request, Response, NextFunction } from 'express';
import * as logger from '../logger/index.js';

export function requestLogger() {
  return (req: Request, res: Response, next: NextFunction) => {
    const start = Date.now();

    res.on('finish', () => {
      const duration = Date.now() - start;
      const ip = req.ip || req.socket.remoteAddress || 'unknown';
      const userAgent = req.get('user-agent') || '-';
      const requestId = req.id || '-';
      logger.info(`${req.method} ${req.path} ${res.statusCode} ${duration}ms - ${ip} - "${userAgent}" [${requestId}]`);
    });

    next();
  };
}
