import { useState, useEffect, useRef, useMemo, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Card, CardContent } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { Textarea } from '@/components/ui/textarea'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Label } from '@/components/ui/label'
import { Spinner } from '@/components/ui/spinner'
import { Switch } from '@/components/ui/switch'
import { toast } from 'sonner'
import { ArrowRightLeft, Copy, Volume2, X, Upload } from 'lucide-react'
import { useIsMobile } from '@/hooks/use-mobile'
import { getSortedLanguages } from '@/lib/languages'

interface TranslateRequest {
  from: string
  to: string
  text: string
  html: boolean
}

interface TranslateResponse {
  result: string
}

interface TranslationPanelProps {
  id: string
  isPrimary: boolean
  languages: string[]
  loadingLanguages: boolean
  addToHistory: (item: any) => void
  onDelete: () => void
  canDelete: boolean
}

export function TranslationPanel({
  id,
  isPrimary,
  languages,
  loadingLanguages,
  addToHistory,
  onDelete,
  canDelete
}: TranslationPanelProps) {
  const { t, i18n } = useTranslation()
  const isMobile = useIsMobile()

  const storageKey = (key: string) => `panel_${id}_${key}`

  const [sourceLang, setSourceLang] = useState(() => localStorage.getItem(storageKey('sourceLang')) || 'auto')
  const [targetLang, setTargetLang] = useState(() => localStorage.getItem(storageKey('targetLang')) || 'zh-Hans')
  const [sourceText, setSourceText] = useState('')
  const [translatedText, setTranslatedText] = useState('')
  const [loading, setLoading] = useState(false)
  const [autoTranslate, setAutoTranslate] = useState(() => localStorage.getItem(storageKey('autoTranslate')) === 'true')

  const translateTimeoutRef = useRef<number | null>(null)

  const sortedLanguages = useMemo(() => {
    return getSortedLanguages(languages, i18n.language)
  }, [languages, i18n.language])

  useEffect(() => {
    localStorage.setItem(storageKey('sourceLang'), sourceLang)
  }, [sourceLang, id])

  useEffect(() => {
    localStorage.setItem(storageKey('targetLang'), targetLang)
  }, [targetLang, id])

  useEffect(() => {
    localStorage.setItem(storageKey('autoTranslate'), String(autoTranslate))
  }, [autoTranslate, id])

  useEffect(() => {
    if (!isPrimary) return

    const handleLoadHistory = (event: Event) => {
      const customEvent = event as CustomEvent
      const item = customEvent.detail
      if (item) {
        setSourceLang(item.from)
        setTargetLang(item.to)
        setSourceText(item.sourceText)
        setTranslatedText(item.translatedText)
      }
    }

    window.addEventListener('loadHistoryItem', handleLoadHistory)
    return () => {
      window.removeEventListener('loadHistoryItem', handleLoadHistory)
    }
  }, [isPrimary])

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
  }, [sourceLang, targetLang, sourceText, t, addToHistory])

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

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault()
      handleTranslate()
    }
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
    <Card className="shadow-lg h-full flex flex-col">
      <CardContent className="pt-1 sm:pt-1 space-y-3 sm:space-y-4 flex-1 flex flex-col">
        <div className="flex flex-wrap items-center gap-3 justify-between">
          <div className="flex items-center gap-2 flex-1 min-w-[280px]">
            <Select
              value={sourceLang}
              onValueChange={setSourceLang}
              disabled={loadingLanguages}
            >
              <SelectTrigger className="w-[110px] sm:w-[140px] flex-shrink-1">
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
              <SelectTrigger className="w-[110px] sm:w-[140px] flex-shrink-1">
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

          <div className="flex items-center gap-2 justify-end flex-shrink-0 ml-auto sm:ml-0">
            <Switch
              id={`auto-translate-${id}`}
              checked={autoTranslate}
              onCheckedChange={setAutoTranslate}
            />
            <Label htmlFor={`auto-translate-${id}`} className="text-xs cursor-pointer whitespace-nowrap">
              {t('autoTranslate')}
            </Label>
            {canDelete && (
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    onClick={onDelete}
                    className="text-muted-foreground hover:text-destructive h-8 w-8 ml-2 shrink-0"
                    aria-label={t('closePanel')}
                  >
                    <X className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('closePanel')}</p>
                </TooltipContent>
              </Tooltip>
            )}
          </div>
        </div>

        <div className="grid grid-cols-1 md:grid-cols-2 gap-3 sm:gap-4 flex-1">
          <div className="relative group h-full flex flex-col">
            <Textarea
              id={`source-text-${id}`}
              placeholder={t('enterText')}
              value={sourceText}
              onChange={(e) => handleSourceTextChange(e.target.value)}
              onKeyDown={handleKeyDown}
              className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} h-full resize-none text-base pr-10 pb-10 flex-1`}
              disabled={loading}
            />

            {sourceText && (
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="absolute top-2 right-2 h-8 w-8 opacity-0 group-hover:opacity-100 transition-opacity"
                    onClick={handleClear}
                    aria-label={t('clear')}
                  >
                    <X className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('clear')}</p>
                </TooltipContent>
              </Tooltip>
            )}

            <div className="absolute bottom-2 left-2 text-xs text-muted-foreground pointer-events-none">
              {sourceText.length}
            </div>

            <div className="absolute bottom-2 right-2 flex gap-1">
              <input
                type="file"
                id={`file-upload-${id}`}
                className="hidden"
                accept=".txt,.md,.json,.js,.ts,.go,.py,.java,.c,.cpp,.h,.hpp"
                onChange={handleFileUpload}
              />
              <Tooltip>
                <TooltipTrigger asChild>
                  <Label
                    htmlFor={`file-upload-${id}`}
                    className="inline-flex items-center justify-center whitespace-nowrap rounded-md text-sm font-medium ring-offset-background transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2 disabled:pointer-events-none disabled:opacity-50 hover:bg-accent hover:text-accent-foreground h-8 w-8 cursor-pointer"
                    aria-label={t('uploadFile')}
                  >
                    <Upload className="h-4 w-4" />
                  </Label>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('uploadFile')}</p>
                </TooltipContent>
              </Tooltip>

              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleSpeak(sourceText, sourceLang)}
                    disabled={!sourceText}
                    aria-label={t('listen')}
                  >
                    <Volume2 className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('listen')}</p>
                </TooltipContent>
              </Tooltip>

              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleCopy(sourceText)}
                    disabled={!sourceText}
                    aria-label={t('copy')}
                  >
                    <Copy className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('copy')}</p>
                </TooltipContent>
              </Tooltip>
            </div>
          </div>

          <div className="relative h-full flex flex-col">
            <Textarea
              id={`translated-text-${id}`}
              placeholder={t('translationWillAppear')}
              value={translatedText}
              readOnly
              className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} h-full resize-none text-base bg-muted pb-10 flex-1`}
            />

            <div className="absolute bottom-2 right-2 flex gap-1">
              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleSpeak(translatedText, targetLang)}
                    disabled={!translatedText}
                    aria-label={t('listen')}
                  >
                    <Volume2 className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('listen')}</p>
                </TooltipContent>
              </Tooltip>

              <Tooltip>
                <TooltipTrigger asChild>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="h-8 w-8"
                    onClick={() => handleCopy(translatedText)}
                    disabled={!translatedText}
                    aria-label={t('copy')}
                  >
                    <Copy className="h-4 w-4" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent>
                  <p>{t('copy')}</p>
                </TooltipContent>
              </Tooltip>
            </div>
          </div>
        </div>

        {!autoTranslate && (
          <div className="flex justify-center mt-auto pt-2">
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
  )
}