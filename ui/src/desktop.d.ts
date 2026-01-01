interface DesktopApi {
  isDesktop: boolean
  getConfig: () => Promise<import('./lib/desktop').DesktopConfigResponse>
  applyConfig: (config: import('./lib/desktop').DesktopConfig) => Promise<import('./lib/desktop').DesktopConfigResponse>
  resetConfig: () => Promise<import('./lib/desktop').DesktopConfigResponse>
  restartServer: () => Promise<import('./lib/desktop').DesktopConfigResponse>
}

interface Window {
  mtranDesktop?: DesktopApi
}
