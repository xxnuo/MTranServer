import { useTranslation } from 'react-i18next'
import { useTheme } from '@/contexts/ThemeContext'
import { Button } from '@/components/ui/button'
import { Sun, Moon, Globe } from 'lucide-react'

export function SettingsMenu() {
  const { i18n } = useTranslation()
  const { actualTheme, setTheme } = useTheme()

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

  const currentLang = languages.find(lang => lang.code === i18n.language) || languages[0]

  return (
    <div className="flex gap-2">
      <Button 
        variant="ghost" 
        size="icon"
        onClick={cycleLanguage}
        className="h-9 w-9"
        title="Switch Language"
      >
        <Globe className="h-4 w-4" />
        <span className="sr-only">{currentLang.name}</span>
      </Button>
      <Button 
        variant="ghost" 
        size="icon"
        onClick={toggleTheme}
        className="h-9 w-9"
        title="Toggle Theme"
      >
        {actualTheme === 'dark' ? (
          <Sun className="h-4 w-4" />
        ) : (
          <Moon className="h-4 w-4" />
        )}
      </Button>
    </div>
  )
}

