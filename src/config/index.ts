import fs from 'fs';
import os from 'os';
import path from 'path';

export interface Config {
  logLevel: string;
  homeDir: string;
  configDir: string;
  modelDir: string;
  host: string;
  port: string;
  enableWebUI: boolean;
  enableOfflineMode: boolean;
  workerIdleTimeout: number;
  workersPerLanguage: number;
  apiToken: string;
  logDir: string;
  logToFile: boolean;
  logConsole: boolean;
  logRequests: boolean;
  maxLengthBreak: number;
  checkUpdate: boolean;
  cacheSize: number;
}

let globalConfig: Config | null = null;

// --- Helper Functions for Parsing ---

function getArgValue(flag: string): string | null {
  const index = process.argv.indexOf(flag);
  if (index > -1 && index + 1 < process.argv.length) {
    const nextArg = process.argv[index + 1];
    if (!nextArg.startsWith('-')) {
      return nextArg;
    }
  }
  // Handle case like --flag=value
  for (const arg of process.argv) {
    if (arg.startsWith(`${flag}=`)) {
      return arg.split('=')[1];
    }
  }
  return null;
}

function hasFlag(flag: string): boolean {
  return process.argv.includes(flag);
}

function getString(flag: string, envKey: string, defaultValue: string): string {
  const argVal = getArgValue(flag);
  if (argVal !== null) return argVal;
  return process.env[envKey] || defaultValue;
}

function getBool(flag: string, envKey: string, defaultValue: boolean): boolean {
  // Check for explicit true flags: --ui, --ui=true
  if (process.argv.includes(flag)) return true;
  const val = getArgValue(flag);
  if (val !== null) return val.toLowerCase() === 'true' || val === '1';

  // Check for explicit false flags (common convention): --no-ui
  const noFlag = `--no-${flag.replace(/^--/, '')}`;
  if (process.argv.includes(noFlag)) return false;

  // Fallback to Env
  const envVal = process.env[envKey];
  if (envVal !== undefined) {
    return envVal.toLowerCase() === 'true' || envVal === '1';
  }
  return defaultValue;
}

function getInt(flag: string, envKey: string, defaultValue: number): number {
  const argVal = getArgValue(flag);
  if (argVal !== null) {
    const parsed = parseInt(argVal, 10);
    if (!isNaN(parsed)) return parsed;
  }

  const envVal = process.env[envKey];
  if (envVal !== undefined) {
    const parsed = parseInt(envVal, 10);
    if (!isNaN(parsed)) return parsed;
  }

  return defaultValue;
}

// --- Main Config Logic ---

export function getConfig(): Config {
  if (globalConfig !== null) {
    return globalConfig;
  }

  const homeDir = path.join(os.homedir(), '.config', 'mtran');
  
  // Resolve directories first as they might depend on defaults or CLI
  const configDir = getString('--config-dir', 'MT_CONFIG_DIR', path.join(homeDir, 'server'));
  const localModelsDir = path.join(process.cwd(), 'models');
  const defaultModelDir = fs.existsSync(localModelsDir) ? localModelsDir : path.join(homeDir, 'models');
  const modelDir = getString('--model-dir', 'MT_MODEL_DIR', defaultModelDir);
  const logDir = getString('--log-dir', 'MT_LOG_DIR', path.join(homeDir, 'logs'));

  globalConfig = {
    homeDir,
    configDir,
    modelDir,
    logDir,

    logLevel: getString('--log-level', 'MT_LOG_LEVEL', 'warn'),
    host: getString('--host', 'MT_HOST', '0.0.0.0'),
    port: getString('--port', 'MT_PORT', '8989'),

    enableWebUI: getBool('--ui', 'MT_ENABLE_UI', true),
    enableOfflineMode: getBool('--offline', 'MT_OFFLINE', false),

    workerIdleTimeout: getInt('--worker-idle-timeout', 'MT_WORKER_IDLE_TIMEOUT', 60),
    workersPerLanguage: getInt('--workers-per-language', 'MT_WORKERS_PER_LANGUAGE', 1),
    maxLengthBreak: getInt('--max-length-break', 'MT_MAX_LENGTH_BREAK', 128),

    apiToken: getString('--api-token', 'MT_API_TOKEN', ''),

    logToFile: getBool('--log-to-file', 'MT_LOG_TO_FILE', false),
    logConsole: getBool('--log-console', 'MT_LOG_CONSOLE', true),
    logRequests: getBool('--log-requests', 'MT_LOG_REQUESTS', false),

    checkUpdate: getBool('--check-update', 'MT_CHECK_UPDATE', true),

    cacheSize: getInt('--cache-size', 'MT_CACHE_SIZE', 0),
  };

  return globalConfig;
}
