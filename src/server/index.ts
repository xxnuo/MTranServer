import express, { Request, Response, NextFunction } from 'express';
import fs from 'fs/promises';
import swaggerUi from 'swagger-ui-express';
import { getConfig } from '@/config/index.js';
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

export async function run() {
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
  app.use(requestLogger());

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
    logger.important(`Web UI: http://${config.host}:${config.port}/ui/`);
    logger.important(`Swagger Docs: http://${config.host}:${config.port}/docs/`);
    logger.important(`Log level set to: ${config.logLevel}`);

    if (config.checkUpdate) {
      checkForUpdate();
    }
  });

  const shutdown = async () => {
    logger.info('Shutting down server...');

    cleanupAllEngines();

    server.close(() => {
      logger.info('Server shutdown complete');
      process.exit(0);
    });

    setTimeout(() => {
      logger.error('Forced shutdown after timeout');
      process.exit(1);
    }, 10000);
  };

  process.on('SIGINT', shutdown);
  process.on('SIGTERM', shutdown);

  return server;
}
