export type DesktopServerConfig = {
  host: string
  port: number
  logLevel: string
  enableWebUI: boolean
  enableOfflineMode: boolean
  workerIdleTimeout: number
  workersPerLanguage: number
  apiToken: string
  logDir: string
  logToFile: boolean
  logConsole: boolean
  logRequests: boolean
  maxLengthBreak: number
  checkUpdate: boolean
  cacheSize: number
  modelDir: string
  configDir: string
}

export type DesktopConfig = {
  locale: string
  server: DesktopServerConfig
}

export type DesktopConfigResponse = {
  config: DesktopConfig
  status: string
  version: string
}

export const isDesktop = () => Boolean(window.mtranDesktop?.isDesktop)

export async function getDesktopConfig() {
  if (!window.mtranDesktop) return null
  return window.mtranDesktop.getConfig()
}

export async function applyDesktopConfig(config: DesktopConfig) {
  if (!window.mtranDesktop) return null
  return window.mtranDesktop.applyConfig(config)
}

export async function resetDesktopConfig() {
  if (!window.mtranDesktop) return null
  return window.mtranDesktop.resetConfig()
}

export async function restartDesktopServer() {
  if (!window.mtranDesktop) return null
  return window.mtranDesktop.restartServer()
}
