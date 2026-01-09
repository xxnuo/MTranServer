import express, { Request, Response, NextFunction } from 'express';
import fs from 'fs/promises';
import swaggerUi from 'swagger-ui-express';
import { getConfig, setConfig, resetConfig, saveConfigFile, clearConfigFile } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import { initRecords } from '@/models/index.js';
import { cleanupAllEngines } from '@/services/index.js';
import { cleanupLegacyBin } from '@/assets/index.js';
import { requestId, errorHandler, cors, requestLogger } from '@/middleware/index.js';
import { RegisterRoutes } from '@/generated/routes.js';
import swaggerDocument from '@/generated/swagger.json';
import { uiStatic } from '@/middleware/ui.js';
import { swaggerStatic } from '@/middleware/swagger.js';
import { checkForUpdate } from '@/utils/update-checker.js';
import { VERSION } from '@/version';

export async function startServer({ handleSignals = true } = {}) {
  const config = getConfig();

  logger.info('Initializing MTranServer...');

  await fs.mkdir(config.modelDir, { recursive: true });
  await fs.mkdir(config.configDir, { recursive: true });

  await cleanupLegacyBin(config.configDir);

  logger.info('Initializing model records...');
  await initRecords();

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
          maxLengthBreak: current.maxLengthBreak,
          checkUpdate: current.checkUpdate,
          cacheSize: current.cacheSize,
          modelDir: current.modelDir,
          configDir: current.configDir
        }
      },
      status: 'running',
      version: VERSION
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
      maxLengthBreak: toNumber(input.maxLengthBreak, current.maxLengthBreak),
      checkUpdate: toBool(input.checkUpdate, current.checkUpdate),
      cacheSize: toNumber(input.cacheSize, current.cacheSize),
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
      res.json(payload);
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

  const server = app.listen(parseInt(config.port), config.host, () => {
    logger.important(`MTranServer v${VERSION} is running!`);
    logger.important(`Web UI: http://${config.host}:${config.port}/ui/`);
    logger.important(`Swagger Docs: http://${config.host}:${config.port}/docs/`);
    logger.important(`Log level set to: ${config.logLevel}`);

    if (config.checkUpdate) {
      checkForUpdate();
    }
  });

  const stop = async () => {
    logger.info('Shutting down server...');
    cleanupAllEngines();
    await new Promise<void>((resolve) => server.close(() => resolve()));
    logger.info('Server shutdown complete');
  };

  if (handleSignals) {
    let shuttingDown = false;
    const shutdown = async () => {
      if (shuttingDown) return;
      shuttingDown = true;
      const timeout = setTimeout(() => {
        logger.error('Forced shutdown after timeout');
        process.exit(1);
      }, 10000);
      await stop();
      clearTimeout(timeout);
      process.exit(0);
    };

    process.on('SIGINT', shutdown);
    process.on('SIGTERM', shutdown);
  }

  return { server, stop };
}

export async function run() {
  const { server } = await startServer({ handleSignals: true });
  return server;
}
