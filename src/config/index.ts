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
}

let globalConfig: Config | null = null;

function getEnv(key: string, defaultValue: string): string {
  return process.env[key] || defaultValue;
}

function getBoolEnv(key: string, defaultValue: boolean): boolean {
  const value = process.env[key];
  if (value === undefined) return defaultValue;
  return value.toLowerCase() === 'true' || value === '1';
}

function getIntEnv(key: string, defaultValue: number): number {
  const value = process.env[key];
  if (value === undefined) return defaultValue;
  const parsed = parseInt(value, 10);
  return isNaN(parsed) ? defaultValue : parsed;
}

export function getConfig(): Config {
  if (globalConfig !== null) {
    return globalConfig;
  }

  const homeDir = path.join(os.homedir(), '.config', 'mtran');
  const configDir = getEnv('MT_CONFIG_DIR', path.join(homeDir, 'server'));
  const modelDir = getEnv('MT_MODEL_DIR', path.join(homeDir, 'models'));

  globalConfig = {
    logLevel: getEnv('MT_LOG_LEVEL', 'warn'),
    homeDir,
    configDir,
    modelDir,
    host: getEnv('MT_HOST', '0.0.0.0'),
    port: getEnv('MT_PORT', '8989'),
    enableWebUI: getBoolEnv('MT_ENABLE_UI', true),
    enableOfflineMode: getBoolEnv('MT_OFFLINE', false),
    workerIdleTimeout: getIntEnv('MT_WORKER_IDLE_TIMEOUT', 60),
    workersPerLanguage: getIntEnv('MT_WORKERS_PER_LANGUAGE', 1),
    apiToken: getEnv('MT_API_TOKEN', ''),
  };

  return globalConfig;
}
