import { useState, useEffect, useRef, useCallback, useMemo } from 'react'
import { useTranslation } from 'react-i18next'
import { Card, CardContent } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Textarea } from '@/components/ui/textarea'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Label } from '@/components/ui/label'
import { Spinner } from '@/components/ui/spinner'
import { Switch } from '@/components/ui/switch'
import { toast } from 'sonner'
import { ArrowRightLeft, Copy, Volume2, X, Upload, History } from 'lucide-react'
import { SettingsMenu } from '@/components/SettingsMenu'
import { HistorySheet } from '@/components/HistorySheet'
import { useIsMobile } from '@/hooks/use-mobile'
import { getSortedLanguages } from '@/lib/languages'
import { useHistory } from '@/hooks/use-history'

interface TranslateRequest {
  from: string
  to: string
  text: string
  html: boolean
}

interface TranslateResponse {
  result: string
}

function App() {
  const { t, i18n } = useTranslation()
  const isMobile = useIsMobile()
  const [languages, setLanguages] = useState<string[]>([])
  const [sourceLang, setSourceLang] = useState('auto')
  const [targetLang, setTargetLang] = useState('zh-Hans')
  const [sourceText, setSourceText] = useState('')
  const [translatedText, setTranslatedText] = useState('')
  const [loading, setLoading] = useState(false)
  const [loadingLanguages, setLoadingLanguages] = useState(true)
  const [autoTranslate, setAutoTranslate] = useState(false)
  const [showTokenDialog, setShowTokenDialog] = useState(false)
  const [showHistory, setShowHistory] = useState(false)
  const translateTimeoutRef = useRef<number | null>(null)
  
  const { history, addToHistory, clearHistory, deleteItem } = useHistory()

  const sortedLanguages = useMemo(() => {
    return getSortedLanguages(languages, i18n.language)
  }, [languages, i18n.language])

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
    fetchLanguages()
  }, [fetchLanguages])

  const handleTranslate = useCallback(async (text?: string, showToast = true) => {
    const textToTranslate = text ?? sourceText
    if (!textToTranslate.trim()) {
      if (showToast) {
        toast.error(t('enterTextError'))
      }
      return
    }

    setLoading(true)
    setTranslatedText('')

    try {
      const request: TranslateRequest = {
        from: sourceLang,
        to: targetLang,
        text: textToTranslate,
        html: false
      }

      const headers: HeadersInit = {
        'Content-Type': 'application/json'
      }

      const apiToken = localStorage.getItem('apiToken')
      if (apiToken) {
        headers['Authorization'] = `Bearer ${apiToken}`
      }

      const response = await fetch('/translate', {
        method: 'POST',
        headers,
        body: JSON.stringify(request)
      })

      if (!response.ok) {
        if (response.status === 401) {
          throw new Error(t('apiTokenPlaceholder'))
        }
        const error = await response.json()
        throw new Error(error.error || t('translationFailed'))
      }

      const data: TranslateResponse = await response.json()
      setTranslatedText(data.result)
      
      addToHistory({
        from: sourceLang,
        to: targetLang,
        sourceText: textToTranslate,
        translatedText: data.result
      })

      if (showToast) {
        toast.success(t('translationCompleted'))
      }
    } catch (error) {
      console.error('Translation error:', error)
      if (showToast) {
        toast.error(error instanceof Error ? error.message : t('translationFailed'))
      }
    } finally {
      setLoading(false)
    }
  }, [sourceLang, targetLang, sourceText, t])

  const handleSourceTextChange = (text: string) => {
    setSourceText(text)

    if (autoTranslate && text.trim()) {
      if (translateTimeoutRef.current) {
        clearTimeout(translateTimeoutRef.current)
      }

      translateTimeoutRef.current = window.setTimeout(() => {
        handleTranslate(text, false)
      }, 800)
    }
  }

  useEffect(() => {
    return () => {
      if (translateTimeoutRef.current) {
        window.clearTimeout(translateTimeoutRef.current)
      }
    }
  }, [])

  const handleSwapLanguages = () => {
    setSourceLang(targetLang)
    setTargetLang(sourceLang)
    setSourceText(translatedText)
    setTranslatedText(sourceText)
  }

  const handleCopy = async (text: string) => {
    if (!text) return
    try {
      await navigator.clipboard.writeText(text)
      toast.success(t('copied'))
    } catch (err) {
      toast.error(t('copyFailed'))
    }
  }

  const handleSpeak = (text: string, lang: string) => {
    if (!text) return
    const utterance = new SpeechSynthesisUtterance(text)
    utterance.lang = lang === 'auto' ? 'en-US' : lang
    window.speechSynthesis.speak(utterance)
  }

  const handleClear = () => {
    setSourceText('')
    setTranslatedText('')
  }

  const handleFileUpload = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0]
    if (!file) return

    const reader = new FileReader()
    reader.onload = (e) => {
      const content = e.target?.result
      if (typeof content === 'string') {
        setSourceText(content)
      }
    }
    reader.readAsText(file)
  }

  return (
    <div className="min-h-screen bg-background p-3 sm:p-4 md:p-8 flex flex-col">
      <div className="max-w-6xl mx-auto flex-1 w-full">
        <div className="flex justify-between items-center mb-4 sm:mb-6">
          <h1 className="text-xl sm:text-2xl font-bold text-foreground">
            {t('title')}
          </h1>
          <div className="flex items-center gap-2">
            <Button
              variant="ghost"
              size="icon"
              onClick={() => setShowHistory(true)}
              title={t('history')}
            >
              <History className="h-5 w-5" />
            </Button>
            <SettingsMenu 
              showTokenDialog={showTokenDialog}
              setShowTokenDialog={setShowTokenDialog}
              onTokenSaved={fetchLanguages}
            />
          </div>
        </div>

        <Card className="shadow-lg">
          <CardContent className="pt-4 sm:pt-6 space-y-3 sm:space-y-4">
            <div className="flex flex-col sm:flex-row items-stretch sm:items-center gap-3 sm:gap-2 justify-between">
              <div className="flex items-center gap-2 flex-1">
                <Select
                  value={sourceLang}
                  onValueChange={setSourceLang}
                  disabled={loadingLanguages}
                >
                  <SelectTrigger className={isMobile ? "flex-1" : "w-[140px]"}>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="auto">{t('autoDetect')}</SelectItem>
                    {sortedLanguages.map((lang) => (
                      <SelectItem key={lang.code} value={lang.code}>
                        {lang.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>

                <Button
                  variant="ghost"
                  size="icon"
                  onClick={handleSwapLanguages}
                  disabled={loadingLanguages || sourceLang === 'auto'}
                  className="h-9 w-9 flex-shrink-0"
                >
                  <ArrowRightLeft className="h-4 w-4" />
                </Button>

                <Select
                  value={targetLang}
                  onValueChange={setTargetLang}
                  disabled={loadingLanguages}
                >
                  <SelectTrigger className={isMobile ? "flex-1" : "w-[140px]"}>
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {sortedLanguages.map((lang) => (
                      <SelectItem key={lang.code} value={lang.code}>
                        {lang.name}
                      </SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center gap-2 justify-end sm:justify-start">
                <Switch
                  id="auto-translate"
                  checked={autoTranslate}
                  onCheckedChange={setAutoTranslate}
                />
                <Label htmlFor="auto-translate" className="text-xs cursor-pointer whitespace-nowrap">
                  {t('autoTranslate')}
                </Label>
              </div>
            </div>

            <div className="grid grid-cols-1 md:grid-cols-2 gap-3 sm:gap-4">
              <div className="relative group h-full">
                <Textarea
                  id="source-text"
                  placeholder={t('enterText')}
                  value={sourceText}
                  onChange={(e) => handleSourceTextChange(e.target.value)}
                  className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} h-full resize-none text-base pr-10 pb-10`}
                  disabled={loading}
                />
                
                {sourceText && (
                  <Button
                    variant="ghost"
                    size="icon"
                    className="absolute top-2 right-2 h-8 w-8 opacity-0 group-hover:opacity-100 transition-opacity"
                    onClick={handleClear}
                    title={t('clear')}
                  >
                    <X className="h-4 w-4" />
                  </Button>
                )}

                <div className="absolute bottom-2 left-2 text-xs text-muted-foreground pointer-events-none">
                  {sourceText.length}
                </div>

                <div className="absolute bottom-2 right-2 flex gap-1">
                  <input
                    type="file"
                    id="file-upload"
                    className="hidden"
                    accept=".txt,.md,.json,.js,.ts,.go,.py,.java,.c,.cpp,.h,.hpp"
                    onChange={handleFileUpload}
                  />
                  <Label
                    htmlFor="file-upload"
                    className="inline-flex items-center justify-center whitespace-nowrap rounded-md text-sm font-medium ring-offset-background transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50 hover:bg-accent hover:text-accent-foreground h-8 w-8 cursor-pointer"
                    title="Upload file"
                  >
                    <Upload className="h-4 w-4" />
                  </Label>
                  
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleSpeak(sourceText, sourceLang)}
                    disabled={!sourceText}
                    title="Listen"
                  >
                    <Volume2 className="h-4 w-4" />
                  </Button>
                  
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleCopy(sourceText)}
                    disabled={!sourceText}
                    title="Copy"
                  >
                    <Copy className="h-4 w-4" />
                  </Button>
                </div>
              </div>

              <div className="relative h-full">
                <Textarea
                  id="translated-text"
                  placeholder={t('translationWillAppear')}
                  value={translatedText}
                  readOnly
                  className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} h-full resize-none text-base bg-muted pb-10`}
                />
                
                <div className="absolute bottom-2 right-2 flex gap-1">
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleSpeak(translatedText, targetLang)}
                    disabled={!translatedText}
                    title="Listen"
                  >
                    <Volume2 className="h-4 w-4" />
                  </Button>
                  
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleCopy(translatedText)}
                    disabled={!translatedText}
                    title="Copy"
                  >
                    <Copy className="h-4 w-4" />
                  </Button>
                </div>
              </div>
            </div>

            {!autoTranslate && (
              <div className="flex justify-center">
                <Button
                  onClick={() => handleTranslate()}
                  disabled={loading || loadingLanguages || !sourceText.trim()}
                  className={isMobile ? "w-full" : "min-w-[200px]"}
                  size="lg"
                >
                  {loading ? (
                    <>
                      <Spinner className="mr-2 h-4 w-4" />
                      {t('translating')}
                    </>
                  ) : (
                    t('translate')
                  )}
                </Button>
              </div>
            )}
          </CardContent>
        </Card>
      </div>

      <footer className="max-w-6xl mx-auto w-full mt-8 py-4 text-center">
        <a 
          href="https://github.com/xxnuo/MTranServer" 
          target="_blank" 
          rel="noopener noreferrer"
          className="inline-flex items-center gap-2 text-sm text-muted-foreground hover:text-foreground transition-colors"
        >
          <svg className="h-5 w-5" fill="currentColor" viewBox="0 0 24 24" aria-hidden="true">
            <path fillRule="evenodd" d="M12 2C6.477 2 2 6.484 2 12.017c0 4.425 2.865 8.18 6.839 9.504.5.092.682-.217.682-.483 0-.237-.008-.868-.013-1.703-2.782.605-3.369-1.343-3.369-1.343-.454-1.158-1.11-1.466-1.11-1.466-.908-.62.069-.608.069-.608 1.003.07 1.531 1.032 1.531 1.032.892 1.53 2.341 1.088 2.91.832.092-.647.35-1.088.636-1.338-2.22-.253-4.555-1.113-4.555-4.951 0-1.093.39-1.988 1.029-2.688-.103-.253-.446-1.272.098-2.65 0 0 .84-.27 2.75 1.026A9.564 9.564 0 0112 6.844c.85.004 1.705.115 2.504.337 1.909-1.296 2.747-1.027 2.747-1.027.546 1.379.202 2.398.1 2.651.64.7 1.028 1.595 1.028 2.688 0 3.848-2.339 4.695-4.566 4.943.359.309.678.92.678 1.855 0 1.338-.012 2.419-.012 2.747 0 .268.18.58.688.482A10.019 10.019 0 0022 12.017C22 6.484 17.522 2 12 2z" clipRule="evenodd" />
          </svg>
          <span>MTranServer</span>
        </a>
      </footer>

      <HistorySheet
        open={showHistory}
        onOpenChange={setShowHistory}
        history={history}
        onSelect={(item) => {
          setSourceLang(item.from)
          setTargetLang(item.to)
          setSourceText(item.sourceText)
          setTranslatedText(item.translatedText)
        }}
        onClear={clearHistory}
        onDelete={deleteItem}
      />
    </div>
  )
}

export default App
