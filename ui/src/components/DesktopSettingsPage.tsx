import { useEffect, useMemo, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Switch } from '@/components/ui/switch'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Separator } from '@/components/ui/separator'
import { Alert, AlertDescription, AlertTitle } from '@/components/ui/alert'
import { toast } from 'sonner'
import {
  applyDesktopConfig,
  getDesktopConfig,
  isDesktop,
  resetDesktopConfig,
  restartDesktopServer
} from '@/lib/desktop'
import type { DesktopConfig } from '@/lib/desktop'

type FormState = {
  locale: string
  server: {
    host: string
    port: string
    logLevel: string
    enableWebUI: boolean
    enableOfflineMode: boolean
    workerIdleTimeout: string
    workersPerLanguage: string
    apiToken: string
    logDir: string
    logToFile: boolean
    logConsole: boolean
    logRequests: boolean
    maxLengthBreak: string
    checkUpdate: boolean
    cacheSize: string
    modelDir: string
    configDir: string
  }
}

function toForm(config: DesktopConfig): FormState {
  return {
    locale: config.locale,
    server: {
      host: config.server.host,
      port: String(config.server.port),
      logLevel: config.server.logLevel,
      enableWebUI: config.server.enableWebUI,
      enableOfflineMode: config.server.enableOfflineMode,
      workerIdleTimeout: String(config.server.workerIdleTimeout),
      workersPerLanguage: String(config.server.workersPerLanguage),
      apiToken: config.server.apiToken,
      logDir: config.server.logDir,
      logToFile: config.server.logToFile,
      logConsole: config.server.logConsole,
      logRequests: config.server.logRequests,
      maxLengthBreak: String(config.server.maxLengthBreak),
      checkUpdate: config.server.checkUpdate,
      cacheSize: String(config.server.cacheSize),
      modelDir: config.server.modelDir,
      configDir: config.server.configDir
    }
  }
}

function toNumber(value: string, fallback: number) {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

export function DesktopSettingsPage() {
  const { t } = useTranslation()
  const desktopAvailable = isDesktop()
  const [config, setConfig] = useState<DesktopConfig | null>(null)
  const [form, setForm] = useState<FormState | null>(null)
  const [status, setStatus] = useState('')
  const [version, setVersion] = useState('')
  const [loading, setLoading] = useState(true)
  const [saving, setSaving] = useState(false)

  useEffect(() => {
    if (!desktopAvailable) {
      setLoading(false)
      return
    }
    getDesktopConfig()
      .then((response) => {
        if (!response) return
        setConfig(response.config)
        setForm(toForm(response.config))
        setStatus(response.status)
        setVersion(response.version)
      })
      .catch(() => {
        toast.error(t('loadingConfigFailed'))
      })
      .finally(() => setLoading(false))
  }, [desktopAvailable, t])

  const serverStatusLabel = useMemo(() => {
    if (!status) return ''
    return status === 'running' ? t('serverStatusRunning') : t('serverStatusStopped')
  }, [status, t])

  const handleApply = async () => {
    if (!form || !config) return
    setSaving(true)
    const nextConfig: DesktopConfig = {
      locale: form.locale,
      server: {
        ...config.server,
        host: form.server.host,
        port: toNumber(form.server.port, config.server.port),
        logLevel: form.server.logLevel,
        enableWebUI: form.server.enableWebUI,
        enableOfflineMode: form.server.enableOfflineMode,
        workerIdleTimeout: toNumber(form.server.workerIdleTimeout, config.server.workerIdleTimeout),
        workersPerLanguage: toNumber(form.server.workersPerLanguage, config.server.workersPerLanguage),
        apiToken: form.server.apiToken,
        logDir: form.server.logDir,
        logToFile: form.server.logToFile,
        logConsole: form.server.logConsole,
        logRequests: form.server.logRequests,
        maxLengthBreak: toNumber(form.server.maxLengthBreak, config.server.maxLengthBreak),
        checkUpdate: form.server.checkUpdate,
        cacheSize: toNumber(form.server.cacheSize, config.server.cacheSize),
        modelDir: form.server.modelDir,
        configDir: form.server.configDir
      }
    }
    try {
      const response = await applyDesktopConfig(nextConfig)
      if (response) {
        setConfig(response.config)
        setForm(toForm(response.config))
        setStatus(response.status)
        setVersion(response.version)
        toast.success(t('settingsApplied'))
      }
    } catch {
      toast.error(t('settingsApplyFailed'))
    } finally {
      setSaving(false)
    }
  }

  const handleRestart = async () => {
    if (!desktopAvailable) return
    setSaving(true)
    try {
      const response = await restartDesktopServer()
      if (response) {
        setConfig(response.config)
        setForm(toForm(response.config))
        setStatus(response.status)
        setVersion(response.version)
        toast.success(t('serverRestarted'))
      }
    } catch {
      toast.error(t('serverRestartFailed'))
    } finally {
      setSaving(false)
    }
  }

  const handleReset = async () => {
    if (!desktopAvailable) return
    setSaving(true)
    try {
      const response = await resetDesktopConfig()
      if (response) {
        setConfig(response.config)
        setForm(toForm(response.config))
        setStatus(response.status)
        setVersion(response.version)
        toast.success(t('settingsReset'))
      }
    } catch {
      toast.error(t('settingsResetFailed'))
    } finally {
      setSaving(false)
    }
  }

  if (!desktopAvailable) {
    return (
      <div className="min-h-screen bg-background p-6">
        <div className="mx-auto max-w-3xl">
          <Alert>
            <AlertTitle>{t('desktopOnly')}</AlertTitle>
            <AlertDescription>{t('desktopOnlyDesc')}</AlertDescription>
          </Alert>
          <div className="mt-6">
            <Button onClick={() => (window.location.href = '/ui/')}>{t('backToMain')}</Button>
          </div>
        </div>
      </div>
    )
  }

  if (loading || !form || !config) {
    return (
      <div className="min-h-screen bg-background p-6">
        <div className="mx-auto max-w-3xl text-muted-foreground">{t('loadingConfig')}</div>
      </div>
    )
  }

  return (
    <div className="min-h-screen bg-background p-3 sm:p-4 md:p-6">
      <div className="mx-auto max-w-4xl space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-4">
          <div>
            <h1 className="text-xl font-semibold text-foreground">{t('desktopSettings')}</h1>
            <p className="text-xs text-muted-foreground">{t('desktopSettingsDesc')}</p>
          </div>
          <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
            <span>{t('serverStatus')}: {serverStatusLabel}</span>
            {version && <span>{t('version')}: v{version}</span>}
          </div>
        </div>

        <Card className="gap-4 py-4">
          <CardHeader className="px-4 pb-3">
            <CardTitle>{t('generalSettings')}</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 px-4">
            <div className="grid gap-3 sm:grid-cols-2">
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('host')}</label>
                <Input value={form.server.host} onChange={(e) => setForm({ ...form, server: { ...form.server, host: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('port')}</label>
                <Input value={form.server.port} onChange={(e) => setForm({ ...form, server: { ...form.server, port: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('logLevel')}</label>
                <Select value={form.server.logLevel} onValueChange={(value) => setForm({ ...form, server: { ...form.server, logLevel: value } })}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="debug">debug</SelectItem>
                    <SelectItem value="info">info</SelectItem>
                    <SelectItem value="warn">warn</SelectItem>
                    <SelectItem value="error">error</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('locale')}</label>
                <Select value={form.locale} onValueChange={(value) => setForm({ ...form, locale: value })}>
                  <SelectTrigger>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="system">{t('system')}</SelectItem>
                    <SelectItem value="en">English</SelectItem>
                    <SelectItem value="zh">中文</SelectItem>
                    <SelectItem value="ja">日本語</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>
            <Separator />
            <div className="grid gap-3 sm:grid-cols-2">
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('enableWebUI')}</div>
                  <div className="text-xs text-muted-foreground">{t('enableWebUIDesc')}</div>
                </div>
                <Switch checked={form.server.enableWebUI} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, enableWebUI: checked } })} />
              </div>
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('enableOfflineMode')}</div>
                  <div className="text-xs text-muted-foreground">{t('enableOfflineModeDesc')}</div>
                </div>
                <Switch checked={form.server.enableOfflineMode} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, enableOfflineMode: checked } })} />
              </div>
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('checkUpdate')}</div>
                  <div className="text-xs text-muted-foreground">{t('checkUpdateDesc')}</div>
                </div>
                <Switch checked={form.server.checkUpdate} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, checkUpdate: checked } })} />
              </div>
            </div>
          </CardContent>
        </Card>

        <Card className="gap-4 py-4">
          <CardHeader className="px-4 pb-3">
            <CardTitle>{t('storageSettings')}</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 px-4">
            <div className="grid gap-3 sm:grid-cols-2">
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('modelDir')}</label>
                <Input value={form.server.modelDir} onChange={(e) => setForm({ ...form, server: { ...form.server, modelDir: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('configDir')}</label>
                <Input value={form.server.configDir} onChange={(e) => setForm({ ...form, server: { ...form.server, configDir: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('logDir')}</label>
                <Input value={form.server.logDir} onChange={(e) => setForm({ ...form, server: { ...form.server, logDir: e.target.value } })} />
              </div>
            </div>
          </CardContent>
        </Card>

        <Card className="gap-4 py-4">
          <CardHeader className="px-4 pb-3">
            <CardTitle>{t('securitySettings')}</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 px-4">
            <div className="space-y-2">
              <label className="text-sm font-medium">{t('apiToken')}</label>
              <Input value={form.server.apiToken} onChange={(e) => setForm({ ...form, server: { ...form.server, apiToken: e.target.value } })} />
            </div>
          </CardContent>
        </Card>

        <Card className="gap-4 py-4">
          <CardHeader className="px-4 pb-3">
            <CardTitle>{t('performanceSettings')}</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 px-4">
            <div className="grid gap-3 sm:grid-cols-2">
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('workerIdleTimeout')}</label>
                <Input value={form.server.workerIdleTimeout} onChange={(e) => setForm({ ...form, server: { ...form.server, workerIdleTimeout: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('workersPerLanguage')}</label>
                <Input value={form.server.workersPerLanguage} onChange={(e) => setForm({ ...form, server: { ...form.server, workersPerLanguage: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('maxLengthBreak')}</label>
                <Input value={form.server.maxLengthBreak} onChange={(e) => setForm({ ...form, server: { ...form.server, maxLengthBreak: e.target.value } })} />
              </div>
              <div className="space-y-2">
                <label className="text-sm font-medium">{t('cacheSize')}</label>
                <Input value={form.server.cacheSize} onChange={(e) => setForm({ ...form, server: { ...form.server, cacheSize: e.target.value } })} />
              </div>
            </div>
          </CardContent>
        </Card>

        <Card className="gap-4 py-4">
          <CardHeader className="px-4 pb-3">
            <CardTitle>{t('loggingSettings')}</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3 px-4">
            <div className="grid gap-3 sm:grid-cols-2">
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('logToFile')}</div>
                  <div className="text-xs text-muted-foreground">{t('logToFileDesc')}</div>
                </div>
                <Switch checked={form.server.logToFile} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, logToFile: checked } })} />
              </div>
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('logConsole')}</div>
                  <div className="text-xs text-muted-foreground">{t('logConsoleDesc')}</div>
                </div>
                <Switch checked={form.server.logConsole} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, logConsole: checked } })} />
              </div>
              <div className="flex items-center justify-between gap-3 rounded-lg border p-3">
                <div>
                  <div className="text-sm font-medium">{t('logRequests')}</div>
                  <div className="text-xs text-muted-foreground">{t('logRequestsDesc')}</div>
                </div>
                <Switch checked={form.server.logRequests} onCheckedChange={(checked) => setForm({ ...form, server: { ...form.server, logRequests: checked } })} />
              </div>
            </div>
          </CardContent>
        </Card>

        <div className="flex flex-wrap items-center justify-between gap-3">
          <Button variant="outline" onClick={() => (window.location.href = '/ui/')}>
            {t('backToMain')}
          </Button>
          <div className="flex flex-wrap gap-2">
            <Button variant="outline" onClick={handleReset} disabled={saving}>
              {t('resetDefaults')}
            </Button>
            <Button variant="outline" onClick={handleRestart} disabled={saving}>
              {t('restart')}
            </Button>
            <Button onClick={handleApply} disabled={saving}>
              {saving ? t('saving') : t('apply')}
            </Button>
          </div>
        </div>
      </div>
    </div>
  )
}
