import { Request, Response } from 'express'
import * as services from '../services/index.js'
import * as logger from '../logger/index.js'

const hcfyLangToBCP47: Record<string, string> = {
  '中文(简体)': 'zh-Hans',
  '中文(繁体)': 'zh-Hant',
  '英语': 'en',
  '日语': 'ja',
  '韩语': 'ko',
  '法语': 'fr',
  '德语': 'de',
  '西班牙语': 'es',
  '俄语': 'ru',
  '意大利语': 'it',
  '葡萄牙语': 'pt',
}

const bcp47ToHcfyLang: Record<string, string> = {
  'zh-Hans': '中文(简体)',
  'zh-CN': '中文(简体)',
  'zh-Hant': '中文(繁体)',
  'zh-TW': '中文(繁体)',
  'en': '英语',
  'ja': '日语',
  'ko': '韩语',
  'fr': '法语',
  'de': '德语',
  'es': '西班牙语',
  'ru': '俄语',
  'it': '意大利语',
  'pt': '葡萄牙语',
}

function convertHcfyLangToBCP47(hcfyLang: string): string {
  return hcfyLangToBCP47[hcfyLang] || hcfyLang
}

function convertBCP47ToHcfyLang(bcp47Lang: string): string {
  return bcp47ToHcfyLang[bcp47Lang] || bcp47Lang
}

function containsChinese(text: string): boolean {
  for (const r of text) {
    const code = r.charCodeAt(0)
    if (code >= 0x4E00 && code <= 0x9FFF) {
      return true
    }
  }
  return false
}

function containsJapanese(text: string): boolean {
  for (const r of text) {
    const code = r.charCodeAt(0)
    if ((code >= 0x3040 && code <= 0x309F) || (code >= 0x30A0 && code <= 0x30FF)) {
      return true
    }
  }
  return false
}

function containsKorean(text: string): boolean {
  for (const r of text) {
    const code = r.charCodeAt(0)
    if (code >= 0xAC00 && code <= 0xD7AF) {
      return true
    }
  }
  return false
}

export interface HcfyTranslateRequest {
  name: string
  text: string
  destination: string[]
  source?: string
}

export interface HcfyTranslateResponse {
  text: string
  from: string
  to: string
  ttsURI?: string
  link?: string
  phonetic?: any[]
  dict?: any[]
  result?: string[]
}

export async function handleHcfyTranslate(req: Request, res: Response): Promise<void> {
  try {
    const { name, text, destination, source } = req.body as HcfyTranslateRequest

    if (!text || !destination || destination.length === 0) {
      res.status(400).json({ error: 'Missing required fields: text, destination' })
      return
    }

    let sourceLang = 'auto'
    if (source) {
      sourceLang = convertHcfyLangToBCP47(source)
    }

    const targetLangName = destination[0]
    let targetLang = convertHcfyLangToBCP47(targetLangName)

    let detectedSourceLang = sourceLang
    if (sourceLang === 'auto') {
      if (containsChinese(text)) {
        detectedSourceLang = 'zh-Hans'
      } else if (containsJapanese(text)) {
        detectedSourceLang = 'ja'
      } else if (containsKorean(text)) {
        detectedSourceLang = 'ko'
      } else {
        detectedSourceLang = 'en'
      }
    }

    if (detectedSourceLang === targetLang && destination.length > 1) {
      const altTargetLangName = destination[1]
      targetLang = convertHcfyLangToBCP47(altTargetLangName)
    }

    const paragraphs = text.split('\n')
    const results: string[] = []

    for (const paragraph of paragraphs) {
      if (!paragraph.trim()) {
        results.push('')
        continue
      }

      const result = await services.translateWithPivot(detectedSourceLang, targetLang, paragraph, false)
      results.push(result)
    }

    res.json({
      text,
      from: convertBCP47ToHcfyLang(detectedSourceLang),
      to: destination[0],
      result: results
    })
  } catch (error: any) {
    logger.error(`HCFY translate error: ${error.message}`)
    res.status(500).json({ error: `Translation failed: ${error.message}` })
  }
}
