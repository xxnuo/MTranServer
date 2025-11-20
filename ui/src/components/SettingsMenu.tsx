import { useState, useEffect } from 'react'
import { useTranslation } from 'react-i18next'
import { useTheme } from '@/contexts/ThemeContext'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Sun, Moon, Globe, Key } from 'lucide-react'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from '@/components/ui/dialog'
import { toast } from 'sonner'

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

  useEffect(() => {
    if (showTokenDialog !== undefined) {
      setTokenDialogOpen(showTokenDialog)
    }
  }, [showTokenDialog])

  const languages = [
    { code: 'en', name: 'EN' },
    { code: 'zh', name: '中' },
    { code: 'ja', name: '日' }
  ]

  const toggleTheme = () => {
    setTheme(actualTheme === 'dark' ? 'light' : 'dark')
  }

  const cycleLanguage = () => {
    const currentIndex = languages.findIndex(lang => lang.code === i18n.language)
    const nextIndex = (currentIndex + 1) % languages.length
    i18n.changeLanguage(languages[nextIndex].code)
  }

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

  const currentLang = languages.find(lang => lang.code === i18n.language) || languages[0]

  return (
    <div className="flex gap-1 sm:gap-2">
      <Dialog open={tokenDialogOpen} onOpenChange={handleDialogChange}>
        <DialogTrigger asChild>
          <Button
            variant="ghost"
            size="icon"
            className="h-8 w-8 sm:h-9 sm:w-9"
            title="API Token"
          >
            <Key className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
          </Button>
        </DialogTrigger>
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
      <Button
        variant="ghost"
        size="icon"
        onClick={cycleLanguage}
        className="h-8 w-8 sm:h-9 sm:w-9"
        title="Switch Language"
      >
        <Globe className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
        <span className="sr-only">{currentLang.name}</span>
      </Button>
      <Button
        variant="ghost"
        size="icon"
        onClick={toggleTheme}
        className="h-8 w-8 sm:h-9 sm:w-9"
        title="Toggle Theme"
      >
        {actualTheme === 'dark' ? (
          <Sun className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
        ) : (
          <Moon className="h-3.5 w-3.5 sm:h-4 sm:w-4" />
        )}
      </Button>
    </div>
  )
}

