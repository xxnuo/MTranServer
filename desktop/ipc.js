import { ipcMain } from 'electron';

export function registerIpcHandlers(handlers) {
  ipcMain.handle('desktop:get-config', async () => handlers.getConfig());
  ipcMain.handle('desktop:apply-config', async (_event, config) => handlers.applyConfig(config));
  ipcMain.handle('desktop:reset-config', async () => handlers.resetConfig());
  ipcMain.handle('desktop:restart-server', async () => handlers.restartServer());
  ipcMain.handle('desktop:get-status', async () => handlers.getStatus());
  ipcMain.handle('desktop:open-external', async (_event, url) => handlers.openExternal(url));
  ipcMain.handle('desktop:open-path', async (_event, targetPath) => handlers.openPath(targetPath));
}
