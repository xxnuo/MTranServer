import { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Card, CardContent } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Textarea } from '@/components/ui/textarea'
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from '@/components/ui/select'
import { Label } from '@/components/ui/label'
import { Spinner } from '@/components/ui/spinner'
import { Switch } from '@/components/ui/switch'
import { toast } from 'sonner'
import { ArrowRightLeft } from 'lucide-react'
import { SettingsMenu } from '@/components/SettingsMenu'
import { useIsMobile } from '@/hooks/use-mobile'

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
  const { t } = useTranslation()
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
  const translateTimeoutRef = useRef<number | null>(null)

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

  return (
    <div className="min-h-screen bg-background p-3 sm:p-4 md:p-8">
      <div className="max-w-6xl mx-auto">
        <div className="flex justify-between items-center mb-4 sm:mb-6">
          <h1 className="text-xl sm:text-2xl font-bold text-foreground">
            {t('title')}
          </h1>
          <SettingsMenu 
            showTokenDialog={showTokenDialog}
            setShowTokenDialog={setShowTokenDialog}
            onTokenSaved={fetchLanguages}
          />
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
                    {languages.map((lang) => (
                      <SelectItem key={lang} value={lang}>
                        {lang === 'auto' ? t('autoDetect') : lang}
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
                    {languages.filter(lang => lang !== 'auto').map((lang) => (
                      <SelectItem key={lang} value={lang}>
                        {lang}
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
              <Textarea
                id="source-text"
                placeholder={t('enterText')}
                value={sourceText}
                onChange={(e) => handleSourceTextChange(e.target.value)}
                className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} resize-none text-base`}
                disabled={loading}
              />

              <Textarea
                id="translated-text"
                placeholder={t('translationWillAppear')}
                value={translatedText}
                readOnly
                className={`${isMobile ? 'min-h-[200px]' : 'min-h-[300px]'} resize-none text-base bg-muted`}
              />
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
    </div>
  )
}

export default App
