import i18n from 'i18next'
import { initReactI18next } from 'react-i18next'
import LanguageDetector from 'i18next-browser-languagedetector'

const resources = {
  en: {
    translation: {
      title: 'MTranServer',
      subtitle: 'Fast and reliable translation service',
      translate: 'Translate',
      translating: 'Translating...',
      sourceLanguage: 'Source Language',
      targetLanguage: 'Target Language',
      sourceText: 'Source Text',
      translation: 'Translation',
      enterText: 'Enter text to translate...',
      translationWillAppear: 'Translation will appear here...',
      selectLanguage: 'Select language',
      autoDetect: 'Auto Detect',
      translationCompleted: 'Translation completed',
      translationFailed: 'Translation failed',
      enterTextError: 'Please enter text to translate',
      failedToLoadLanguages: 'Failed to load languages',
      theme: 'Theme',
      language: 'Language',
      light: 'Light',
      dark: 'Dark',
      system: 'System',
      autoTranslate: 'Auto Translate',
      autoTranslateDesc: 'Automatically translate as you type'
    }
  },
  zh: {
    translation: {
      title: 'MTranServer',
      subtitle: '快速可靠的翻译服务',
      translate: '翻译',
      translating: '翻译中...',
      sourceLanguage: '源语言',
      targetLanguage: '目标语言',
      sourceText: '源文本',
      translation: '翻译结果',
      enterText: '输入要翻译的文本...',
      translationWillAppear: '翻译结果将显示在这里...',
      selectLanguage: '选择语言',
      autoDetect: '自动检测',
      translationCompleted: '翻译完成',
      translationFailed: '翻译失败',
      enterTextError: '请输入要翻译的文本',
      failedToLoadLanguages: '加载语言列表失败',
      theme: '主题',
      language: '语言',
      light: '浅色',
      dark: '深色',
      system: '跟随系统',
      autoTranslate: '自动翻译',
      autoTranslateDesc: '输入时自动翻译'
    }
  },
  ja: {
    translation: {
      title: 'MTranServer',
      subtitle: '高速で信頼性の高い翻訳サービス',
      translate: '翻訳',
      translating: '翻訳中...',
      sourceLanguage: 'ソース言語',
      targetLanguage: 'ターゲット言語',
      sourceText: 'ソーステキスト',
      translation: '翻訳',
      enterText: '翻訳するテキストを入力...',
      translationWillAppear: '翻訳がここに表示されます...',
      selectLanguage: '言語を選択',
      autoDetect: '自動検出',
      translationCompleted: '翻訳完了',
      translationFailed: '翻訳失敗',
      enterTextError: '翻訳するテキストを入力してください',
      failedToLoadLanguages: '言語の読み込みに失敗しました',
      theme: 'テーマ',
      language: '言語',
      light: 'ライト',
      dark: 'ダーク',
      system: 'システム',
      autoTranslate: '自動翻訳',
      autoTranslateDesc: '入力時に自動翻訳'
    }
  }
}

i18n
  .use(LanguageDetector)
  .use(initReactI18next)
  .init({
    resources,
    fallbackLng: 'en',
    interpolation: {
      escapeValue: false
    },
    detection: {
      order: ['localStorage', 'navigator'],
      caches: ['localStorage']
    }
  })

export default i18n

