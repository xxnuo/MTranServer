import { Request, Response, NextFunction } from 'express';

export function auth(apiToken: string) {
  return (req: Request, res: Response, next: NextFunction) => {
    if (!apiToken) {
      next();
      return;
    }

    const headerToken = req.headers['authorization']?.replace('Bearer ', '');
    const queryToken = req.query.api_token as string;
    const xApiToken = req.headers['x-api-token'] as string;

    const token = headerToken || queryToken || xApiToken;

    if (token !== apiToken) {
      res.status(401).json({ error: 'Unauthorized' });
      return;
    }

    next();
  };
}
