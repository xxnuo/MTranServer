import express from 'express';
import fs from 'fs/promises';
import { getConfig } from '@/config/index.js';
import * as logger from '@/logger/index.js';
import { setupRoutes } from '@/routes/index.js';
import { initRecords } from '@/models/index.js';
import { cleanupAllEngines } from '@/services/index.js';
import { cleanupLegacyBin } from '@/assets/index.js';
import { requestId, errorHandler } from '@/middleware/index.js';

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

  setupRoutes(app, config.apiToken);
  
  app.use(errorHandler());

  const server = app.listen(parseInt(config.port), config.host, () => {
    logger.info(`HTTP Service URL: http://${config.host}:${config.port}`);
    logger.info(`Log level set to: ${config.logLevel}`);
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
