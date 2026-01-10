import { app, dialog, nativeImage, shell, Menu, screen } from 'electron';
import fs from 'fs/promises';
import fsSync from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { loadDesktopConfig, saveDesktopConfig, resetDesktopConfig, getDefaultDesktopConfig } from './config.js';
import { resolveLocale, getMessages } from './i18n.js';
import { getFreePort, isPortAvailable } from './ports.js';
import { getServerStatus, startServerWithConfig, stopServerInstance } from './server.js';
import { createTray, updateTrayMenu } from './tray.js';
import { registerIpcHandlers } from './ipc.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

let desktopConfig = null;
let locale = 'en';
let messages = getMessages(locale);
let tray = null;
const repoUrl = 'https://github.com/xxnuo/MTranServer';
const releasesUrl = 'https://github.com/xxnuo/MTranServer/releases';
let portCheckPromise = null;
let desktopLogPath = null;
let newVersionAvailable = null;

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

function compareVersions(current, latest) {
  const parseCurrent = current.replace(/^v/, '').split('.').map(Number);
  const parseLatest = latest.replace(/^v/, '').split('.').map(Number);
  for (let i = 0; i < Math.max(parseCurrent.length, parseLatest.length); i++) {
    const a = parseCurrent[i] || 0;
    const b = parseLatest[i] || 0;
    if (a < b) return -1;
    if (a > b) return 1;
  }
  return 0;
}

async function checkForUpdate() {
  try {
    const response = await fetch(releasesUrl, {
      headers: { 'Accept': 'text/html' },
      redirect: 'follow',
    });
    if (!response.ok) return;
    const html = await response.text();
    const match = html.match(/\/xxnuo\/MTranServer\/releases\/tag\/v?([\d.]+)/);
    if (!match) return;
    const latestVersion = match[1];
    const currentVersion = app.getVersion();
    if (compareVersions(currentVersion, latestVersion) < 0) {
      newVersionAvailable = latestVersion;
      updateTray();
    }
  } catch {
  }
}

function getStatusLabel() {
  const status = getServerStatus();
  if (status === 'running') {
    return `${messages.trayServiceRunning} (${desktopConfig.server.port})`;
  }
  if (status === 'starting') return messages.trayServiceRunning;
  return messages.trayServiceStopped;
}

function getTrayIcon() {
  if (process.platform === 'darwin') {
    const iconPath16 = path.join(__dirname, '..', 'images', 'icons', 'icon@16px.png');
    const iconPath32 = path.join(__dirname, '..', 'images', 'icons', 'icon@32px.png');
    const icon = nativeImage.createFromPath(iconPath16);
    if (fsSync.existsSync(iconPath32)) {
      const icon2x = nativeImage.createFromPath(iconPath32);
      icon.addRepresentation({
        scaleFactor: 2,
        width: 32,
        height: 32,
        buffer: icon2x.toPNG()
      });
    }
    icon.setTemplateImage(true);
    return icon;
  }
  const scaleFactor = screen.getPrimaryDisplay().scaleFactor || 1;
  const size = scaleFactor >= 1.25 ? 32 : 16;
  const iconPath = path.join(__dirname, '..', 'images', 'icons', `icon@${size}px.png`);
  return nativeImage.createFromPath(iconPath);
}

function updateTray() {
  if (!tray) return;
  updateTrayMenu({
    messages,
    statusLabel: getStatusLabel(),
    versionLabel: app.getVersion(),
    newVersionLabel: newVersionAvailable,
    autoStartEnabled: app.getLoginItemSettings().openAtLogin,
    onOpenBrowserUi: () => shell.openExternal(getUiUrl(desktopConfig.server)),
    onOpenBrowserDocs: () => shell.openExternal(getDocsUrl(desktopConfig.server)),
    onOpenRepo: () => shell.openExternal(repoUrl),
    onOpenSettings: () => shell.openExternal(getSettingsUrl(desktopConfig.server)),
    onRestart: restartServer,
    onOpenModels: () => shell.openPath(desktopConfig.server.modelDir),
    onOpenConfig: () => shell.openPath(desktopConfig.server.configDir),
    onToggleAutoStart: toggleAutoStart,
    onOpenReleasePage: () => shell.openExternal(releasesUrl),
    onQuit: quitApp
  });
}

function toggleAutoStart() {
  const current = app.getLoginItemSettings().openAtLogin;
  app.setLoginItemSettings({ openAtLogin: !current });
  updateTray();
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
  const stopPromise = stopServerInstance();
  const timeoutPromise = new Promise((resolve) => setTimeout(resolve, 5000));
  await Promise.race([stopPromise, timeoutPromise]);
  app.quit();
  setTimeout(() => app.exit(0), 2000);
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

function buildDesktopResponse() {
  return { config: desktopConfig, status: getServerStatus(), version: app.getVersion() };
}

export async function startDesktop() {
  app.isQuitting = false;
  Menu.setApplicationMenu(null);
  if (app.dock?.hide) {
    app.dock.hide();
  }
  desktopConfig = await loadDesktopConfig();
  updateLocale(desktopConfig.locale);
  desktopLogPath = path.join(desktopConfig.server.configDir, 'desktop.log');
  await fs.mkdir(desktopConfig.server.configDir, { recursive: true });
  await logDesktop('desktop starting');
  await ensureWritableDirs();

  const trayIcon = getTrayIcon();
  tray = createTray({
    icon: trayIcon,
    tooltip: messages.appName,
    messages,
    statusLabel: getStatusLabel(),
    versionLabel: app.getVersion(),
    newVersionLabel: newVersionAvailable,
    autoStartEnabled: app.getLoginItemSettings().openAtLogin,
    onOpenBrowserUi: () => shell.openExternal(getUiUrl(desktopConfig.server)),
    onOpenBrowserDocs: () => shell.openExternal(getDocsUrl(desktopConfig.server)),
    onOpenRepo: () => shell.openExternal(repoUrl),
    onOpenSettings: () => shell.openExternal(getSettingsUrl(desktopConfig.server)),
    onRestart: restartServer,
    onOpenModels: () => shell.openPath(desktopConfig.server.modelDir),
    onOpenConfig: () => shell.openPath(desktopConfig.server.configDir),
    onToggleAutoStart: toggleAutoStart,
    onOpenReleasePage: () => shell.openExternal(releasesUrl),
    onQuit: quitApp
  });

  const started = await startServer();
  if (!started) {
    app.quit();
    return;
  }

  checkForUpdate();

  const getConfigResponse = async () => buildDesktopResponse();
  const applyConfig = async (config) => {
    desktopConfig = await saveDesktopConfig(config);
    updateLocale(desktopConfig.locale);
    const ok = await restartServer();
    if (!ok) return buildDesktopResponse();
    return buildDesktopResponse();
  };
  const resetConfig = async () => {
    desktopConfig = await resetDesktopConfig();
    updateLocale(desktopConfig.locale);
    const ok = await restartServer();
    if (!ok) return buildDesktopResponse();
    return buildDesktopResponse();
  };
  const restartAndRespond = async () => {
    const ok = await restartServer();
    if (!ok) return buildDesktopResponse();
    return buildDesktopResponse();
  };

  globalThis.mtranDesktopControl = {
    getConfig: getConfigResponse,
    applyConfig,
    resetConfig,
    restartServer: restartAndRespond
  };

  registerIpcHandlers({
    getConfig: getConfigResponse,
    applyConfig,
    resetConfig,
    restartServer: restartAndRespond,
    getStatus: async () => ({ status: getServerStatus() }),
    openExternal: async (url) => shell.openExternal(url),
    openPath: async (targetPath) => shell.openPath(targetPath)
  });

  app.on('before-quit', (event) => {
    if (app.isQuitting) return;
    event.preventDefault();
    quitApp();
  });

  updateTray();
}

export function focusMainWindow() {
}
