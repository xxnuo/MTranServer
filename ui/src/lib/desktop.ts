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
  maxSentenceLength: number
  fullwidthZhPunctuation: boolean
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

async function fetchSettings(path: string, body?: unknown) {
  const res = await fetch(`/ui/api/settings${path}`, {
    method: body ? 'POST' : 'GET',
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined
  })
  if (!res.ok) {
    throw new Error(`Request failed: ${res.status}`)
  }
  return res.json()
}

export async function getWebConfig() {
  return fetchSettings('')
}

export async function applyWebConfig(config: DesktopConfig) {
  return fetchSettings('/apply', { config })
}

export async function resetWebConfig() {
  return fetchSettings('/reset', {})
}

export async function restartWebServer() {
  return fetchSettings('/restart', {})
}
