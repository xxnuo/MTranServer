import { Request, Response, NextFunction } from 'express';
import * as logger from '@/logger/index.js';

export function errorHandler() {
  return (err: any, req: Request, res: Response, next: NextFunction) => {
    const requestId = req.id || '-';
    const status = err.status || 500;
    const normalizedMessage = typeof err.message === 'string' ? err.message.toLowerCase() : '';
    const requestAborted = req.aborted
      || err.name === 'AbortError'
      || err.code === 'ECONNABORTED'
      || normalizedMessage.includes('request aborted');

    if (requestAborted) {
      logger.debug(`[${requestId}] Client disconnected before request completed`);
    } else if (status === 401) {
      logger.warn(`[${requestId}] Unauthorized`);
    } else {
      logger.error(`[${requestId}] Unhandled Error: ${err.message}`, err);
    }

    if (requestAborted) {
      return;
    }

    if (res.headersSent) {
      return next(err);
    }

    if (status === 401) {
      res.status(401).json({ error: 'Unauthorized', requestId });
      return;
    }

    res.status(500).json({
      error: 'Internal Server Error',
      message: process.env.NODE_ENV === 'production' ? 'An unexpected error occurred' : err.message,
      requestId,
    });
  };
}
