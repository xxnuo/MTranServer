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
  maxSentenceLength: number;
  checkUpdate: boolean;
  cacheSize: number;
  fullwidthZhPunctuation: boolean;
}

let globalConfig: Config | null = null;
let fileConfigCache: Partial<Config> | null = null;

function getConfigFilePath(homeDir: string) {
  return path.join(homeDir, 'server.json');
}

function readConfigFile(homeDir: string): Partial<Config> {
  if (fileConfigCache) return fileConfigCache;
  const configPath = getConfigFilePath(homeDir);
  try {
    const raw = fs.readFileSync(configPath, 'utf8');
    const parsed = JSON.parse(raw);
    if (parsed && typeof parsed === 'object') {
      fileConfigCache = parsed as Partial<Config>;
      return fileConfigCache;
    }
  } catch {
    return {};
  }
  return {};
}

function getArgValue(flag: string): string | null {
  const index = process.argv.indexOf(flag);
  if (index > -1 && index + 1 < process.argv.length) {
    const nextArg = process.argv[index + 1];
    if (!nextArg.startsWith('-')) {
      return nextArg;
    }
  }
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
  if (process.argv.includes(flag)) return true;
  const val = getArgValue(flag);
  if (val !== null) return val.toLowerCase() === 'true' || val === '1';

  const noFlag = `--no-${flag.replace(/^--/, '')}`;
  if (process.argv.includes(noFlag)) return false;

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

export function getConfig(): Config {
  if (globalConfig !== null) {
    return globalConfig;
  }

  const homeDir = path.join(os.homedir(), '.config', 'mtran');
  const fileConfig = readConfigFile(homeDir);

  const configDir = getString('--config-dir', 'MT_CONFIG_DIR', fileConfig.configDir || path.join(homeDir, 'server'));
  const localModelsDir = path.join(process.cwd(), 'models');
  const defaultModelDir = fs.existsSync(localModelsDir) ? localModelsDir : path.join(homeDir, 'models');
  const modelDir = getString('--model-dir', 'MT_MODEL_DIR', fileConfig.modelDir || defaultModelDir);
  const logDir = getString('--log-dir', 'MT_LOG_DIR', fileConfig.logDir || path.join(homeDir, 'logs'));

  globalConfig = {
    homeDir,
    configDir,
    modelDir,
    logDir,

    logLevel: getString('--log-level', 'MT_LOG_LEVEL', fileConfig.logLevel || 'warn'),
    host: getString('--host', 'MT_HOST', fileConfig.host || '0.0.0.0'),
    port: getString('--port', 'MT_PORT', fileConfig.port || '8989'),

    enableWebUI: getBool('--ui', 'MT_ENABLE_UI', fileConfig.enableWebUI ?? true),
    enableOfflineMode: getBool('--offline', 'MT_OFFLINE', fileConfig.enableOfflineMode ?? false),

    workerIdleTimeout: getInt('--worker-idle-timeout', 'MT_WORKER_IDLE_TIMEOUT', fileConfig.workerIdleTimeout ?? 60),
    workersPerLanguage: getInt('--workers-per-language', 'MT_WORKERS_PER_LANGUAGE', fileConfig.workersPerLanguage ?? 1),
    maxSentenceLength: getInt('--max-sentence-length', 'MT_MAX_SENTENCE_LENGTH', fileConfig.maxSentenceLength ?? 512),
    fullwidthZhPunctuation: getBool('--fullwidth-zh-punctuation', 'MT_FULLWIDTH_ZH_PUNCTUATION', fileConfig.fullwidthZhPunctuation ?? true),

    apiToken: getString('--api-token', 'MT_API_TOKEN', fileConfig.apiToken || ''),

    logToFile: getBool('--log-to-file', 'MT_LOG_TO_FILE', fileConfig.logToFile ?? false),
    logConsole: getBool('--log-console', 'MT_LOG_CONSOLE', fileConfig.logConsole ?? true),
    logRequests: getBool('--log-requests', 'MT_LOG_REQUESTS', fileConfig.logRequests ?? false),

    checkUpdate: getBool('--check-update', 'MT_CHECK_UPDATE', fileConfig.checkUpdate ?? true),

    cacheSize: getInt('--cache-size', 'MT_CACHE_SIZE', fileConfig.cacheSize ?? 1000),
  };

  return globalConfig;
}

export function setConfig(config: Partial<Config>) {
  const current = getConfig();
  globalConfig = { ...current, ...config };
}

export function resetConfig() {
  globalConfig = null;
  fileConfigCache = null;
}

export function saveConfigFile(config: Partial<Config>) {
  const current = getConfig();
  const next = { ...current, ...config };
  const configPath = getConfigFilePath(current.homeDir);
  fs.mkdirSync(current.homeDir, { recursive: true });
  fs.writeFileSync(configPath, JSON.stringify(next, null, 2), 'utf8');
  fileConfigCache = next;
}

export function clearConfigFile() {
  const homeDir = path.join(os.homedir(), '.config', 'mtran');
  const configPath = getConfigFilePath(homeDir);
  try {
    if (fs.existsSync(configPath)) {
      fs.unlinkSync(configPath);
    }
  } catch {
    return;
  }
}
