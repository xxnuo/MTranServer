const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('mtranDesktop', {
  isDesktop: true,
  getConfig: () => ipcRenderer.invoke('desktop:get-config'),
  applyConfig: (config) => ipcRenderer.invoke('desktop:apply-config', config),
  resetConfig: () => ipcRenderer.invoke('desktop:reset-config'),
  restartServer: () => ipcRenderer.invoke('desktop:restart-server'),
  getStatus: () => ipcRenderer.invoke('desktop:get-status'),
  openExternal: (url) => ipcRenderer.invoke('desktop:open-external', url),
  openPath: (targetPath) => ipcRenderer.invoke('desktop:open-path', targetPath)
});
