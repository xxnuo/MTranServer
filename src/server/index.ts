import express, { Request, Response, NextFunction } from 'express';
import fs from 'fs/promises';
import { createServer } from 'node:http';
import swaggerUi from 'swagger-ui-express';
import { getConfig, setConfig, resetConfig, saveConfigFile, clearConfigFile } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import { initRecords } from '@/models/index.js';
import { cleanupAllEngines, getEngineRuntimes, warmupEngines } from '@/services/index.js';
import { cleanupLegacyBin } from '@/assets/index.js';
import { requestId, errorHandler, cors, requestLogger } from '@/middleware/index.js';
import { RegisterRoutes } from '@/generated/routes.js';
import swaggerDocument from '@/generated/swagger.json';
import { uiStatic } from '@/middleware/ui.js';
import { swaggerStatic } from '@/middleware/swagger.js';
import { checkForUpdate } from '@/utils/update-checker.js';
import { VERSION } from '@/version';

export async function startServer({ handleSignals = true, signal }: { handleSignals?: boolean; signal?: AbortSignal } = {}) {
  const config = getConfig();
  const app = express();

  app.use(requestId());
  app.use(express.json());
  app.use(cors());
  if (config.logRequests) {
    app.use(requestLogger());
  }

  const getDesktopControl = () => (globalThis as any).mtranDesktopControl;
  const getSettingsPayload = () => {
    const current = getConfig();
    return {
      config: {
        locale: 'system',
        server: {
          host: current.host,
          port: Number(current.port),
          logLevel: current.logLevel,
          enableWebUI: current.enableWebUI,
          enableOfflineMode: current.enableOfflineMode,
          workerIdleTimeout: current.workerIdleTimeout,
          workersPerLanguage: current.workersPerLanguage,
          apiToken: current.apiToken,
          logDir: current.logDir,
          logToFile: current.logToFile,
          logConsole: current.logConsole,
          logRequests: current.logRequests,
          maxSentenceLength: current.maxSentenceLength,
          fullwidthZhPunctuation: current.fullwidthZhPunctuation,
          checkUpdate: current.checkUpdate,
          cacheSize: current.cacheSize,
          fast: current.fast,
          modelDir: current.modelDir,
          configDir: current.configDir
        }
      },
      status: 'running',
      version: VERSION,
      engines: getEngineRuntimes()
    };
  };
  const toNumber = (value: unknown, fallback: number) => {
    if (typeof value === 'number' && Number.isFinite(value)) return value;
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : fallback;
  };
  const toBool = (value: unknown, fallback: boolean) => {
    if (typeof value === 'boolean') return value;
    return fallback;
  };
  const toString = (value: unknown, fallback: string) => {
    if (typeof value === 'string') return value;
    return fallback;
  };
  const applyServerConfig = (input: any) => {
    const current = getConfig();
    const next = {
      host: toString(input.host, current.host),
      port: String(toNumber(input.port, Number(current.port))),
      logLevel: toString(input.logLevel, current.logLevel),
      enableWebUI: toBool(input.enableWebUI, current.enableWebUI),
      enableOfflineMode: toBool(input.enableOfflineMode, current.enableOfflineMode),
      workerIdleTimeout: toNumber(input.workerIdleTimeout, current.workerIdleTimeout),
      workersPerLanguage: toNumber(input.workersPerLanguage, current.workersPerLanguage),
      apiToken: toString(input.apiToken, current.apiToken),
      logDir: toString(input.logDir, current.logDir),
      logToFile: toBool(input.logToFile, current.logToFile),
      logConsole: toBool(input.logConsole, current.logConsole),
      logRequests: toBool(input.logRequests, current.logRequests),
      maxSentenceLength: toNumber(input.maxSentenceLength, current.maxSentenceLength),
      fullwidthZhPunctuation: toBool(input.fullwidthZhPunctuation, current.fullwidthZhPunctuation),
      checkUpdate: toBool(input.checkUpdate, current.checkUpdate),
      cacheSize: toNumber(input.cacheSize, current.cacheSize),
      fast: toBool(input.fast, toBool(input.lexicalShortlist, current.fast)),
      modelDir: toString(input.modelDir, current.modelDir),
      configDir: toString(input.configDir, current.configDir)
    };
    setConfig(next);
    saveConfigFile(next);
    logger.setLogLevel(next.logLevel as any);
  };

  app.get('/ui/api/settings', async (_, res) => {
    const control = getDesktopControl();
    if (control?.getConfig) {
      const payload = await control.getConfig();
      res.json({ ...payload, engines: getEngineRuntimes() });
      return;
    }
    res.json(getSettingsPayload());
  });

  app.post('/ui/api/settings/apply', async (req, res) => {
    const control = getDesktopControl();
    if (control?.applyConfig) {
      const payload = await control.applyConfig(req.body?.config || req.body);
      res.json(payload);
      return;
    }
    const input = req.body?.config?.server || req.body?.server || {};
    applyServerConfig(input);
    res.json(getSettingsPayload());
  });

  app.post('/ui/api/settings/reset', async (_, res) => {
    const control = getDesktopControl();
    if (control?.resetConfig) {
      const payload = await control.resetConfig();
      res.json(payload);
      return;
    }
    clearConfigFile();
    resetConfig();
    res.json(getSettingsPayload());
  });

  app.post('/ui/api/settings/restart', async (_, res) => {
    const control = getDesktopControl();
    if (control?.restartServer) {
      const payload = await control.restartServer();
      res.json(payload);
      return;
    }
    res.json(getSettingsPayload());
  });

  const verifyHotRouteAuth = (req: Request): boolean => {
    const apiToken = getConfig().apiToken;
    if (!apiToken) return true;
    const authHeader = req.headers.authorization;
    const headerToken = typeof authHeader === 'string'
      ? (authHeader.startsWith('Bearer ') ? authHeader.slice(7) : authHeader.replace('Bearer ', ''))
      : undefined;
    const q = req.query as Record<string, unknown>;
    const token = headerToken || (q.api_token as string) || (q.token as string) || (req.headers['x-api-token'] as string);
    return token === apiToken;
  };
  const parseOptionalBoolean = (value: unknown): boolean | undefined => {
    if (typeof value === 'boolean') return value;
    if (value === 'true' || value === '1' || value === 1) return true;
    if (value === 'false' || value === '0' || value === 0) return false;
    return undefined;
  };
  const validationError = (message: string) => Object.assign(new Error(message), { status: 422 });
  const unauthorizedError = () => Object.assign(new Error('Unauthorized'), { status: 401 });
  let cachedHotDeps: {
    utils: typeof import('@/utils/index.js');
    services: typeof import('@/services/index.js');
  } | undefined;
  const loadHotDeps = async () => {
    if (cachedHotDeps) return cachedHotDeps;
    const [utils, services] = await Promise.all([
      import('@/utils/index.js'),
      import('@/services/index.js'),
    ]);
    return cachedHotDeps = { utils, services };
  };

  // Direct hot handlers bypass tsoa ExpressTemplateService validation + Promise.any auth per request.
  app.post('/translate', async (req: Request, res: Response, next: NextFunction) => {
    try {
      if (!verifyHotRouteAuth(req)) return next(unauthorizedError());
      const body = req.body;
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        return next(validationError('Invalid request body'));
      }
      const { from, to, text, html: rawHtml } = body as Record<string, unknown>;
      if (typeof from !== 'string' || typeof to !== 'string' || typeof text !== 'string') {
        return next(validationError('Invalid request parameters'));
      }
      let html = false;
      if (rawHtml !== undefined) {
        const coerced = parseOptionalBoolean(rawHtml);
        if (coerced === undefined) return next(validationError('Invalid html parameter'));
        html = coerced;
      }
      for (const key in body) {
        if (key !== 'from' && key !== 'to' && key !== 'text' && key !== 'html') {
          return next(validationError(`Excess property '${key}' is not allowed`));
        }
      }
      const { utils, services } = cachedHotDeps ?? await loadHotDeps();
      const result = await services.translateWithPivot(utils.NormalizeLanguageCode(from), utils.NormalizeLanguageCode(to), text, html);
      res.json({ result });
    } catch (error) {
      next(error);
    }
  });

  app.post('/translate/batch', async (req: Request, res: Response, next: NextFunction) => {
    try {
      if (!verifyHotRouteAuth(req)) return next(unauthorizedError());
      const body = req.body;
      if (!body || typeof body !== 'object' || Array.isArray(body)) {
        return next(validationError('Invalid request body'));
      }
      const { from, to, texts, html: rawHtml } = body as Record<string, unknown>;
      if (typeof from !== 'string' || typeof to !== 'string' || !Array.isArray(texts)) {
        return next(validationError('Invalid request parameters'));
      }
      for (let i = 0; i < texts.length; i++) {
        if (typeof texts[i] !== 'string') return next(validationError('Invalid texts item'));
      }
      let html = false;
      if (rawHtml !== undefined) {
        const coerced = parseOptionalBoolean(rawHtml);
        if (coerced === undefined) return next(validationError('Invalid html parameter'));
        html = coerced;
      }
      for (const key in body) {
        if (key !== 'from' && key !== 'to' && key !== 'texts' && key !== 'html') {
          return next(validationError(`Excess property '${key}' is not allowed`));
        }
      }
      const { utils, services } = cachedHotDeps ?? await loadHotDeps();
      const results = await services.translateBatchWithPivot(utils.NormalizeLanguageCode(from), utils.NormalizeLanguageCode(to), texts as string[], html);
      res.json({ results });
    } catch (error) {
      next(error);
    }
  });

  RegisterRoutes(app);

  app.use('/ui', (req: Request, res: Response, next: NextFunction) => {
    if (req.originalUrl === '/ui') {
      return res.redirect(301, '/ui/');
    }
    next();
  }, uiStatic);

  app.use('/docs', (req: Request, res: Response, next: NextFunction) => {
    if (req.originalUrl === '/docs') {
      return res.redirect(301, '/docs/');
    }
    next();
  }, swaggerStatic, swaggerUi.serve, swaggerUi.setup(swaggerDocument));

  app.get('/', (_, res) => res.redirect(301, '/ui/'));

  app.use((_, res) => res.status(404).send('404'));

  app.use(errorHandler());

  const server = createServer(app);
  const listening = new AbortController();
  let stopping: Promise<void> | undefined;
  const stop = () => stopping ??= (async () => {
    logger.info('Shutting down server...');
    listening.abort(signal?.aborted ? signal.reason : new Error('Server startup interrupted by shutdown'));
    try {
      await new Promise<void>((resolve) => server.close(() => resolve()));
      await cleanupAllEngines();
    } finally {
      signal?.removeEventListener('abort', abort);
      process.off('SIGINT', shutdown);
      process.off('SIGTERM', shutdown);
    }
    logger.info('Server shutdown complete');
  })();
  const abort = () => { void stop().catch(error => logger.error(`Server shutdown failed: ${error}`)); };
  let shuttingDown = false;
  const shutdown = async () => {
    if (shuttingDown) return;
    shuttingDown = true;
    const timeout = setTimeout(() => {
      logger.error('Forced shutdown after timeout');
      process.exit(1);
    }, 10000);
    try { await stop(); }
    finally { clearTimeout(timeout); }
    process.exit(0);
  };
  if (handleSignals) {
    process.on('SIGINT', shutdown);
    process.on('SIGTERM', shutdown);
  }
  signal?.addEventListener('abort', abort, { once: true });
  if (signal?.aborted) abort();
  try {
    listening.signal.throwIfAborted();
    logger.info('Initializing MTranServer...');
    await fs.mkdir(config.modelDir, { recursive: true });
    listening.signal.throwIfAborted();
    await fs.mkdir(config.configDir, { recursive: true });
    listening.signal.throwIfAborted();
    await cleanupLegacyBin(config.configDir);
    listening.signal.throwIfAborted();
    logger.info('Initializing model records...');
    await initRecords();
    listening.signal.throwIfAborted();
    await warmupEngines();
    listening.signal.throwIfAborted();
    await new Promise<void>((resolve, reject) => {
      const clear = () => { server.off('error', failed); server.off('close', closed); };
      const failed = (error: Error) => { clear(); reject(error); };
      const closed = () => failed(new Error('Server startup interrupted by shutdown'));
      server.once('error', failed);
      server.once('close', closed);
      server.listen({ port: parseInt(config.port), host: config.host, signal: listening.signal }, () => {
        clear();
        resolve();
      });
    });
    listening.signal.throwIfAborted();
  } catch (error) {
    await stop();
    throw error;
  }
  logger.important(`MTranServer v${VERSION} is running!`);
  logger.important(`Web UI: http://${config.host}:${config.port}/ui/`);
  logger.important(`Swagger Docs: http://${config.host}:${config.port}/docs/`);
  logger.important(`Log level set to: ${config.logLevel}`);

  if (config.checkUpdate) checkForUpdate();

  return { server, stop };
}

export async function run() {
  const { server } = await startServer({ handleSignals: true });
  return server;
}
