import { useState, useEffect, useCallback, useMemo } from 'react'
import { useTranslation } from 'react-i18next'
import { Button } from '@/components/ui/button'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { toast } from 'sonner'
import { History, BookOpen, Maximize2, Minimize2, Plus } from 'lucide-react'
import { SettingsMenu } from '@/components/SettingsMenu'
import { HistorySheet } from '@/components/HistorySheet'
import { TranslationPanel } from '@/components/TranslationPanel'
import { useHistory } from '@/hooks/use-history'
import { DesktopSettingsPage } from '@/components/DesktopSettingsPage'

function getRoute(pathname: string) {
  const normalized = pathname.replace(/\/+$/, '')
  if (normalized.endsWith('/settings')) return 'settings'
  return 'main'
}

function MainPage() {
  const { t } = useTranslation()
  const [languages, setLanguages] = useState<string[]>([])
  const [loadingLanguages, setLoadingLanguages] = useState(true)
  const [widescreen, setWidescreen] = useState(() => localStorage.getItem('widescreen') === 'true')
  const [showTokenDialog, setShowTokenDialog] = useState(false)
  const [showHistory, setShowHistory] = useState(false)
  const [backendVersion, setBackendVersion] = useState('')

  const [panels, setPanels] = useState<string[]>(() => {
    const saved = localStorage.getItem('translationPanels')
    if (saved) {
      try {
        const parsed = JSON.parse(saved)
        if (Array.isArray(parsed) && parsed.length > 0) {
          return parsed
        }
      } catch (e) {
        console.error('Failed to parse saved panels', e)
      }
    }
    return ['panel-' + Date.now()]
  })

  const { history, addToHistory, clearHistory, deleteItem } = useHistory()

  const fetchVersion = async () => {
    try {
      const response = await fetch('/version')
      if (response.ok) {
        const data = await response.json()
        if (data.version) {
          setBackendVersion(data.version)
        }
      }
    } catch (error) {
      console.error('Failed to fetch version:', error)
    }
  }

  const fetchLanguages = useCallback(async () => {
    try {
      const headers: HeadersInit = {}
      const apiToken = localStorage.getItem('apiToken')
      if (apiToken) {
        headers['Authorization'] = `Bearer ${apiToken}`
      }

      const response = await fetch('/languages', { headers })
      if (!response.ok) {
        if (response.status === 401) {
          setShowTokenDialog(true)
          toast.error(t('apiTokenPlaceholder'))
        } else {
          throw new Error('Failed to fetch languages')
        }
      } else {
        const data = await response.json()
        setLanguages(['auto', ...(data.languages || [])])
      }
    } catch (error) {
      console.error('Error fetching languages:', error)
      toast.error(t('failedToLoadLanguages'))
    } finally {
      setLoadingLanguages(false)
    }
  }, [t])

  useEffect(() => {
    localStorage.setItem('widescreen', String(widescreen))
  }, [widescreen])

  useEffect(() => {
    localStorage.setItem('translationPanels', JSON.stringify(panels))
  }, [panels])

  useEffect(() => {
    const urlParams = new URLSearchParams(window.location.search)
    const tokenFromUrl = urlParams.get('token')
    if (tokenFromUrl) {
      localStorage.setItem('apiToken', tokenFromUrl)
    }
    fetchLanguages()
    fetchVersion()
  }, [fetchLanguages])

  const addPanel = () => {
    setPanels(prev => [...prev, 'panel-' + Date.now()])
  }

  const removePanel = (id: string) => {
    if (panels.length <= 1) return
    setPanels(prev => prev.filter(p => p !== id))
  }

  return (
    <div className="min-h-screen bg-background p-3 sm:p-4 md:p-8 flex flex-col">
      <div className={`mx-auto flex-1 w-full transition-all duration-300 ${widescreen ? 'max-w-full' : 'max-w-[90rem]'}`}>
        <div className="flex justify-between items-center mb-4 sm:mb-6">
          <div className="flex items-center gap-4">
            <h1 className="text-xl sm:text-2xl font-bold text-foreground">
              {t('title')}
            </h1>
            <Tooltip>
              <TooltipTrigger asChild>
                <a
                  href="/docs"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-flex items-center justify-center rounded-md text-sm font-medium transition-colors focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring disabled:pointer-events-none disabled:opacity-50 hover:bg-accent hover:text-accent-foreground h-9 w-9"
                  aria-label={t('apiDocs')}
                >
                  <BookOpen className="h-5 w-5 text-muted-foreground" />
                </a>
              </TooltipTrigger>
              <TooltipContent>
                <p>{t('apiDocs')}</p>
              </TooltipContent>
            </Tooltip>
          </div>
          <div className="flex items-center gap-2">
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={addPanel}
                  aria-label={t('addPanel')}
                >
                  <Plus className="h-5 w-5" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>
                <p>{t('addPanel')}</p>
              </TooltipContent>
            </Tooltip>
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={() => setWidescreen(!widescreen)}
                  aria-label={widescreen ? t('standardView') : t('widescreen')}
                >
                  {widescreen ? (
                    <Minimize2 className="h-5 w-5" />
                  ) : (
                    <Maximize2 className="h-5 w-5" />
                  )}
                </Button>
              </TooltipTrigger>
              <TooltipContent>
                <p>{widescreen ? t('standardView') : t('widescreen')}</p>
              </TooltipContent>
            </Tooltip>
            <Tooltip>
              <TooltipTrigger asChild>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={() => setShowHistory(true)}
                  aria-label={t('history')}
                >
                  <History className="h-5 w-5" />
                </Button>
              </TooltipTrigger>
              <TooltipContent>
                <p>{t('history')}</p>
              </TooltipContent>
            </Tooltip>
            <SettingsMenu
              showTokenDialog={showTokenDialog}
              setShowTokenDialog={setShowTokenDialog}
              onTokenSaved={fetchLanguages}
            />
          </div>
        </div>

        <div className="flex flex-wrap gap-4 items-stretch">
          {panels.map((id, index) => (
            <div
              key={id}
              className={
                panels.length === 1
                  ? `w-full ${widescreen ? 'sm:w-[90%]' : 'sm:w-[75%]'} mx-auto`
                  : 'flex-1 min-w-[350px] max-w-full'
              }
            >
              <TranslationPanel
                id={id}
                isPrimary={index === 0}
                languages={languages}
                loadingLanguages={loadingLanguages}
                addToHistory={addToHistory}
                onDelete={() => removePanel(id)}
                canDelete={panels.length > 1}
              />
            </div>
          ))}
        </div>

      </div>

      <footer className="w-full mt-8 py-4 text-center">
        <a
          href="https://github.com/xxnuo/MTranServer"
          target="_blank"
          rel="noopener noreferrer"
          className="inline-flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground transition-colors"
        >
          <svg className="h-5 w-5" fill="currentColor" viewBox="0 0 24 24" aria-hidden="true">
            <path fillRule="evenodd" d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.531 1.032 1.531 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z" clipRule="evenodd" />
          </svg>
          <span>MTranServer {backendVersion && <span className="text-xs ml-1 opacity-75">v{backendVersion}</span>}</span>
        </a>
      </footer>

      <HistorySheet
        open={showHistory}
        onOpenChange={setShowHistory}
        history={history}
        onSelect={(item) => {
          const event = new CustomEvent('loadHistoryItem', { detail: item });
          window.dispatchEvent(event);
          setShowHistory(false);
        }}
        onClear={clearHistory}
        onDelete={deleteItem}
      />
    </div>
  )
}

function App() {
  const route = useMemo(() => getRoute(window.location.pathname), [])
  if (route === 'settings') {
    return <DesktopSettingsPage />
  }
  return <MainPage />
}

export default App
