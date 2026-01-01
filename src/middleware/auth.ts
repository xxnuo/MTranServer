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

export function expressAuthentication(
  request: Request,
  securityName: string,
  scopes?: string[]
): Promise<any> {
  if (securityName === 'api_token') {
    const apiToken = process.env.MT_API_TOKEN || '';

    if (!apiToken) {
      return Promise.resolve();
    }

    const headerToken = request.headers['authorization']?.replace('Bearer ', '');
    const queryToken = request.query.api_token as string;
    const queryToken2 = request.query.token as string;
    const xApiToken = request.headers['x-api-token'] as string;

    const token = headerToken || queryToken || queryToken2 || xApiToken;

    if (token === apiToken) {
      return Promise.resolve();
    } else {
      return Promise.reject(new Error('Unauthorized'));
    }
  }

  return Promise.reject(new Error('Unknown security name'));
}
