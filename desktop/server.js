let currentServer = null;
let stopServer = null;
let status = 'stopped';
let serverModulePromise = null;

const valueFlags = new Set([
  '--host',
  '--port',
  '--log-level',
  '--config-dir',
  '--model-dir',
  '--log-dir',
  '--worker-idle-timeout',
  '--workers-per-language',
  '--api-token',
  '--max-length-break',
  '--cache-size'
]);

const booleanFlags = new Set([
  '--ui',
  '--no-ui',
  '--offline',
  '--log-to-file',
  '--log-console',
  '--no-log-console',
  '--log-requests',
  '--check-update',
  '--no-check-update'
]);

function stripServerArgs() {
  const nextArgs = [];
  for (let i = 0; i < process.argv.length; i += 1) {
    const arg = process.argv[i];
    if (valueFlags.has(arg)) {
      i += 1;
      continue;
    }
    const flagMatch = arg.split('=')[0];
    if (valueFlags.has(flagMatch) || booleanFlags.has(flagMatch)) {
      continue;
    }
    if (booleanFlags.has(arg)) {
      continue;
    }
    nextArgs.push(arg);
  }
  process.argv = nextArgs;
}

async function loadServerModule() {
  if (!serverModulePromise) {
    serverModulePromise = import('../dist/desktop-server.js');
  }
  return serverModulePromise;
}

function applyConfigToEnv(config) {
  stripServerArgs();
  process.env.MT_HOST = config.host;
  process.env.MT_PORT = String(config.port);
  process.env.MT_LOG_LEVEL = config.logLevel;
  process.env.MT_ENABLE_UI = String(config.enableWebUI);
  process.env.MT_OFFLINE = String(config.enableOfflineMode);
  process.env.MT_WORKER_IDLE_TIMEOUT = String(config.workerIdleTimeout);
  process.env.MT_WORKERS_PER_LANGUAGE = String(config.workersPerLanguage);
  process.env.MT_API_TOKEN = config.apiToken || '';
  process.env.MT_LOG_DIR = config.logDir;
  process.env.MT_LOG_TO_FILE = String(config.logToFile);
  process.env.MT_LOG_CONSOLE = String(config.logConsole);
  process.env.MT_LOG_REQUESTS = String(config.logRequests);
  process.env.MT_MAX_LENGTH_BREAK = String(config.maxLengthBreak);
  process.env.MT_CHECK_UPDATE = String(config.checkUpdate);
  process.env.MT_CACHE_SIZE = String(config.cacheSize);
  process.env.MT_MODEL_DIR = config.modelDir;
  process.env.MT_CONFIG_DIR = config.configDir;
}

export async function startServerWithConfig(config) {
  applyConfigToEnv(config);
  const serverModule = await loadServerModule();
  serverModule.resetConfig();
  status = 'starting';
  const result = await serverModule.startServer({ handleSignals: false });
  currentServer = result.server;
  stopServer = result.stop;
  status = 'running';
  return currentServer;
}

export async function stopServerInstance() {
  if (!stopServer) {
    status = 'stopped';
    return;
  }
  status = 'stopping';
  await stopServer();
  currentServer = null;
  stopServer = null;
  status = 'stopped';
}

export function getServerStatus() {
  return status;
}
