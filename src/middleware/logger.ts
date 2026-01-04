import { Request, Response, NextFunction } from 'express';
import * as logger from '@/logger/index.js';
import { getConfig } from '@/config/index.js';

export function requestLogger() {
  return (req: Request, res: Response, next: NextFunction) => {
    const config = getConfig();
    const start = Date.now();

    res.on('finish', () => {
      // Only log requests if the feature is enabled - this should work regardless of log level
      if (config.logRequests) {
        const duration = ((Date.now() - start) / 1000).toFixed(2);
        const ip = req.ip || req.socket.remoteAddress || 'unknown';
        
        // Extract path without query string
        const pathWithoutQuery = req.path || req.url.split('?')[0];
        
        // Get content length and accept language
        const contentLength = res.get('content-length') || '0';
        
        logger.important(`${req.method} ${pathWithoutQuery} ${res.statusCode} ${duration}s ${contentLength}bytes ${ip}`);
      }
    });

    next();
  };
}
