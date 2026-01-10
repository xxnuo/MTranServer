import { Menu, Tray } from 'electron';

let tray = null;

export function createTray({
  icon,
  tooltip,
  messages,
  statusLabel,
  versionLabel,
  newVersionLabel,
  autoStartEnabled,
  onOpenBrowserUi,
  onOpenBrowserDocs,
  onOpenRepo,
  onOpenSettings,
  onRestart,
  onOpenModels,
  onOpenConfig,
  onToggleAutoStart,
  onOpenReleasePage,
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
    newVersionLabel,
    autoStartEnabled,
    onOpenBrowserUi,
    onOpenBrowserDocs,
    onOpenRepo,
    onOpenSettings,
    onRestart,
    onOpenModels,
    onOpenConfig,
    onToggleAutoStart,
    onOpenReleasePage,
    onQuit
  });
  return tray;
}

export function updateTrayMenu({
  messages,
  statusLabel,
  versionLabel,
  newVersionLabel,
  autoStartEnabled,
  onOpenBrowserUi,
  onOpenBrowserDocs,
  onOpenRepo,
  onOpenSettings,
  onRestart,
  onOpenModels,
  onOpenConfig,
  onToggleAutoStart,
  onOpenReleasePage,
  onQuit
}) {
  if (!tray) return;
  const menuItems = [
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
    {
      label: messages.trayAutoStart,
      type: 'checkbox',
      checked: autoStartEnabled,
      click: onToggleAutoStart
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
    }
  ];
  if (newVersionLabel) {
    menuItems.push({
      label: messages.trayNewVersion.replace('{version}', newVersionLabel),
      click: onOpenReleasePage
    });
  }
  menuItems.push({
    label: messages.trayQuit,
    click: onQuit
  });
  const contextMenu = Menu.buildFromTemplate(menuItems);
  tray.setContextMenu(contextMenu);
}
