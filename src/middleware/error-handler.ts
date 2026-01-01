import { Request, Response, NextFunction } from 'express';
import * as logger from '@/logger/index.js';

export function errorHandler() {
  return (err: any, req: Request, res: Response, next: NextFunction) => {
    const requestId = req.id || '-';
    logger.error(`[${requestId}] Unhandled Error: ${err.message}`, err);

    if (res.headersSent) {
      return next(err);
    }

    res.status(500).json({
      error: 'Internal Server Error',
      message: process.env.NODE_ENV === 'production' ? 'An unexpected error occurred' : err.message,
      requestId,
    });
  };
}
