import { Request, Response, NextFunction } from 'express';
import crypto from 'crypto';

declare global {
  namespace Express {
    interface Request {
      id: string;
    }
  }
}

export function requestId() {
  return (req: Request, res: Response, next: NextFunction) => {
    const id = req.get('X-Request-ID') || crypto.randomUUID();
    req.id = id;
    res.setHeader('X-Request-ID', id);
    next();
  };
}
