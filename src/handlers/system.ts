import { Request, Response } from 'express';
import { getVersion } from '@/version/index.js';

export function handleVersion(req: Request, res: Response) {
  res.json({ version: getVersion() });
}

export function handleHealth(req: Request, res: Response) {
  res.json({ status: 'ok' });
}

export function handleHeartbeat(req: Request, res: Response) {
  res.sendStatus(200);
}

export function handleLBHeartbeat(req: Request, res: Response) {
  res.sendStatus(200);
}
