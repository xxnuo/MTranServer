import { Fragment, useCallback, useEffect, useRef, useState } from 'react'
import type { ReactNode } from 'react'
import { useTranslation } from 'react-i18next'
import {
  AlertCircle,
  ArrowLeft,
  Check,
  CheckCircle2,
  ChevronLeft,
  ChevronDown,
  ChevronRight,
  Download,
  FileText,
  Languages,
  Loader2,
  Package,
  RefreshCw,
  ScanText,
  Search,
  Settings2,
  Trash2,
  XCircle,
} from 'lucide-react'
import { toast } from 'sonner'
import { Badge } from '@/components/ui/badge'
import { Button } from '@/components/ui/button'
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
} from '@/components/ui/alert-dialog'
import { Input } from '@/components/ui/input'
import { Progress } from '@/components/ui/progress'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { RadioGroup, RadioGroupItem } from '@/components/ui/radio-group'
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@/components/ui/select'
import { Table, TableBody, TableCell, TableHead, TableHeader, TableRow } from '@/components/ui/table'
import { Tabs, TabsList, TabsTrigger } from '@/components/ui/tabs'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { getLanguageName } from '@/lib/languages'

type ModelStatus = 'available' | 'installed' | 'downloading' | 'decompressing' | 'failed'
type DownloadStatus = 'queued' | 'checking' | 'downloading' | 'decompressing' | 'completed' | 'failed' | 'cancelled'
type StatusFilter = 'all' | ModelStatus
type ModelCategory = 'translation' | 'ocr'

const TRANSLATION_PAGE_SIZE = 24
const OCR_PAGE_SIZE = 4

interface ModelFileState {
  fileType: string
  filename: string
  compressedSize: number
  decompressedSize: number
  installedSize: number
  installed: boolean
}

interface ManagedModel {
  id: string
  from: string
  to: string
  architecture: string
  version: string
  status: ModelStatus
  downloadSize: number
  installedSize: number
  progress: number
  downloadId?: string
  error?: string
  errorCode?: string
  fallbackUsed?: boolean
  files: ModelFileState[]
}

interface ModelCatalog {
  modelDir: string
  recordsLoadedAt: string | null
  models: ManagedModel[]
  totalModels: number
  filteredModels: number
  page: number
  pageSize: number
  totalPages: number
  architectures: string[]
  statusCounts: Record<ModelStatus, number>
}

interface DownloadJob {
  kind?: 'translation' | 'ocr'
  id: string
  from: string
  to: string
  architecture: string
  version: string
  source: ModelDownloadSource
  status: DownloadStatus
  progress: number
  downloadedBytes: number
  totalBytes: number
  currentFile?: string
  error?: string
  errorCode?: string
  fallbackUsed?: boolean
  downloadRateMbps?: number
  createdAt: string
  startedAt?: string
  finishedAt?: string
  modelId?: string
}

type ModelDownloadSource = 'mirror' | 'official'
type MirrorPreset = 'japan' | 'ningbo' | 'custom'

interface ModelDownloadSettings {
  source: ModelDownloadSource
  mirrorUrl: string
  proxyUrl: string
  fallbackToOfficial: boolean
}

interface MirrorOption {
  id: MirrorPreset
  labelKey: string
  url: string
  noteKey: string
}

interface DownloadSpeedTestItem {
  source: ModelDownloadSource
  url: string
  status: 'ok' | 'failed'
  latencyMs: number | null
  speedMbps: number | null
  downloadedBytes: number
  durationMs: number | null
  error?: string
}

interface DownloadSpeedTest {
  testedAt: string
  viaProxy: boolean
  mirror: DownloadSpeedTestItem
  official: DownloadSpeedTestItem
}

interface DownloadLatencyTestItem {
  source: ModelDownloadSource
  url: string
  status: 'ok' | 'failed'
  latencyMs: number | null
  error?: string
}

interface DownloadLatencyTest {
  testedAt: string
  viaProxy: boolean
  mirror: DownloadLatencyTestItem
  official: DownloadLatencyTestItem
}

const defaultDownloadSettings: ModelDownloadSettings = {
  source: 'mirror',
  mirrorUrl: 'http://183.136.206.212:8787',
  proxyUrl: '',
  fallbackToOfficial: true,
}

const MIRROR_OPTIONS: MirrorOption[] = [
  {
    id: 'ningbo',
    url: 'http://183.136.206.212:8787',
    labelKey: 'mirrorPresetNingbo',
    noteKey: 'mirrorPresetNingboDesc',
  },
  {
    id: 'japan',
    url: 'http://74.81.55.196:8787',
    labelKey: 'mirrorPresetJapan',
    noteKey: 'mirrorPresetJapanDesc',
  },
  {
    id: 'custom',
    url: '',
    labelKey: 'mirrorPresetCustom',
    noteKey: 'mirrorPresetCustomDesc',
  },
]

function detectMirrorPreset(value: string): MirrorPreset {
  const normalized = value.trim().replace(/\/+$/, '')
  const preset = MIRROR_OPTIONS.find(option => option.url === normalized)
  return preset?.id ?? 'custom'
}

const emptyModels: ManagedModel[] = []

function modelCatalogPath(page: number, query: string, status: StatusFilter, architecture: string, locale: string) {
  const params = new URLSearchParams({ page: String(page), pageSize: String(TRANSLATION_PAGE_SIZE) })
  if (query.trim()) params.set('query', query.trim())
  if (status !== 'all') params.set('status', status)
  if (architecture !== 'all') params.set('architecture', architecture)
  if (locale) params.set('locale', locale)
  return `/models?${params.toString()}`
}

interface OcrModelFile {
  role: string
  path: string
  url: string
  sizeBytes: number
  sha256: string
  available: boolean
}

interface OcrModelSummary {
  id: string
  name: string
  version: string
  variant: string
  backend: string
  device: string
  languages: string[]
  sizeBytes: number
  recommended: boolean
  available: boolean
  description: Record<string, string>
  files: OcrModelFile[]
  status: ModelStatus
  progress: number
  downloadedBytes: number
  downloadId?: string
  error?: string
  errorCode?: string
}

interface OcrModelCatalog {
  schema: number
  updatedAt: string
  models: OcrModelSummary[]
}

function getAuthHeaders() {
  const headers = new Headers({ Accept: 'application/json' })
  const token = localStorage.getItem('apiToken')
  if (token) headers.set('Authorization', `Bearer ${token}`)
  return headers
}

async function requestJson<T>(input: string, init?: RequestInit): Promise<T> {
  const headers = getAuthHeaders()
  if (init?.body) headers.set('Content-Type', 'application/json')

  const response = await fetch(input, { ...init, headers })
  if (!response.ok) {
    let message = `HTTP ${response.status}`
    try {
      const body = await response.json() as { message?: string; error?: string }
      message = body.message || body.error || message
    } catch {
      // 代理失败时可能没有 JSON 错误体，保留 HTTP 状态便于定位问题。
    }
    throw new Error(message)
  }
  return response.json() as Promise<T>
}

function formatBytes(value: number, locale: string) {
  if (!Number.isFinite(value) || value <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  const unitIndex = Math.min(Math.floor(Math.log(value) / Math.log(1024)), units.length - 1)
  const amount = value / 1024 ** unitIndex
  return `${new Intl.NumberFormat(locale, { maximumFractionDigits: amount >= 10 ? 0 : 1 }).format(amount)} ${units[unitIndex]}`
}

function formatSpeed(value: number | null, locale: string) {
  if (value === null || !Number.isFinite(value)) return '-'
  return `${new Intl.NumberFormat(locale, { maximumFractionDigits: 1 }).format(value)} Mbps`
}

function isActiveStatus(status: ModelStatus) {
  return status === 'downloading' || status === 'decompressing'
}

function statusLabel(status: ModelStatus, t: (key: string) => string) {
  const labels: Record<ModelStatus, string> = {
    available: t('modelAvailable'),
    installed: t('modelInstalled'),
    downloading: t('modelDownloading'),
    decompressing: t('modelDecompressing'),
    failed: t('modelFailed'),
  }
  return labels[status]
}

function statusVariant(status: ModelStatus): 'default' | 'secondary' | 'destructive' | 'outline' {
  if (status === 'installed') return 'secondary'
  if (status === 'failed') return 'destructive'
  if (status === 'available') return 'outline'
  return 'default'
}

function downloadErrorLabel(code: string | undefined, t: (key: string) => string) {
  if (code === 'network') return t('downloadErrorNetwork')
  if (code === 'hash_mismatch') return t('downloadErrorHash')
  if (code === 'decompression') return t('downloadErrorDecompression')
  if (code === 'cancelled') return t('downloadErrorCancelled')
  return t('downloadErrorUnknown')
}

export function ModelManagerButton() {
  const { t } = useTranslation()
  return (
    <Tooltip>
      <TooltipTrigger asChild>
        <a
          href="/ui/models"
          className="inline-flex h-9 w-9 items-center justify-center rounded-md text-sm font-medium transition-colors hover:bg-accent hover:text-accent-foreground focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring"
          aria-label={t('models')}
        >
          <Package className="h-5 w-5" />
        </a>
      </TooltipTrigger>
      <TooltipContent><p>{t('models')}</p></TooltipContent>
    </Tooltip>
  )
}

function StatusBadge({ status }: { status: ModelStatus }) {
  const { t } = useTranslation()
  const active = isActiveStatus(status)
  return (
    <Badge variant={statusVariant(status)}>
      {active && <Loader2 className="animate-spin" />}
      {status === 'installed' && <CheckCircle2 />}
      {status === 'failed' && <AlertCircle />}
      {statusLabel(status, t)}
    </Badge>
  )
}

interface ModelTableProps {
  models: ManagedModel[]
  loading: boolean
  error: string | null
  emptyLabel: string
  expanded: Set<string>
  onToggle: (id: string) => void
  onDownload: (model: ManagedModel) => void
  onDelete: (model: ManagedModel) => void
}

function ModelTable({ models, loading, error, emptyLabel, expanded, onToggle, onDownload, onDelete }: ModelTableProps) {
  const { t, i18n } = useTranslation()
  const locale = i18n.resolvedLanguage || i18n.language

  if (loading && models.length === 0) {
    return <div className="flex min-h-64 items-center justify-center text-sm text-muted-foreground"><Loader2 className="mr-2 size-4 animate-spin" />{t('loadingModels')}</div>
  }
  if (error && models.length === 0) {
    return <div className="flex min-h-64 flex-col items-center justify-center gap-2 text-center text-sm text-destructive"><AlertCircle className="size-5" /><span>{error}</span></div>
  }
  if (models.length === 0) {
    return <div className="flex min-h-64 items-center justify-center text-sm text-muted-foreground">{emptyLabel}</div>
  }

  return (
    <div className="overflow-hidden rounded-lg border">
      <Table>
        <TableHeader>
          <TableRow>
            <TableHead className="w-10" />
            <TableHead>{t('modelPair')}</TableHead>
            <TableHead className="hidden md:table-cell">{t('modelArchitecture')}</TableHead>
            <TableHead className="hidden lg:table-cell">{t('modelVersion')}</TableHead>
            <TableHead>{t('modelDownloadSize')}</TableHead>
            <TableHead>{t('modelStatus')}</TableHead>
            <TableHead className="text-right">{t('modelAction')}</TableHead>
          </TableRow>
        </TableHeader>
        <TableBody>
          {models.map(model => {
            const open = expanded.has(model.id)
            const active = isActiveStatus(model.status)
            const languagePair = `${getLanguageName(model.from, locale)} → ${getLanguageName(model.to, locale)}`
            return (
              <Fragment key={model.id}>
                <TableRow className={open ? 'bg-muted/30' : undefined}>
                  <TableCell className="w-10 pr-0">
                    <Tooltip>
                      <TooltipTrigger asChild>
                        <Button variant="ghost" size="icon" className="size-7" onClick={() => onToggle(model.id)} aria-label={t('modelDetails')}>
                          {open ? <ChevronDown /> : <ChevronRight />}
                        </Button>
                      </TooltipTrigger>
                      <TooltipContent><p>{t('modelDetails')}</p></TooltipContent>
                    </Tooltip>
                  </TableCell>
                  <TableCell>
                    <div className="min-w-44">
                      <div className="font-medium">{languagePair}</div>
                      <div className="text-xs text-muted-foreground">{model.from} → {model.to}</div>
                    </div>
                  </TableCell>
                  <TableCell className="hidden md:table-cell">{model.architecture}</TableCell>
                  <TableCell className="hidden lg:table-cell">{model.version}</TableCell>
                  <TableCell>{formatBytes(model.downloadSize, locale)}</TableCell>
                  <TableCell>
                    <div className="flex min-w-28 flex-col items-start gap-1">
                      <StatusBadge status={model.status} />
                      {(active || model.status === 'failed') && (
                        <div className="flex w-24 items-center gap-2">
                          <Progress value={model.progress} className="h-1.5" />
                          <span className="text-[11px] tabular-nums text-muted-foreground">{model.progress}%</span>
                        </div>
                      )}
                    </div>
                  </TableCell>
                  <TableCell className="text-right">
                    <div className="flex justify-end gap-1">
                      {model.status !== 'installed' && !active && (
                        <Button size="sm" onClick={() => onDownload(model)}>
                          <Download />
                          <span className="hidden sm:inline">{model.status === 'failed' ? t('modelRetry') : t('modelDownload')}</span>
                        </Button>
                      )}
                      {model.status === 'installed' && (
                        <Tooltip>
                          <TooltipTrigger asChild>
                            <Button variant="ghost" size="icon" className="text-muted-foreground hover:text-destructive" onClick={() => onDelete(model)} aria-label={t('modelDelete')}>
                              <Trash2 />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent><p>{t('modelDelete')}</p></TooltipContent>
                        </Tooltip>
                      )}
                    </div>
                  </TableCell>
                </TableRow>
                {open && (
                  <TableRow className="bg-muted/20 hover:bg-muted/20">
                    <TableCell colSpan={7} className="whitespace-normal p-0">
                      <div className="grid gap-4 px-5 py-4 md:grid-cols-[minmax(0,1fr)_minmax(0,1.6fr)]">
                        <div className="grid grid-cols-2 gap-3 text-sm">
                          <div><p className="text-xs text-muted-foreground">{t('modelVersion')}</p><p className="mt-1 font-medium">{model.version}</p></div>
                          <div><p className="text-xs text-muted-foreground">{t('modelArchitecture')}</p><p className="mt-1 font-medium">{model.architecture}</p></div>
                          <div><p className="text-xs text-muted-foreground">{t('modelDownloadSize')}</p><p className="mt-1 font-medium">{formatBytes(model.downloadSize, locale)}</p></div>
                          <div><p className="text-xs text-muted-foreground">{t('modelInstalledSize')}</p><p className="mt-1 font-medium">{formatBytes(model.installedSize, locale)}</p></div>
                          {model.errorCode && <p className="col-span-2 break-words text-xs text-destructive">{t('modelDownloadError')}: {downloadErrorLabel(model.errorCode, t)}</p>}
                          {model.error && <p className="col-span-2 break-words text-xs text-destructive">{model.error}</p>}
                          {model.fallbackUsed && <p className="col-span-2 break-words text-xs text-muted-foreground">{t('modelFallbackUsed')}</p>}
                        </div>
                        <div>
                          <p className="mb-2 text-xs font-medium text-muted-foreground">{t('modelFiles')} ({model.files.length})</p>
                          <div className="grid gap-2 sm:grid-cols-2">
                            {model.files.map(file => (
                              <div key={`${model.id}-${file.fileType}-${file.filename}`} className="min-w-0 rounded-md border bg-background p-2">
                                <div className="flex items-center gap-1.5">
                                  {file.installed ? <Check className="size-3.5 shrink-0 text-emerald-600" /> : <FileText className="size-3.5 shrink-0 text-muted-foreground" />}
                                  <span className="truncate text-xs font-medium" title={file.filename}>{file.filename}</span>
                                </div>
                                <p className="mt-1 text-[11px] text-muted-foreground">{file.fileType} · {formatBytes(file.compressedSize, locale)}</p>
                              </div>
                            ))}
                          </div>
                        </div>
                      </div>
                    </TableCell>
                  </TableRow>
                )}
              </Fragment>
            )
          })}
        </TableBody>
      </Table>
    </div>
  )
}

interface OcrModelCatalogProps {
  catalog: OcrModelCatalog | null
  loading: boolean
  error: string | null
  page: number
  onPageChange: (page: number) => void
  onDownload: (model: OcrModelSummary) => void
}

function OcrModelCatalog({ catalog, loading, error, page, onPageChange, onDownload }: OcrModelCatalogProps) {
  const { t, i18n } = useTranslation()
  const locale = i18n.resolvedLanguage || i18n.language
  const models = catalog?.models ?? []
  const totalPages = Math.max(1, Math.ceil(models.length / OCR_PAGE_SIZE))
  const currentPage = Math.min(page, totalPages)
  const visibleModels = models.slice((currentPage - 1) * OCR_PAGE_SIZE, currentPage * OCR_PAGE_SIZE)

  if (loading && models.length === 0) {
    return <div className="flex min-h-64 items-center justify-center text-sm text-muted-foreground"><Loader2 className="mr-2 size-4 animate-spin" />{t('loadingModels')}</div>
  }
  if (error && models.length === 0) {
    return <div className="flex min-h-64 flex-col items-center justify-center gap-2 text-center text-sm text-destructive"><AlertCircle className="size-5" /><span>{error}</span></div>
  }
  if (models.length === 0) {
    return <div className="flex min-h-64 items-center justify-center text-sm text-muted-foreground">{t('noOcrModels')}</div>
  }

  return (
    <main className="space-y-3">
      <div className="flex flex-wrap items-baseline justify-between gap-2">
        <div>
          <h2 className="text-base font-semibold">{t('ocrModelList')}</h2>
          <p className="text-sm text-muted-foreground">
            {t('ocrModelListDesc')}
            {catalog?.updatedAt && <span className="ml-2">{t('ocrModelCatalogUpdated')}: {new Date(catalog.updatedAt).toLocaleString(locale)}</span>}
          </p>
        </div>
      </div>
      <div className="grid gap-3 lg:grid-cols-2">
        {visibleModels.map(model => {
          const language = locale.split('-')[0]
          const description = model.description[language] || model.description.en || ''
          const active = isActiveStatus(model.status)
          return (
            <article key={model.id} className="rounded-lg border p-4">
              <div className="flex items-start justify-between gap-3">
                <div className="flex min-w-0 items-start gap-3">
                  <div className="flex size-9 shrink-0 items-center justify-center rounded-md bg-muted text-primary">
                    <ScanText className="size-5" />
                  </div>
                  <div className="min-w-0">
                    <h3 className="truncate font-medium">{model.name}</h3>
                    <p className="truncate text-xs text-muted-foreground">{model.variant} · {model.version}</p>
                  </div>
                </div>
                <Badge variant={model.recommended ? 'default' : 'outline'}>{t(model.recommended ? 'ocrModelRecommended' : 'ocrModelLightweight')}</Badge>
              </div>
              {description && <p className="mt-3 text-sm text-muted-foreground">{description}</p>}
              <dl className="mt-4 grid grid-cols-2 gap-3 text-sm">
                <div>
                  <dt className="text-xs text-muted-foreground">{t('ocrModelBackend')}</dt>
                  <dd className="mt-1 font-medium">{model.backend} · {model.device}</dd>
                </div>
                <div>
                  <dt className="text-xs text-muted-foreground">{t('ocrModelSize')}</dt>
                  <dd className="mt-1 font-medium">{formatBytes(model.sizeBytes, locale)}</dd>
                </div>
                <div className="col-span-2">
                  <dt className="text-xs text-muted-foreground">{t('ocrModelLanguages')}</dt>
                  <dd className="mt-1 font-medium">{model.languages.join(' / ')}</dd>
                </div>
              </dl>
              {active && (
                <div className="mt-4 space-y-1.5">
                  <div className="flex items-center justify-between gap-2 text-xs text-muted-foreground">
                    <span className="truncate">{model.downloadedBytes > 0 ? formatBytes(model.downloadedBytes, locale) : t('modelDownloadProgress')}</span>
                    <span className="shrink-0 tabular-nums">{model.progress}%</span>
                  </div>
                  <Progress value={model.progress} />
                </div>
              )}
              <div className="mt-4 flex flex-wrap items-center justify-between gap-2 border-t pt-3">
                <div className="flex min-w-0 items-center gap-2 text-xs text-muted-foreground">
                  <StatusBadge status={model.status} />
                  <span className="truncate">{model.available ? t('ocrModelMirrorReady') : t('ocrModelUnavailable')}</span>
                </div>
                {model.status !== 'installed' && (
                  <Button size="sm" onClick={() => onDownload(model)} disabled={!model.available || active}>
                    {active ? <Loader2 className="animate-spin" /> : <Download />}
                    <span>{model.status === 'failed' ? t('modelRetry') : t('modelDownload')}</span>
                  </Button>
                )}
              </div>
              {model.error && <p className="mt-2 break-words text-xs text-destructive">{model.errorCode ? `${downloadErrorLabel(model.errorCode, t)}: ` : ''}{model.error}</p>}
            </article>
          )
        })}
      </div>
      <PaginationControls page={currentPage} totalPages={totalPages} onPageChange={onPageChange} />
      {error && <div className="flex items-center gap-2 rounded-md border border-destructive/30 bg-destructive/5 px-3 py-2 text-sm text-destructive"><AlertCircle className="size-4 shrink-0" />{error}</div>}
    </main>
  )
}

function ActiveDownloads({ jobs, cancellingJobId, onCancel }: { jobs: DownloadJob[]; cancellingJobId: string | null; onCancel: (job: DownloadJob) => void }) {
  const { t, i18n } = useTranslation()
  const locale = i18n.resolvedLanguage || i18n.language
  const activeJobs = jobs.filter(job => ['queued', 'checking', 'downloading', 'decompressing'].includes(job.status))
  if (activeJobs.length === 0) return null

  return (
    <section className="rounded-lg border border-primary/20 bg-primary/5 p-4">
      <div className="mb-3 flex items-center gap-2 text-sm font-medium"><Loader2 className="size-4 animate-spin text-primary" />{t('modelActiveDownloads')} ({activeJobs.length})</div>
      <div className="space-y-3">
        {activeJobs.map(job => (
          <div key={job.id} className="space-y-1.5 border-t pt-3 first:border-t-0 first:pt-0">
            <div className="flex items-center justify-between gap-3 text-xs">
              <span className="truncate font-medium">{job.from} → {job.to} · {job.architecture}</span>
              <div className="flex shrink-0 items-center gap-2">
                <span className="tabular-nums text-muted-foreground">{job.progress}%</span>
                <Tooltip>
                  <TooltipTrigger asChild>
                    <Button variant="ghost" size="icon" className="size-7 text-muted-foreground hover:text-destructive" onClick={() => onCancel(job)} disabled={cancellingJobId === job.id} aria-label={t('modelCancelDownload')}>
                      {cancellingJobId === job.id ? <Loader2 className="animate-spin" /> : <XCircle />}
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent><p>{t('modelCancelDownload')}</p></TooltipContent>
                </Tooltip>
              </div>
            </div>
            <div className="flex flex-wrap items-center gap-1.5 text-[11px] text-muted-foreground">
              <Badge variant="outline">{job.source === 'mirror' ? t('mirrorSource') : t('officialSource')}</Badge>
              {job.fallbackUsed && <Badge variant="secondary">{t('modelFallbackUsed')}</Badge>}
            </div>
            <Progress value={job.progress} />
            <p className="truncate text-[11px] text-muted-foreground">{job.currentFile || t('modelDownloadProgress')} · {formatBytes(job.downloadedBytes, locale)} / {formatBytes(job.totalBytes, locale)} · {formatSpeed(job.downloadRateMbps ?? null, locale)}</p>
          </div>
        ))}
      </div>
    </section>
  )
}

function PaginationControls({ page, totalPages, onPageChange }: { page: number; totalPages: number; onPageChange: (page: number) => void }) {
  const { t } = useTranslation()
  if (totalPages <= 1) return null

  return (
    <nav className="flex items-center justify-center gap-3" aria-label={t('modelPagination')}>
      <Tooltip>
        <TooltipTrigger asChild>
          <Button variant="outline" size="icon" onClick={() => onPageChange(page - 1)} disabled={page <= 1} aria-label={t('modelPreviousPage')}>
            <ChevronLeft />
          </Button>
        </TooltipTrigger>
        <TooltipContent><p>{t('modelPreviousPage')}</p></TooltipContent>
      </Tooltip>
      <span className="min-w-20 text-center text-sm tabular-nums text-muted-foreground">{t('modelPage', { current: page, total: totalPages })}</span>
      <Tooltip>
        <TooltipTrigger asChild>
          <Button variant="outline" size="icon" onClick={() => onPageChange(page + 1)} disabled={page >= totalPages} aria-label={t('modelNextPage')}>
            <ChevronRight />
          </Button>
        </TooltipTrigger>
        <TooltipContent><p>{t('modelNextPage')}</p></TooltipContent>
      </Tooltip>
    </nav>
  )
}

export function ModelManagerPage() {
  const { t, i18n } = useTranslation()
  const [category, setCategory] = useState<ModelCategory>(() => localStorage.getItem('modelCategory') === 'ocr' ? 'ocr' : 'translation')
  const [catalog, setCatalog] = useState<ModelCatalog | null>(null)
  const [ocrCatalog, setOcrCatalog] = useState<OcrModelCatalog | null>(null)
  const [jobs, setJobs] = useState<DownloadJob[]>([])
  const [loading, setLoading] = useState(true)
  const [ocrLoading, setOcrLoading] = useState(false)
  const [refreshing, setRefreshing] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [ocrError, setOcrError] = useState<string | null>(null)
  const [query, setQuery] = useState('')
  const [statusFilter, setStatusFilter] = useState<StatusFilter>('all')
  const [architectureFilter, setArchitectureFilter] = useState('all')
  const [page, setPage] = useState(1)
  const [ocrPage, setOcrPage] = useState(1)
  const [expanded, setExpanded] = useState<Set<string>>(new Set())
  const [deleteTarget, setDeleteTarget] = useState<ManagedModel | null>(null)
  const [deleting, setDeleting] = useState(false)
  const [cancellingJobId, setCancellingJobId] = useState<string | null>(null)
  const [downloadSettingsOpen, setDownloadSettingsOpen] = useState(false)
  const [downloadSettings, setDownloadSettings] = useState<ModelDownloadSettings>(defaultDownloadSettings)
  const [savedDownloadSettings, setSavedDownloadSettings] = useState<ModelDownloadSettings>(defaultDownloadSettings)
  const [mirrorPreset, setMirrorPreset] = useState<MirrorPreset>('japan')
  const [savingDownloadSettings, setSavingDownloadSettings] = useState(false)
  const [speedTest, setSpeedTest] = useState<DownloadSpeedTest | null>(null)
  const [speedTesting, setSpeedTesting] = useState(false)
  const [latencyTest, setLatencyTest] = useState<DownloadLatencyTest | null>(null)
  const [latencyTesting, setLatencyTesting] = useState(false)
  const translationLoadedRef = useRef(false)
  const loadRequestRef = useRef(0)
  const locale = i18n.resolvedLanguage || i18n.language

  useEffect(() => {
    localStorage.setItem('modelCategory', category)
  }, [category])

  const loadData = useCallback(async ({ page: requestedPage, query: requestedQuery, status: requestedStatus, architecture: requestedArchitecture, locale: requestedLocale, silent = false }: {
    page: number
    query: string
    status: StatusFilter
    architecture: string
    locale: string
    silent?: boolean
  }) => {
    const requestId = ++loadRequestRef.current
    if (!silent) setLoading(true)
    try {
      const [nextCatalog, nextJobs] = await Promise.all([
        requestJson<ModelCatalog>(modelCatalogPath(requestedPage, requestedQuery, requestedStatus, requestedArchitecture, requestedLocale)),
        requestJson<DownloadJob[]>('/models/downloads?active=true'),
      ])
      if (requestId !== loadRequestRef.current) return
      setCatalog(nextCatalog)
      setJobs(nextJobs)
      setError(null)
    } catch (loadError) {
      if (requestId !== loadRequestRef.current) return
      const message = loadError instanceof Error ? loadError.message : String(loadError)
      setError(message)
      if (!silent) toast.error(`${t('modelLoadFailed')}: ${message}`)
    } finally {
      if (requestId !== loadRequestRef.current) return
      if (!silent) setLoading(false)
    }
  }, [t])

  const loadOcrCatalog = useCallback(async (silent = false) => {
    if (!silent) setOcrLoading(true)
    try {
      const nextCatalog = await requestJson<OcrModelCatalog>('/models/ocr')
      setOcrCatalog(nextCatalog)
      setOcrError(null)
    } catch (loadError) {
      const message = loadError instanceof Error ? loadError.message : String(loadError)
      setOcrError(message)
      throw loadError
    } finally {
      if (!silent) setOcrLoading(false)
    }
  }, [])

  useEffect(() => {
    if (category !== 'translation') return
    const initialLoad = !translationLoadedRef.current
    translationLoadedRef.current = true
    const request = { page, query, status: statusFilter, architecture: architectureFilter, locale }
    void loadData({ ...request, silent: !initialLoad })
    const timer = window.setInterval(() => void loadData({ ...request, silent: true }), 2000)
    return () => window.clearInterval(timer)
  }, [architectureFilter, category, loadData, locale, page, query, statusFilter])

  useEffect(() => {
    if (category !== 'ocr') return
    void loadOcrCatalog().catch(() => undefined)
    const timer = window.setInterval(() => void loadOcrCatalog(true).catch(() => undefined), 2000)
    return () => window.clearInterval(timer)
  }, [category, loadOcrCatalog])

  useEffect(() => {
    void requestJson<ModelDownloadSettings>('/models/settings')
      .then(settings => {
        setDownloadSettings(settings)
        setSavedDownloadSettings(settings)
        setMirrorPreset(detectMirrorPreset(settings.mirrorUrl))
      })
      .catch(loadError => {
        const message = loadError instanceof Error ? loadError.message : String(loadError)
        toast.error(`${t('downloadSettingsLoadFailed')}: ${message}`)
      })
  }, [t])

  const models = catalog?.models ?? emptyModels
  const statusCounts = catalog?.statusCounts
  const installedCount = statusCounts?.installed ?? 0
  const availableCount = statusCounts?.available ?? 0
  const activeCount = (statusCounts?.downloading ?? 0) + (statusCounts?.decompressing ?? 0)
  const failedCount = statusCounts?.failed ?? 0
  const totalModelCount = catalog?.totalModels ?? models.length
  const filteredModelCount = catalog?.filteredModels ?? models.length
  const architectures = catalog?.architectures ?? []
  const totalPages = catalog?.totalPages ?? 1
  const currentPage = Math.min(page, totalPages)
  const visibleModels = models

  useEffect(() => {
    setPage(1)
  }, [query, statusFilter, architectureFilter])

  useEffect(() => {
    if (page > totalPages) setPage(totalPages)
  }, [page, totalPages])

  const resetFilters = () => {
    setQuery('')
    setStatusFilter('all')
    setArchitectureFilter('all')
    setPage(1)
  }

  const handleDownload = async (model: ManagedModel) => {
    try {
      const job = await requestJson<DownloadJob>('/models/download', {
        method: 'POST',
        body: JSON.stringify({ from: model.from, to: model.to, version: model.version, architecture: model.architecture }),
      })
      setJobs(previous => [job, ...previous.filter(item => item.id !== job.id)])
      toast.success(`${model.from} → ${model.to} ${t('modelDownloadStarted')}`)
      await loadData({ page, query, status: statusFilter, architecture: architectureFilter, locale, silent: true })
    } catch (downloadError) {
      const message = downloadError instanceof Error ? downloadError.message : String(downloadError)
      const displayMessage = /concurrency|同時実行|并发/i.test(message) ? t('downloadConcurrencyLimit') : message
      toast.error(`${t('modelDownloadFailed')}: ${displayMessage}`)
    }
  }

  const handleOcrDownload = async (model: OcrModelSummary) => {
    try {
      const job = await requestJson<DownloadJob>('/models/ocr/download', {
        method: 'POST',
        body: JSON.stringify({ modelId: model.id }),
      })
      setJobs(previous => [job, ...previous.filter(item => item.id !== job.id)])
      toast.success(`${model.name} ${t('modelDownloadStarted')}`)
      await loadOcrCatalog(true)
    } catch (downloadError) {
      const message = downloadError instanceof Error ? downloadError.message : String(downloadError)
      const displayMessage = /concurrency|同時実行|并发/i.test(message) ? t('downloadConcurrencyLimit') : message
      toast.error(`${t('modelDownloadFailed')}: ${displayMessage}`)
    }
  }

  const handleRefresh = async () => {
    setRefreshing(true)
    try {
      if (category === 'ocr') {
        await loadOcrCatalog()
        toast.success(t('ocrModelsRefreshed'))
        return
      }
      await requestJson<ModelCatalog>('/models/refresh', { method: 'POST' })
      await loadData({ page: 1, query, status: statusFilter, architecture: architectureFilter, locale, silent: true })
      setPage(1)
      toast.success(t('modelRecordsRefreshed'))
    } catch (refreshError) {
      const message = refreshError instanceof Error ? refreshError.message : String(refreshError)
      toast.error(`${t('modelRefreshFailed')}: ${message}`)
    } finally {
      setRefreshing(false)
    }
  }

  const runDownloadSpeedTest = useCallback(async (settings?: ModelDownloadSettings) => {
    setSpeedTesting(true)
    try {
      setSpeedTest(await requestJson<DownloadSpeedTest>('/models/speedtest', {
        method: 'POST',
        body: JSON.stringify(settings ? {
          mirrorUrl: settings.mirrorUrl.trim(),
          proxyUrl: settings.proxyUrl.trim(),
        } : {}),
      }))
    } catch (speedTestError) {
      const message = speedTestError instanceof Error ? speedTestError.message : String(speedTestError)
      toast.error(`${t('downloadSpeedTestFailed')}: ${message}`)
    } finally {
      setSpeedTesting(false)
    }
  }, [t])

  const runDownloadLatencyTest = useCallback(async (settings?: ModelDownloadSettings) => {
    setLatencyTesting(true)
    try {
      setLatencyTest(await requestJson<DownloadLatencyTest>('/models/latency', {
        method: 'POST',
        body: JSON.stringify(settings ? {
          mirrorUrl: settings.mirrorUrl.trim(),
          proxyUrl: settings.proxyUrl.trim(),
        } : {}),
      }))
    } catch (latencyError) {
      const message = latencyError instanceof Error ? latencyError.message : String(latencyError)
      toast.error(`${t('downloadLatencyTestFailed')}: ${message}`)
    } finally {
      setLatencyTesting(false)
    }
  }, [t])

  const openDownloadSettings = () => {
    setDownloadSettings(savedDownloadSettings)
    setMirrorPreset(detectMirrorPreset(savedDownloadSettings.mirrorUrl))
    setLatencyTest(null)
    setSpeedTest(null)
    setDownloadSettingsOpen(true)
    void runDownloadLatencyTest(savedDownloadSettings)
    void runDownloadSpeedTest(savedDownloadSettings)
  }

  const handleSaveDownloadSettings = async () => {
    setSavingDownloadSettings(true)
    try {
      const nextSettings = await requestJson<ModelDownloadSettings>('/models/settings', {
        method: 'POST',
        body: JSON.stringify({
          source: downloadSettings.source,
          mirrorUrl: downloadSettings.mirrorUrl.trim(),
          proxyUrl: downloadSettings.proxyUrl.trim(),
        }),
      })
      setDownloadSettings(nextSettings)
      setSavedDownloadSettings(nextSettings)
      setMirrorPreset(detectMirrorPreset(nextSettings.mirrorUrl))
      setDownloadSettingsOpen(false)
      toast.success(t('downloadSettingsSaved'))
      void runDownloadLatencyTest(nextSettings)
      void runDownloadSpeedTest(nextSettings)
    } catch (saveError) {
      const message = saveError instanceof Error ? saveError.message : String(saveError)
      toast.error(`${t('downloadSettingsSaveFailed')}: ${message}`)
    } finally {
      setSavingDownloadSettings(false)
    }
  }

  const handleDelete = async () => {
    if (!deleteTarget) return
    setDeleting(true)
    try {
      await requestJson(`/models/${encodeURIComponent(deleteTarget.from)}/${encodeURIComponent(deleteTarget.to)}`, { method: 'DELETE' })
      setDeleteTarget(null)
      await loadData({ page, query, status: statusFilter, architecture: architectureFilter, locale, silent: true })
      toast.success(t('modelDeleted'))
    } catch (deleteError) {
      const message = deleteError instanceof Error ? deleteError.message : String(deleteError)
      toast.error(`${t('modelDeleteFailed')}: ${message}`)
    } finally {
      setDeleting(false)
    }
  }

  const handleCancel = async (job: DownloadJob) => {
    setCancellingJobId(job.id)
    try {
      await requestJson(`/models/downloads/${encodeURIComponent(job.id)}`, { method: 'DELETE' })
      await loadData({ page, query, status: statusFilter, architecture: architectureFilter, locale, silent: true })
      toast.success(t('modelDownloadCancelled'))
    } catch (cancelError) {
      const message = cancelError instanceof Error ? cancelError.message : String(cancelError)
      toast.error(`${t('modelDownloadCancelFailed')}: ${message}`)
    } finally {
      setCancellingJobId(null)
    }
  }

  const recordsUpdatedAt = catalog?.recordsLoadedAt
    ? new Date(catalog.recordsLoadedAt).toLocaleString(locale, { dateStyle: 'short', timeStyle: 'short' })
    : t('modelUnknown')

  return (
    <div className="min-h-screen bg-background p-4 sm:p-6 md:p-8">
      <div className="mx-auto max-w-[95rem] space-y-6">
        <div className="sticky top-0 z-30 -mx-4 bg-background/95 px-4 backdrop-blur supports-[backdrop-filter]:bg-background/80 sm:-mx-6 sm:px-6 md:-mx-8 md:px-8">
          <header className="flex flex-wrap items-center justify-between gap-4 border-b py-4">
            <div className="flex min-w-0 items-center gap-3">
              <Tooltip>
                <TooltipTrigger asChild>
                  <a href="/ui/" className="inline-flex size-9 shrink-0 items-center justify-center rounded-md hover:bg-accent" aria-label={t('backToMain')}><ArrowLeft className="size-5" /></a>
                </TooltipTrigger>
                <TooltipContent><p>{t('backToMain')}</p></TooltipContent>
              </Tooltip>
              <div className="min-w-0">
                <h1 className="text-xl font-semibold sm:text-2xl">{t('modelManagement')}</h1>
                <p className="truncate text-sm text-muted-foreground">{t('modelManagementDesc')}</p>
              </div>
            </div>
            <div className="flex items-center gap-2">
              <Button variant="outline" size="sm" onClick={openDownloadSettings}>
                <Settings2 />
                {t('downloadSettings')}
              </Button>
              <Button variant="outline" size="sm" onClick={() => void handleRefresh()} disabled={refreshing || (category === 'translation' ? loading : ocrLoading)}>
                <RefreshCw className={refreshing ? 'animate-spin' : ''} />
                {t('refreshModels')}
              </Button>
            </div>
          </header>

          <Tabs
            value={category}
            onValueChange={value => {
              const nextCategory: ModelCategory = value === 'ocr' ? 'ocr' : 'translation'
              setCategory(nextCategory)
              resetFilters()
              setOcrPage(1)
              setExpanded(new Set())
            }}
            className="border-b py-3"
          >
            <TabsList>
              <TabsTrigger value="translation"><Languages />{t('translationModels')}</TabsTrigger>
              <TabsTrigger value="ocr"><ScanText />{t('ocrModels')}</TabsTrigger>
            </TabsList>
          </Tabs>

          {category === 'translation' && (
            <div className="flex flex-col gap-3 border-b py-3 lg:flex-row lg:items-center">
              <div className="relative min-w-0 flex-1 lg:max-w-xl">
                <Search className="pointer-events-none absolute left-2.5 top-2.5 size-4 text-muted-foreground" />
                <Input value={query} onChange={event => setQuery(event.target.value)} placeholder={t('modelSearchPlaceholder')} className="pl-8" />
              </div>
              <Select value={statusFilter} onValueChange={value => setStatusFilter(value as StatusFilter)}>
                <SelectTrigger className="w-full lg:w-44"><SelectValue /></SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">{t('allModels')} ({totalModelCount})</SelectItem>
                  <SelectItem value="installed">{t('modelInstalled')} ({installedCount})</SelectItem>
                  <SelectItem value="available">{t('modelAvailable')} ({availableCount})</SelectItem>
                  <SelectItem value="downloading">{t('modelDownloading')} ({models.filter(model => model.status === 'downloading').length})</SelectItem>
                  <SelectItem value="decompressing">{t('modelDecompressing')} ({models.filter(model => model.status === 'decompressing').length})</SelectItem>
                  <SelectItem value="failed">{t('modelFailed')} ({failedCount})</SelectItem>
                </SelectContent>
              </Select>
              <Select value={architectureFilter} onValueChange={setArchitectureFilter}>
                <SelectTrigger className="w-full lg:w-44"><SelectValue /></SelectTrigger>
                <SelectContent>
                  <SelectItem value="all">{t('modelArchitecture')} ({architectures.length})</SelectItem>
                  {architectures.map(architecture => <SelectItem key={architecture} value={architecture}>{architecture}</SelectItem>)}
                </SelectContent>
              </Select>
              {(query || statusFilter !== 'all' || architectureFilter !== 'all') && <Button variant="ghost" size="sm" onClick={resetFilters}>{t('clear')}</Button>}
            </div>
          )}
        </div>

        <div key={category} className="animate-in fade-in-0 slide-in-from-bottom-2 duration-200 motion-reduce:animate-none">
          {category === 'ocr' ? (
            <OcrModelCatalog catalog={ocrCatalog} loading={ocrLoading} error={ocrError} page={ocrPage} onPageChange={setOcrPage} onDownload={model => void handleOcrDownload(model)} />
          ) : (
            <>
              <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
                <Metric label={t('modelTotal')} value={totalModelCount} />
                <Metric label={t('modelInstalled')} value={installedCount} icon={<CheckCircle2 className="text-emerald-600" />} />
                <Metric label={t('modelAvailable')} value={availableCount} icon={<Download className="text-muted-foreground" />} />
                <Metric label={t('modelActiveDownloads')} value={activeCount} icon={<Loader2 className={activeCount ? 'animate-spin text-primary' : 'text-muted-foreground'} />} />
              </div>

              <ActiveDownloads jobs={jobs} cancellingJobId={cancellingJobId} onCancel={job => { if (!cancellingJobId) void handleCancel(job) }} />

              <main className="space-y-3">
                <div className="flex flex-wrap items-baseline justify-between gap-2">
                  <div>
                    <h2 className="text-base font-semibold">{t('modelList')}</h2>
                    <p className="text-sm text-muted-foreground">{t('modelFilteredCount', { count: filteredModelCount })}</p>
                  </div>
                  <p className="text-xs text-muted-foreground">{t('modelRecordsUpdated')}: {recordsUpdatedAt}</p>
                </div>
                {error && models.length > 0 && <div className="flex items-center gap-2 rounded-md border border-destructive/30 bg-destructive/5 px-3 py-2 text-sm text-destructive"><AlertCircle className="size-4 shrink-0" />{error}</div>}
                <ModelTable models={visibleModels} loading={loading} error={error} emptyLabel={query || statusFilter !== 'all' || architectureFilter !== 'all' ? t('noResults') : t('noAvailableModels')} expanded={expanded} onToggle={id => setExpanded(previous => { const next = new Set(previous); if (next.has(id)) next.delete(id); else next.add(id); return next })} onDownload={model => void handleDownload(model)} onDelete={model => setDeleteTarget(model)} />
                <PaginationControls page={currentPage} totalPages={totalPages} onPageChange={setPage} />
              </main>

              <p className="break-all text-xs text-muted-foreground">{t('modelDirectory')}: {catalog?.modelDir || t('modelUnknown')}</p>
            </>
          )}
        </div>
      </div>

      <AlertDialog open={Boolean(deleteTarget)} onOpenChange={nextOpen => { if (!nextOpen && !deleting) setDeleteTarget(null) }}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>{t('modelDeleteConfirmTitle')}</AlertDialogTitle>
            <AlertDialogDescription>{t('modelDeleteConfirmDesc')} {deleteTarget && `${deleteTarget.from} → ${deleteTarget.to}`}</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel disabled={deleting}>{t('cancel')}</AlertDialogCancel>
            <AlertDialogAction disabled={deleting} onClick={event => { event.preventDefault(); void handleDelete() }}>
              {deleting && <Loader2 className="animate-spin" />}{t('modelDeleteConfirmAction')}
            </AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>

      <Dialog open={downloadSettingsOpen} onOpenChange={open => { if (!savingDownloadSettings) setDownloadSettingsOpen(open) }}>
        <DialogContent className="max-h-[calc(100vh-2rem)] min-w-0 overflow-x-hidden overflow-y-auto sm:max-w-4xl">
          <DialogHeader>
            <DialogTitle>{t('downloadSettings')}</DialogTitle>
            <DialogDescription>{t('downloadSettingsDesc')}</DialogDescription>
          </DialogHeader>

          <div className="grid min-w-0 gap-4 lg:grid-cols-2">
            <section className="min-w-0 space-y-3 rounded-md border bg-muted/20 p-3">
              <div className="flex items-center justify-between gap-3">
                <div className="flex min-w-0 items-center gap-2 text-sm font-medium">
                  {latencyTesting && <Loader2 className="size-4 shrink-0 animate-spin text-primary" />}
                  <span className="truncate">{t('downloadLatencyTest')}</span>
                </div>
                {latencyTest && <span className="shrink-0 text-xs text-muted-foreground">{new Date(latencyTest.testedAt).toLocaleTimeString()}</span>}
              </div>
              {latencyTest && <p className="text-xs text-muted-foreground">{latencyTest.viaProxy ? t('downloadSpeedViaProxy') : t('downloadSpeedDirect')}</p>}
              {latencyTesting && !latencyTest && <p className="text-xs text-muted-foreground">{t('downloadLatencyTesting')}</p>}
              {latencyTest && (
                <div className="grid gap-2 sm:grid-cols-2">
                  {[latencyTest.mirror, latencyTest.official].map(result => {
                    const sourceLabel = result.source === 'mirror' ? t('mirrorSource') : t('officialSource')
                    const latencyClass = result.latencyMs !== null && result.latencyMs >= 200 ? 'text-destructive' : 'text-foreground'
                    return (
                      <div key={result.source} className="min-w-0 rounded-md border bg-background p-3">
                        <div className="truncate text-sm font-medium">{sourceLabel}</div>
                        <div className="mt-2 text-xs">
                          <div className="text-muted-foreground">{t('downloadLatency')}</div>
                          <div className={`mt-1 font-semibold tabular-nums ${latencyClass}`}>
                            {result.latencyMs === null ? '-' : `${result.latencyMs} ms`}
                          </div>
                        </div>
                        {result.status === 'failed' && <p className="mt-2 break-words text-xs text-destructive">{result.error || t('downloadSpeedUnavailable')}</p>}
                      </div>
                    )
                  })}
                </div>
              )}
            </section>

            <section className="min-w-0 space-y-3 rounded-md border bg-muted/20 p-3">
              <div className="flex items-center justify-between gap-3">
                <div className="flex min-w-0 items-center gap-2 text-sm font-medium">
                  {speedTesting && <Loader2 className="size-4 shrink-0 animate-spin text-primary" />}
                  <span className="truncate">{t('downloadSpeedTest')}</span>
                </div>
                {speedTest && <span className="shrink-0 text-xs text-muted-foreground">{new Date(speedTest.testedAt).toLocaleTimeString()}</span>}
              </div>
              {speedTest && <p className="text-xs text-muted-foreground">{speedTest.viaProxy ? t('downloadSpeedViaProxy') : t('downloadSpeedDirect')}</p>}
              {speedTesting && !speedTest && <p className="text-xs text-muted-foreground">{t('downloadSpeedTesting')}</p>}
              {speedTest && (
                <div className="grid gap-2 sm:grid-cols-2">
                  {[speedTest.mirror, speedTest.official].map(result => {
                    const sourceLabel = result.source === 'mirror' ? t('mirrorSource') : t('officialSource')
                    return (
                      <div key={result.source} className="min-w-0 rounded-md border bg-background p-3">
                        <div className="truncate text-sm font-medium">{sourceLabel}</div>
                        <div className="mt-2 text-xs">
                          <div className="text-muted-foreground">{t('downloadSpeed')}</div>
                          <div className="mt-1 font-semibold tabular-nums">{formatSpeed(result.speedMbps, locale)}</div>
                        </div>
                        {result.status === 'failed' && <p className="mt-2 break-words text-xs text-destructive">{result.error || t('downloadSpeedUnavailable')}</p>}
                      </div>
                    )
                  })}
                </div>
              )}
            </section>

            <section className="min-w-0 space-y-3 lg:col-span-2">
              <RadioGroup
                value={downloadSettings.source}
                onValueChange={value => setDownloadSettings(previous => ({ ...previous, source: value as ModelDownloadSource }))}
                className="grid gap-3 sm:grid-cols-2"
              >
                <label htmlFor="model-download-mirror" className="flex min-w-0 cursor-pointer items-start gap-3 rounded-md border p-3 transition-colors hover:bg-accent/50">
                  <RadioGroupItem id="model-download-mirror" value="mirror" className="mt-1 shrink-0" />
                  <span className="min-w-0 flex-1">
                    <span className="block font-medium">{t('mirrorSource')}</span>
                    <span className="mt-1 block text-sm text-muted-foreground">{t('mirrorSourceDesc')}</span>
                    <span className="mt-1 block text-xs text-muted-foreground">{t('mirrorFallbackDesc')}</span>
                    <span className="mt-2 flex flex-wrap gap-1.5">
                      <Badge variant="secondary">{t('mirrorTagResume')}</Badge>
                      <Badge variant="secondary">{t('mirrorTagUsBandwidth')}</Badge>
                    </span>
                  </span>
                </label>
                <label htmlFor="model-download-official" className="flex min-w-0 cursor-pointer items-start gap-3 rounded-md border p-3 transition-colors hover:bg-accent/50">
                  <RadioGroupItem id="model-download-official" value="official" className="mt-1 shrink-0" />
                  <span className="min-w-0 flex-1">
                    <span className="block font-medium">{t('officialSource')}</span>
                    <span className="mt-1 block text-sm text-muted-foreground">{t('officialSourceDesc')}</span>
                    <span className="mt-1 block text-xs text-muted-foreground">{t('officialNoFallbackDesc')}</span>
                    <span className="mt-2 flex flex-wrap gap-1.5">
                      <Badge variant="secondary">{t('officialTagLive')}</Badge>
                      <Badge variant="secondary">{t('officialTagStable')}</Badge>
                    </span>
                  </span>
                </label>
              </RadioGroup>
            </section>

            <div className="grid min-w-0 gap-3 sm:grid-cols-2 lg:col-span-2">
              <div className="min-w-0 space-y-2">
                <label htmlFor="model-mirror-preset" className="text-sm font-medium">{t('mirrorPreset')}</label>
                <Select
                  value={mirrorPreset}
                  onValueChange={(value) => {
                    const nextPreset = value as MirrorPreset
                    setMirrorPreset(nextPreset)
                    const option = MIRROR_OPTIONS.find(item => item.id === nextPreset)
                    if (option && option.url) {
                      setDownloadSettings(previous => ({ ...previous, mirrorUrl: option.url }))
                    }
                  }}
                  disabled={downloadSettings.source !== 'mirror'}
                >
                  <SelectTrigger id="model-mirror-preset" className="w-full">
                    <SelectValue placeholder={t('mirrorPreset')} />
                  </SelectTrigger>
                  <SelectContent>
                    {MIRROR_OPTIONS.map(option => (
                      <SelectItem key={option.id} value={option.id}>
                        {t(option.labelKey)}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
                <p className="break-words text-xs text-muted-foreground">
                  {t(MIRROR_OPTIONS.find(option => option.id === mirrorPreset)?.noteKey || 'mirrorPresetCustomDesc')}
                </p>
              </div>

              <div className="min-w-0 space-y-2">
                <label htmlFor="model-mirror-url" className="text-sm font-medium">{t('mirrorUrl')}</label>
                <Input
                  id="model-mirror-url"
                  value={downloadSettings.mirrorUrl}
                  onChange={event => {
                    const nextUrl = event.target.value
                    setDownloadSettings(previous => ({ ...previous, mirrorUrl: nextUrl }))
                    setMirrorPreset(detectMirrorPreset(nextUrl))
                  }}
                  disabled={downloadSettings.source !== 'mirror'}
                  spellCheck={false}
                />
                <p className="break-words text-xs text-muted-foreground">{t('mirrorUrlDesc')}</p>
              </div>

              <div className="min-w-0 space-y-2 sm:col-span-2">
                <label htmlFor="model-download-proxy" className="text-sm font-medium">{t('downloadProxy')}</label>
                <Input
                  id="model-download-proxy"
                  value={downloadSettings.proxyUrl}
                  onChange={event => setDownloadSettings(previous => ({ ...previous, proxyUrl: event.target.value }))}
                  placeholder={t('proxyPlaceholder')}
                  spellCheck={false}
                />
                <p className="break-words text-xs text-muted-foreground">{t('downloadProxyDesc')}</p>
              </div>
            </div>
          </div>

          <DialogFooter className="mt-0">
            <Button variant="outline" onClick={() => setDownloadSettingsOpen(false)} disabled={savingDownloadSettings}>{t('cancel')}</Button>
            <Button onClick={() => void handleSaveDownloadSettings()} disabled={savingDownloadSettings}>
              {savingDownloadSettings && <Loader2 className="animate-spin" />}
              {t('saveDownloadSettings')}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}

function Metric({ label, value, icon }: { label: string; value: number; icon?: ReactNode }) {
  return <div className="rounded-lg border bg-card p-4"><div className="flex items-center justify-between gap-2 text-sm text-muted-foreground"><span>{label}</span>{icon}</div><p className="mt-2 text-2xl font-semibold tabular-nums">{value}</p></div>
}
