import fs from 'fs';
import fsPromises from 'fs/promises';
import os from 'os';
import path from 'path';
import YAML from 'yaml';

const configRoot = path.join(os.homedir(), '.config', 'mtran');
const serverConfigDir = path.join(configRoot, 'server');
const desktopConfigPath = path.join(serverConfigDir, 'desktop.yml');

function getDefaultModelDir() {
  const localModelsDir = path.join(process.cwd(), 'models');
  if (fs.existsSync(localModelsDir)) {
    return localModelsDir;
  }
  return path.join(configRoot, 'models');
}

export function getDesktopConfigPath() {
  return desktopConfigPath;
}

export function getDesktopConfigDir() {
  return serverConfigDir;
}

export function getDefaultDesktopConfig() {
  return {
    locale: 'system',
    server: {
      host: '0.0.0.0',
      port: 8989,
      logLevel: 'warn',
      enableWebUI: true,
      enableOfflineMode: false,
      workerIdleTimeout: 60,
      workersPerLanguage: 1,
      apiToken: '',
      logDir: path.join(configRoot, 'logs'),
      logToFile: false,
      logConsole: true,
      logRequests: false,
      maxSentenceLength: 128,
      checkUpdate: true,
      cacheSize: 1000,
      modelDir: getDefaultModelDir(),
      configDir: serverConfigDir
    }
  };
}

function normalizeConfig(input) {
  const defaults = getDefaultDesktopConfig();
  const server = input?.server || {};
  return {
    locale: input?.locale || defaults.locale,
    server: {
      host: server.host ?? defaults.server.host,
      port: Number.isFinite(Number(server.port)) ? Number(server.port) : defaults.server.port,
      logLevel: server.logLevel ?? defaults.server.logLevel,
      enableWebUI: typeof server.enableWebUI === 'boolean' ? server.enableWebUI : defaults.server.enableWebUI,
      enableOfflineMode: typeof server.enableOfflineMode === 'boolean' ? server.enableOfflineMode : defaults.server.enableOfflineMode,
      workerIdleTimeout: Number.isFinite(Number(server.workerIdleTimeout))
        ? Number(server.workerIdleTimeout)
        : defaults.server.workerIdleTimeout,
      workersPerLanguage: Number.isFinite(Number(server.workersPerLanguage))
        ? Number(server.workersPerLanguage)
        : defaults.server.workersPerLanguage,
      apiToken: server.apiToken ?? defaults.server.apiToken,
      logDir: server.logDir ?? defaults.server.logDir,
      logToFile: typeof server.logToFile === 'boolean' ? server.logToFile : defaults.server.logToFile,
      logConsole: typeof server.logConsole === 'boolean' ? server.logConsole : defaults.server.logConsole,
      logRequests: typeof server.logRequests === 'boolean' ? server.logRequests : defaults.server.logRequests,
      maxSentenceLength: Number.isFinite(Number(server.maxSentenceLength))
        ? Number(server.maxSentenceLength)
        : defaults.server.maxSentenceLength,
      checkUpdate: typeof server.checkUpdate === 'boolean' ? server.checkUpdate : defaults.server.checkUpdate,
      cacheSize: Number.isFinite(Number(server.cacheSize)) ? Number(server.cacheSize) : defaults.server.cacheSize,
      modelDir: server.modelDir ?? defaults.server.modelDir,
      configDir: server.configDir ?? defaults.server.configDir
    }
  };
}

function getDiffConfig(config) {
  const defaults = getDefaultDesktopConfig();
  const diff = {};
  if (config.locale !== defaults.locale) {
    diff.locale = config.locale;
  }
  const serverDiff = {};
  const serverKeys = Object.keys(defaults.server);
  for (const key of serverKeys) {
    if (config.server[key] !== defaults.server[key]) {
      serverDiff[key] = config.server[key];
    }
  }
  if (Object.keys(serverDiff).length > 0) {
    diff.server = serverDiff;
  }
  return diff;
}

function isConfigDefault(config) {
  const diff = getDiffConfig(config);
  return Object.keys(diff).length === 0;
}

export async function loadDesktopConfig() {
  try {
    const raw = await fsPromises.readFile(desktopConfigPath, 'utf8');
    const parsed = YAML.parse(raw);
    const normalized = normalizeConfig(parsed);
    const diff = getDiffConfig(normalized);
    if (Object.keys(diff).length === 0) {
      try {
        await fsPromises.unlink(desktopConfigPath);
      } catch {
      }
    } else {
      const currentDiff = getDiffConfig(normalizeConfig(parsed));
      const parsedKeys = Object.keys(parsed?.server || {});
      const defaultKeys = Object.keys(getDefaultDesktopConfig().server);
      const hasRemovedKeys = parsedKeys.some(k => !defaultKeys.includes(k));
      if (hasRemovedKeys) {
        await fsPromises.mkdir(serverConfigDir, { recursive: true });
        const data = YAML.stringify(diff);
        await fsPromises.writeFile(desktopConfigPath, data, 'utf8');
      }
    }
    return normalized;
  } catch {
    return getDefaultDesktopConfig();
  }
}

export async function saveDesktopConfig(config) {
  const normalized = normalizeConfig(config);
  const diff = getDiffConfig(normalized);
  if (Object.keys(diff).length === 0) {
    try {
      await fsPromises.unlink(desktopConfigPath);
    } catch {
    }
  } else {
    await fsPromises.mkdir(serverConfigDir, { recursive: true });
    const data = YAML.stringify(diff);
    await fsPromises.writeFile(desktopConfigPath, data, 'utf8');
  }
  return normalized;
}

export async function resetDesktopConfig() {
  try {
    await fsPromises.unlink(desktopConfigPath);
  } catch {
  }
  return getDefaultDesktopConfig();
}
