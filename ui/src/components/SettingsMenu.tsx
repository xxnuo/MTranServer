import { useState, useEffect, useMemo } from 'react'
import { useTranslation } from 'react-i18next'
import { useTheme } from '@/contexts/ThemeContext'
import { Button } from '@/components/ui/button'
import { Tooltip, TooltipContent, TooltipTrigger } from '@/components/ui/tooltip'
import { Input } from '@/components/ui/input'
import { Sun, Moon, Globe, Key, Check } from 'lucide-react'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog'
import { toast } from 'sonner'
import { Popover, PopoverContent, PopoverTrigger } from '@/components/ui/popover'
import {
  Command,
  CommandEmpty,
  CommandGroup,
  CommandInput,
  CommandItem,
  CommandList,
} from '@/components/ui/command'
import { ToggleGroup, ToggleGroupItem } from '@/components/ui/toggle-group'

interface SettingsMenuProps {
  showTokenDialog?: boolean
  setShowTokenDialog?: (show: boolean) => void
  onTokenSaved?: () => void
}

export function SettingsMenu({ showTokenDialog, setShowTokenDialog, onTokenSaved }: SettingsMenuProps) {
  const { t, i18n } = useTranslation()
  const { actualTheme, setTheme } = useTheme()
  const [tokenDialogOpen, setTokenDialogOpen] = useState(false)
  const [token, setToken] = useState(() => localStorage.getItem('apiToken') || '')
  const [languageOpen, setLanguageOpen] = useState(false)
  const [languageMode, setLanguageMode] = useState(() => localStorage.getItem('uiLangMode') || 'manual')
  const [recentLanguages, setRecentLanguages] = useState<string[]>(() => {
    try {
      const stored = localStorage.getItem('recentUILanguages')
      if (!stored) return []
      const parsed = JSON.parse(stored)
      return Array.isArray(parsed) ? parsed : []
    } catch {
      return []
    }
  })

  useEffect(() => {
    if (showTokenDialog !== undefined) {
      setTokenDialogOpen(showTokenDialog)
    }
  }, [showTokenDialog])

  const languages = [
    { code: 'en', name: 'English', short: 'EN' },
    { code: 'zh', name: '中文', short: '中' },
    { code: 'ja', name: '日本語', short: '日' }
  ]



  const getSystemLanguage = () => {
    const lang = navigator.language?.toLowerCase() || 'en'
    if (lang.startsWith('zh')) return 'zh'
    if (lang.startsWith('ja')) return 'ja'
    return 'en'
  }

  const saveRecentLanguage = (code: string) => {
    const next = [code, ...recentLanguages.filter(item => item !== code)].slice(0, 3)
    setRecentLanguages(next)
    localStorage.setItem('recentUILanguages', JSON.stringify(next))
  }

  const applyLanguage = (code: string) => {
    if (code === 'system') {
      const systemCode = getSystemLanguage()
      setLanguageMode('system')
      localStorage.setItem('uiLangMode', 'system')
      i18n.changeLanguage(systemCode)
    } else {
      setLanguageMode('manual')
      localStorage.setItem('uiLangMode', 'manual')
      i18n.changeLanguage(code)
      saveRecentLanguage(code)
    }
    setLanguageOpen(false)
  }

  useEffect(() => {
    if (languageMode !== 'system') return
    const systemCode = getSystemLanguage()
    if (systemCode !== i18n.language) {
      i18n.changeLanguage(systemCode)
    }
    const handleSystemChange = () => {
      const next = getSystemLanguage()
      if (next !== i18n.language) {
        i18n.changeLanguage(next)
      }
    }
    window.addEventListener('languagechange', handleSystemChange)
    return () => window.removeEventListener('languagechange', handleSystemChange)
  }, [languageMode, i18n])

  const handleSaveToken = () => {
    if (token.trim()) {
      localStorage.setItem('apiToken', token.trim())
      toast.success(t('apiTokenSaved'))
    } else {
      localStorage.removeItem('apiToken')
      toast.success(t('apiTokenCleared'))
    }
    const shouldClose = !showTokenDialog || token.trim() !== ''
    if (shouldClose) {
      setTokenDialogOpen(false)
      if (setShowTokenDialog) {
        setShowTokenDialog(false)
      }
      if (onTokenSaved) {
        onTokenSaved()
      }
    }
  }

  const handleDialogChange = (open: boolean) => {
    setTokenDialogOpen(open)
    if (setShowTokenDialog) {
      setShowTokenDialog(open)
    }
  }

  const resolvedLanguage = languageMode === 'system' ? getSystemLanguage() : i18n.language
  const currentLang = languages.find(lang => lang.code === resolvedLanguage) || languages[0]
  const recentOptions = useMemo(
    () => recentLanguages.flatMap(code => {
      const found = languages.find(lang => lang.code === code)
      return found ? [found] : []
    }),
    [recentLanguages]
  )
  const activeValue = languageMode === 'system' ? 'system' : currentLang.code
  return (
    <div className="flex gap-1 sm:gap-2">
      <Dialog open={tokenDialogOpen} onOpenChange={handleDialogChange}>
        <Tooltip>
          <TooltipTrigger asChild>
            <DialogTrigger asChild>
              <Button
                variant="ghost"
                size="icon"
                className="h-8 w-8 sm:h-9 sm:w-9"
                aria-label={t('apiToken')}
              >
                <Key className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
              </Button>
            </DialogTrigger>
          </TooltipTrigger>
          <TooltipContent>
            <p>{t('apiToken')}</p>
          </TooltipContent>
        </Tooltip>
        <DialogContent className="w-[calc(100%-2rem)] sm:max-w-md">
          <DialogHeader>
            <DialogTitle>{t('apiToken')}</DialogTitle>
            <DialogDescription>
              {showTokenDialog ? t('apiTokenRequired') : t('apiTokenPlaceholder')}
            </DialogDescription>
          </DialogHeader>
          <div className="space-y-4">
            <Input
              type="password"
              placeholder={t('apiTokenPlaceholder')}
              value={token}
              onChange={(e) => setToken(e.target.value)}
            />
            <Button onClick={handleSaveToken} className="w-full">
              {t('save')}
            </Button>
          </div>
        </DialogContent>
      </Dialog>
      <Popover open={languageOpen} onOpenChange={setLanguageOpen}>
        <Tooltip>
          <TooltipTrigger asChild>
            <PopoverTrigger asChild>
              <Button
                variant="ghost"
                size="icon"
                className="h-8 w-8 sm:h-9 sm:w-9"
                aria-label={t('switchLanguage')}
              >
                <Globe className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
              </Button>
            </PopoverTrigger>
          </TooltipTrigger>
          <TooltipContent>
            <p>{t('switchLanguage')}</p>
          </TooltipContent>
        </Tooltip>
        <PopoverContent className="w-72 p-2" align="end">
          <div className="mb-2 text-sm font-medium">{t('languageSwitcher')}</div>
          <div className="mb-3">
            <ToggleGroup
              type="single"
              value={activeValue}
              onValueChange={(value) => {
                if (value) applyLanguage(value)
              }}
              variant="outline"
              size="sm"
              className="w-full justify-between"
            >
              <ToggleGroupItem value="system" className="flex-1">
                {t('system')}
              </ToggleGroupItem>
              {languages.map((lang) => (
                <ToggleGroupItem key={lang.code} value={lang.code} className="flex-1">
                  {lang.short}
                </ToggleGroupItem>
              ))}
            </ToggleGroup>
          </div>
          <Command>
            <CommandInput placeholder={t('languageSearchPlaceholder')} autoFocus />
            <CommandList>
              <CommandEmpty>{t('noResults')}</CommandEmpty>
              <CommandGroup heading={t('followSystemLanguage')}>
                <CommandItem value="system" onSelect={() => applyLanguage('system')}>
                  <span className="flex-1">{t('followSystemLanguage')}</span>
                  {languageMode === 'system' && <Check className="h-4 w-4" />}
                </CommandItem>
              </CommandGroup>
              {recentOptions.length > 0 && (
                <CommandGroup heading={t('languageRecent')}>
                  {recentOptions.map((lang) => (
                    <CommandItem key={lang.code} value={lang.code} onSelect={() => applyLanguage(lang.code)}>
                      <span className="flex-1">{lang.name}</span>
                      {languageMode !== 'system' && lang.code === currentLang.code && (
                        <Check className="h-4 w-4" />
                      )}
                    </CommandItem>
                  ))}
                </CommandGroup>
              )}
              <CommandGroup heading={t('languageAll')}>
                {languages.map((lang) => (
                  <CommandItem key={lang.code} value={lang.code} onSelect={() => applyLanguage(lang.code)}>
                    <span className="flex-1">{lang.name}</span>
                    {languageMode !== 'system' && lang.code === currentLang.code && (
                      <Check className="h-4 w-4" />
                    )}
                  </CommandItem>
                ))}
              </CommandGroup>
            </CommandList>
          </Command>
        </PopoverContent>
      </Popover>
      <Popover>
        <Tooltip>
          <TooltipTrigger asChild>
            <PopoverTrigger asChild>
              <Button
                variant="ghost"
                size="icon"
                className="h-8 w-8 sm:h-9 sm:w-9"
                aria-label={t('theme')}
              >
                {actualTheme === 'dark' ? (
                  <Moon className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
                ) : (
                  <Sun className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
                )}
              </Button>
            </PopoverTrigger>
          </TooltipTrigger>
          <TooltipContent>
            <p>{t('theme')}</p>
          </TooltipContent>
        </Tooltip>
        <PopoverContent className="w-80 p-2" align="end">
          <div className="mb-2 text-sm font-medium">{t('themeMode')}</div>
          <ToggleGroup
            type="single"
            value={useTheme().theme} 
            onValueChange={(value) => {
               if (value) setTheme(value as any)
            }}
            variant="outline"
            size="sm"
            className="w-full grid grid-cols-3 gap-1"
          >
            <ToggleGroupItem value="light" aria-label={t('light')}>
              <Sun className="h-4 w-4 mr-2" />
              <span className="sr-only sm:not-sr-only sm:inline-block">{t('light')}</span>
            </ToggleGroupItem>
            <ToggleGroupItem value="dark" aria-label={t('dark')}>
              <Moon className="h-4 w-4 mr-2" />
               <span className="sr-only sm:not-sr-only sm:inline-block">{t('dark')}</span>
            </ToggleGroupItem>
            <ToggleGroupItem value="system" aria-label={t('system')}>
               <span className="text-xs">{t('system')}</span>
            </ToggleGroupItem>
          </ToggleGroup>
        </PopoverContent>
      </Popover>
    </div>
  )
}
