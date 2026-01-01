import { app, dialog, nativeImage, shell, Menu } from 'electron';
import fs from 'fs/promises';
import fsSync from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { loadDesktopConfig, saveDesktopConfig, resetDesktopConfig, getDefaultDesktopConfig } from './config.js';
import { resolveLocale, getMessages } from './i18n.js';
import { getFreePort, isPortAvailable } from './ports.js';
import { getServerStatus, startServerWithConfig, stopServerInstance } from './server.js';
import {
  createMainWindow,
  createSettingsWindow,
  showMainWindow,
  showSettingsWindow,
  updateWindowUrls
} from './windows.js';
import { createTray, updateTrayMenu } from './tray.js';
import { registerIpcHandlers } from './ipc.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let desktopConfig = null;
let locale = 'en';
let messages = getMessages(locale);
let tray = null;
const repoUrl = 'https://github.com/xxnuo/MTranServer';
let portCheckPromise = null;
let desktopLogPath = null;

function getLocalHost(host) {
  if (!host || host === '0.0.0.0') return '127.0.0.1';
  return host;
}

function getUiUrl(server) {
  return `http://${getLocalHost(server.host)}:${server.port}/ui/`;
}

function getDocsUrl(server) {
  return `http://${getLocalHost(server.host)}:${server.port}/docs/`;
}

function getSettingsUrl(server) {
  return `http://${getLocalHost(server.host)}:${server.port}/ui/settings`;
}

function getStatusLabel() {
  const status = getServerStatus();
  if (status === 'running') {
    return `${messages.trayServiceRunning} (${desktopConfig.server.port})`;
  }
  if (status === 'starting') return messages.trayServiceRunning;
  return messages.trayServiceStopped;
}

function updateTray() {
  if (!tray) return;
  updateTrayMenu({
    messages,
    statusLabel: getStatusLabel(),
    versionLabel: app.getVersion(),
    onOpenMain: showMainWindow,
    onOpenBrowserUi: () => shell.openExternal(getUiUrl(desktopConfig.server)),
    onOpenBrowserDocs: () => shell.openExternal(getDocsUrl(desktopConfig.server)),
    onOpenRepo: () => shell.openExternal(repoUrl),
    onOpenSettings: showSettingsWindow,
    onRestart: restartServer,
    onOpenModels: () => shell.openPath(desktopConfig.server.modelDir),
    onOpenConfig: () => shell.openPath(desktopConfig.server.configDir),
    onQuit: quitApp
  });
}

async function logDesktop(message, error) {
  if (!desktopLogPath) return;
  const timestamp = new Date().toISOString();
  const details = error ? ` ${error.stack || error.message || error}` : '';
  try {
    await fs.appendFile(desktopLogPath, `[${timestamp}] ${message}${details}\n`, 'utf8');
  } catch {
    return;
  }
}

async function ensurePortAvailable() {
  if (portCheckPromise) return portCheckPromise;
  portCheckPromise = (async () => {
    const host = desktopConfig.server.host || '0.0.0.0';
    const available = await isPortAvailable(desktopConfig.server.port, host);
    if (available) return true;

    await logDesktop(`port in use ${desktopConfig.server.port}`);
    const result = await dialog.showMessageBox({
      type: 'warning',
      buttons: [messages.portInUseUseRandom, messages.portInUseQuit],
      defaultId: 0,
      cancelId: 1,
      message: messages.portInUseTitle,
      detail: messages.portInUseDetail.replace('{port}', String(desktopConfig.server.port))
    });

    if (result.response === 0) {
      const newPort = await getFreePort();
      await logDesktop(`use random port ${newPort}`);
      desktopConfig.server.port = newPort;
      desktopConfig = await saveDesktopConfig(desktopConfig);
      return true;
    }
    await logDesktop('quit after port in use');
    await quitApp();
    return false;
  })();
  try {
    return await portCheckPromise;
  } finally {
    portCheckPromise = null;
  }
}

async function startServer() {
  const ok = await ensurePortAvailable();
  if (!ok) return false;
  try {
    await logDesktop('starting server');
    await startServerWithConfig(desktopConfig.server);
    await logDesktop('server started');
    return true;
  } catch (error) {
    await logDesktop('server start failed', error);
    dialog.showMessageBox({
      type: 'error',
      message: messages.serverStartFailed,
      detail: messages.serverStartFailedDetail
    });
    return false;
  }
}

async function restartServer() {
  try {
    await logDesktop('restarting server');
    await stopServerInstance();
    await ensureWritableDirs();
    const ok = await startServer();
    if (!ok) return false;
    updateWindowUrls({
      mainUrl: getUiUrl(desktopConfig.server),
      settingsUrl: getSettingsUrl(desktopConfig.server)
    });
    updateTray();
    return true;
  } catch {
    dialog.showMessageBox({
      type: 'error',
      message: messages.serverRestartFailed
    });
    return false;
  }
}

async function quitApp() {
  await logDesktop('quit app');
  app.isQuitting = true;
  await stopServerInstance();
  app.quit();
}

function updateLocale(nextLocale) {
  locale = resolveLocale(nextLocale, app.getLocale());
  messages = getMessages(locale);
  updateTray();
}

async function ensureWritableDirs() {
  const defaults = getDefaultDesktopConfig();
  const updates = {};
  const targets = [
    ['modelDir', defaults.server.modelDir],
    ['logDir', defaults.server.logDir],
    ['configDir', defaults.server.configDir]
  ];

  for (const [key, fallback] of targets) {
    const target = desktopConfig.server[key];
    if (!target) {
      updates[key] = fallback;
      continue;
    }
    try {
      await fs.mkdir(target, { recursive: true });
      await fs.access(target, fsSync.constants.W_OK);
    } catch {
      updates[key] = fallback;
    }
  }

  if (Object.keys(updates).length > 0) {
    desktopConfig = await saveDesktopConfig({
      ...desktopConfig,
      server: {
        ...desktopConfig.server,
        ...updates
      }
    });
  }
}

function getLoadingUrl() {
  const html = `
  <!doctype html>
  <html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>${messages.appName}</title>
    <style>
      html,body{margin:0;height:100%;font-family:system-ui,Segoe UI,Arial,sans-serif;background:#f5f5f5;color:#1f2937}
      .wrap{height:100%;display:flex;align-items:center;justify-content:center}
      .card{padding:24px 32px;border:1px solid #e5e7eb;border-radius:16px;background:#fff;box-shadow:0 12px 30px rgba(0,0,0,0.08);text-align:center;min-width:260px}
      .title{font-size:18px;font-weight:600;margin-bottom:8px}
      .desc{font-size:14px;color:#6b7280}
      .dot{display:inline-block;animation:blink 1.2s infinite}
      @keyframes blink{0%,100%{opacity:.2}50%{opacity:1}}
    </style>
  </head>
  <body>
    <div class="wrap">
      <div class="card">
        <div class="title">${messages.appName}</div>
        <div class="desc">${messages.trayServiceStatus}: ${messages.trayServiceRunning} <span class="dot">...</span></div>
      </div>
    </div>
  </body>
  </html>
  `;
  return `data:text/html;charset=utf-8,${encodeURIComponent(html)}`;
}

export async function startDesktop() {
  app.isQuitting = false;
  Menu.setApplicationMenu(null);
  desktopConfig = await loadDesktopConfig();
  updateLocale(desktopConfig.locale);
  desktopLogPath = path.join(desktopConfig.server.configDir, 'desktop.log');
  await fs.mkdir(desktopConfig.server.configDir, { recursive: true });
  await logDesktop('desktop starting');
  await ensureWritableDirs();

  const preloadPath = path.join(__dirname, 'preload.cjs');
  const loadingUrl = getLoadingUrl();
  const mainWindow = createMainWindow({ url: loadingUrl, preload: preloadPath });
  createSettingsWindow({ url: loadingUrl, preload: preloadPath, parent: mainWindow });
  mainWindow.once('ready-to-show', () => mainWindow.show());

  const iconPath = path.join(__dirname, '..', 'images', 'icons', 'icon@16px.png');
  const trayIcon = nativeImage.createFromPath(iconPath);
  tray = createTray({
    icon: trayIcon,
    tooltip: messages.appName,
    messages,
    statusLabel: getStatusLabel(),
    versionLabel: app.getVersion(),
    onOpenMain: showMainWindow,
    onOpenBrowserUi: () => shell.openExternal(getUiUrl(desktopConfig.server)),
    onOpenBrowserDocs: () => shell.openExternal(getDocsUrl(desktopConfig.server)),
    onOpenRepo: () => shell.openExternal(repoUrl),
    onOpenSettings: showSettingsWindow,
    onRestart: restartServer,
    onOpenModels: () => shell.openPath(desktopConfig.server.modelDir),
    onOpenConfig: () => shell.openPath(desktopConfig.server.configDir),
    onQuit: quitApp
  });

  const started = await startServer();
  if (!started) {
    app.quit();
    return;
  }

  updateWindowUrls({
    mainUrl: getUiUrl(desktopConfig.server),
    settingsUrl: getSettingsUrl(desktopConfig.server)
  });

  registerIpcHandlers({
    getConfig: async () => ({
      config: desktopConfig,
      status: getServerStatus(),
      version: app.getVersion()
    }),
    applyConfig: async (config) => {
      desktopConfig = await saveDesktopConfig(config);
      updateLocale(desktopConfig.locale);
      const ok = await restartServer();
      if (!ok) return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
      return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
    },
    resetConfig: async () => {
      desktopConfig = await resetDesktopConfig();
      updateLocale(desktopConfig.locale);
      const ok = await restartServer();
      if (!ok) return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
      return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
    },
    restartServer: async () => {
      const ok = await restartServer();
      if (!ok) return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
      return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
    },
    getStatus: async () => ({ status: getServerStatus() }),
    openExternal: async (url) => shell.openExternal(url),
    openPath: async (targetPath) => shell.openPath(targetPath)
  });

  app.on('activate', () => showMainWindow());
  app.on('before-quit', (event) => {
    if (app.isQuitting) return;
    event.preventDefault();
    quitApp();
  });
  app.on('window-all-closed', (event) => event.preventDefault());

  updateTray();
}

export function focusMainWindow() {
  showMainWindow();
}
