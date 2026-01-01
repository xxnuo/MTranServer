import { app, BrowserWindow, Menu } from 'electron';

let mainWindow = null;
let settingsWindow = null;

function attachContextMenu(window) {
  window.webContents.on('context-menu', () => {
    const menu = Menu.buildFromTemplate([
      { role: 'cut' },
      { role: 'copy' },
      { role: 'paste' },
      { type: 'separator' },
      { role: 'selectAll' }
    ]);
    menu.popup({ window });
  });
}

export function createMainWindow({ url, preload }) {
  if (mainWindow) return mainWindow;
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 720,
    show: false,
    autoHideMenuBar: true,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload
    }
  });

  mainWindow.loadURL(url);
  attachContextMenu(mainWindow);

  mainWindow.on('close', (event) => {
    if (!app.isQuitting && !mainWindow?.isDestroyed()) {
      event.preventDefault();
      mainWindow.hide();
    }
  });

  return mainWindow;
}

export function createSettingsWindow({ url, preload, parent }) {
  if (settingsWindow) return settingsWindow;
  settingsWindow = new BrowserWindow({
    width: 960,
    height: 540,
    show: false,
    resizable: true,
    parent,
    autoHideMenuBar: true,
    webPreferences: {
      nodeIntegration: false,
      contextIsolation: true,
      preload
    }
  });

  settingsWindow.loadURL(url);
  attachContextMenu(settingsWindow);

  settingsWindow.on('close', (event) => {
    if (!app.isQuitting && !settingsWindow?.isDestroyed()) {
      event.preventDefault();
      settingsWindow.hide();
    }
  });

  return settingsWindow;
}

export function showMainWindow() {
  if (!mainWindow) return;
  if (mainWindow.isMinimized()) mainWindow.restore();
  mainWindow.show();
  mainWindow.focus();
}

export function showSettingsWindow() {
  if (!settingsWindow) return;
  if (settingsWindow.isMinimized()) settingsWindow.restore();
  settingsWindow.show();
  settingsWindow.focus();
}

export function getMainWindow() {
  return mainWindow;
}

export function destroySettingsWindow() {
  if (settingsWindow && !settingsWindow.isDestroyed()) {
    settingsWindow.destroy();
  }
  settingsWindow = null;
}

export function updateWindowUrls({ mainUrl, settingsUrl }) {
  if (mainWindow && !mainWindow.isDestroyed() && mainWindow.webContents) {
    mainWindow.loadURL(mainUrl);
  }
  if (settingsWindow && !settingsWindow.isDestroyed() && settingsWindow.webContents) {
    settingsWindow.loadURL(settingsUrl);
  }
}
