#!/usr/bin/env node

import { run } from '@/server/index.js';
import * as logger from '@/logger/index.js';

if (process.argv.includes('--help') || process.argv.includes('-h')) {
  console.log(`
MTranServer - Machine Translation Server

Usage: mtranserver [options]

Options:
  --host <ip>              Server host address (default: 0.0.0.0)
  --port <port>            Server port (default: 8989)
  --log-level <level>      Log level (debug, info, warn, error) (default: warn)
  --config-dir <path>      Config directory
  --model-dir <path>       Model directory
  --log-dir <path>         Log directory
  --ui / --no-ui           Enable/Disable web UI (default: true)
  --offline                Enable offline mode (default: false)
  --worker-idle-timeout    Worker idle timeout in seconds (default: 60)
  --workers-per-language   Number of workers per language pair (default: 1)
  --api-token <token>      API access token
  --log-to-file            Enable logging to file (default: false)
  --log-console            Enable logging to console (default: true)
  --no-log-console         Disable logging to console

Environment Variables:
  MT_HOST, MT_PORT, MT_LOG_LEVEL, MT_CONFIG_DIR, MT_MODEL_DIR,
  MT_LOG_DIR, MT_ENABLE_UI, MT_OFFLINE, MT_WORKER_IDLE_TIMEOUT,
  MT_WORKERS_PER_LANGUAGE, MT_API_TOKEN, MT_LOG_TO_FILE, MT_LOG_CONSOLE
`);
  process.exit(0);
}

process.on('uncaughtException', (error) => {
  logger.fatal('Uncaught Exception:', error);
});

process.on('unhandledRejection', (reason, promise) => {
  logger.error('Unhandled Rejection at:', promise, 'reason:', reason);
});

run().catch(error => {
  logger.fatal('Failed to start server:', error);
});