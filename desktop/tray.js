import { Menu, Tray } from 'electron';

let tray = null;

export function createTray({
  icon,
  tooltip,
  messages,
  statusLabel,
  versionLabel,
  onOpenBrowserUi,
  onOpenBrowserDocs,
  onOpenRepo,
  onOpenSettings,
  onRestart,
  onOpenModels,
  onOpenConfig,
  onQuit
}) {
  if (tray) return tray;
  tray = new Tray(icon);
  tray.setToolTip(tooltip);
  tray.on('click', () => tray.popUpContextMenu());
  updateTrayMenu({
    messages,
    statusLabel,
    versionLabel,
    onOpenBrowserUi,
    onOpenBrowserDocs,
    onOpenRepo,
    onOpenSettings,
    onRestart,
    onOpenModels,
    onOpenConfig,
    onQuit
  });
  return tray;
}

export function updateTrayMenu({
  messages,
  statusLabel,
  versionLabel,
  onOpenBrowserUi,
  onOpenBrowserDocs,
  onOpenRepo,
  onOpenSettings,
  onRestart,
  onOpenModels,
  onOpenConfig,
  onQuit
}) {
  if (!tray) return;
  const contextMenu = Menu.buildFromTemplate([
    {
      label: messages.trayOpenUi,
      click: onOpenBrowserUi
    },
    {
      label: messages.trayOpenDocs,
      click: onOpenBrowserDocs
    },
    {
      label: messages.trayOpenRepo,
      click: onOpenRepo
    },
    { type: 'separator' },
    {
      label: `${messages.trayServiceStatus}: ${statusLabel}`,
      enabled: false
    },
    {
      label: messages.trayServiceManagement,
      submenu: [
        { label: messages.trayOpenSettings, click: onOpenSettings },
        { label: messages.trayRestart, click: onRestart }
      ]
    },
    { type: 'separator' },
    {
      label: messages.trayData,
      submenu: [
        { label: messages.trayOpenModels, click: onOpenModels },
        { label: messages.trayOpenConfig, click: onOpenConfig }
      ]
    },
    {
      label: `${messages.trayVersion}: ${versionLabel}`,
      enabled: false
    },
    {
      label: messages.trayQuit,
      click: onQuit
    }
  ]);
  tray.setContextMenu(contextMenu);
}
