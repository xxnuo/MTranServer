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
      checkUpdate: true,
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
      checkUpdate: typeof server.checkUpdate === 'boolean' ? server.checkUpdate : defaults.server.checkUpdate,
      modelDir: server.modelDir ?? defaults.server.modelDir,
      configDir: server.configDir ?? defaults.server.configDir
    }
  };
}

export async function loadDesktopConfig() {
  try {
    const raw = await fsPromises.readFile(desktopConfigPath, 'utf8');
    const parsed = YAML.parse(raw);
    return normalizeConfig(parsed);
  } catch (error) {
    const defaults = getDefaultDesktopConfig();
    await saveDesktopConfig(defaults);
    return defaults;
  }
}

export async function saveDesktopConfig(config) {
  const normalized = normalizeConfig(config);
  await fsPromises.mkdir(serverConfigDir, { recursive: true });
  const data = YAML.stringify(normalized);
  await fsPromises.writeFile(desktopConfigPath, data, 'utf8');
  return normalized;
}

export async function resetDesktopConfig() {
  const defaults = getDefaultDesktopConfig();
  await saveDesktopConfig(defaults);
  return defaults;
}
