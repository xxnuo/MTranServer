let currentServer = null;
let stopServer = null;
let status = 'stopped';
let serverModulePromise = null;
let lifecycle = Promise.resolve();
const startupControllers = new Set();

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
  '--no-check-update',
  '--fast',
  '--no-fast',
  '--lexical-shortlist',
  '--no-lexical-shortlist'
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
    serverModulePromise = import('../dist/desktop.js');
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
  process.env.MT_MAX_SENTENCE_LENGTH = String(config.maxSentenceLength);
  process.env.MT_CHECK_UPDATE = String(config.checkUpdate);
  process.env.MT_CACHE_SIZE = String(config.cacheSize);
  process.env.MT_FAST = String(config.fast ?? config.lexicalShortlist ?? false);
  process.env.MT_MODEL_DIR = config.modelDir;
  process.env.MT_CONFIG_DIR = config.configDir;
}

export function startServerWithConfig(config) {
  const controller = new AbortController();
  startupControllers.add(controller);
  const starting = lifecycle.then(async () => {
    await stopCurrentServer();
    controller.signal.throwIfAborted();
    status = 'starting';
    applyConfigToEnv(config);
    const serverModule = await loadServerModule();
    controller.signal.throwIfAborted();
    serverModule.resetConfig();
    const result = await serverModule.startServer({ handleSignals: false, signal: controller.signal });
    currentServer = result.server;
    stopServer = result.stop;
    controller.signal.throwIfAborted();
    status = 'running';
    return currentServer;
  }).catch(async error => {
    await stopCurrentServer();
    throw error;
  }).finally(() => startupControllers.delete(controller));
  lifecycle = starting.catch(() => {});
  return starting;
}

export async function updateServerFastMode(fast) {
  const serverModule = await loadServerModule();
  serverModule.setConfig({ fast });
  process.env.MT_FAST = String(fast);
}

async function stopCurrentServer() {
  if (stopServer) {
    status = 'stopping';
    try { await stopServer(); }
    finally { currentServer = null; stopServer = null; status = 'stopped'; }
  } else status = 'stopped';
}

export function stopServerInstance() {
  status = 'stopping';
  for (const controller of startupControllers) controller.abort(new Error('Server startup interrupted by shutdown'));
  const stopping = lifecycle.then(stopCurrentServer);
  lifecycle = stopping.catch(() => {});
  return stopping;
}

export function getServerStatus() {
  return status;
}
