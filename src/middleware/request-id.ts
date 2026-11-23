import { Request, Response, NextFunction } from 'express';
import crypto from 'crypto';

declare global {
  namespace Express {
    interface Request {
      id: string;
    }
  }
}

const instancePrefix = `${crypto.randomBytes(6).toString('hex')}-`;
let sequence = 0;

export function requestId() {
  return (req: Request, res: Response, next: NextFunction) => {
    const header = req.headers['x-request-id'];
    const incoming = Array.isArray(header) ? header[0] : header;
    const id = incoming || `${instancePrefix}${(++sequence).toString(36)}`;
    req.id = id;
    res.setHeader('X-Request-ID', id);
    next();
  };
}
