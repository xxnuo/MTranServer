import { Request, Response } from 'express'
import * as services from '@/services/index.js'
import * as logger from '@/logger/index.js'
import { normalizeLanguageCode } from '@/utils/lang-alias.js'

const bcp47ToGoogleLang: Record<string, string> = {
  'zh-Hans': 'zh-CN',
  'zh-Hant': 'zh-TW',
}

function convertBCP47ToGoogleLang(bcp47Lang: string): string {
  return bcp47ToGoogleLang[bcp47Lang] || bcp47Lang
}

export interface GoogleTranslateRequest {
  q: string
  source: string
  target: string
  format?: string
}

export interface GoogleTranslateResponse {
  data: {
    translations: Array<{
      translatedText: string
    }>
  }
}

export async function handleGoogleCompatTranslate(req: Request, res: Response): Promise<void> {
  try {
    const { q, source, target, format } = req.body as GoogleTranslateRequest

    if (!q || !source || !target) {
      res.status(400).json({ error: 'Missing required parameters: q, source, target' })
      return
    }

    const sourceBCP47 = normalizeLanguageCode(source)
    const targetBCP47 = normalizeLanguageCode(target)
    const isHTML = format === 'html'

    const result = await services.translateWithPivot(sourceBCP47, targetBCP47, q, isHTML)

    res.json({
      data: {
        translations: [{ translatedText: result }]
      }
    })
  } catch (error: any) {
    logger.error(`Google compat translate error: ${error.message}`)
    res.status(500).json({ error: `Translation failed: ${error.message}` })
  }
}

export async function handleGoogleTranslateSingle(req: Request, res: Response): Promise<void> {
  try {
    const sl = req.query.sl as string || 'auto'
    const tl = req.query.tl as string
    const q = req.query.q as string

    if (!tl || !q) {
      res.status(400).json({ error: 'Missing required parameters: tl, q' })
      return
    }

    const sourceBCP47 = normalizeLanguageCode(sl)
    const targetBCP47 = normalizeLanguageCode(tl)

    const result = await services.translateWithPivot(sourceBCP47, targetBCP47, q, false)

    const detectedLang = convertBCP47ToGoogleLang(sourceBCP47)
    const response = [
      [[result, q, null, null, 1]],
      null,
      detectedLang,
      null,
      null,
      null,
      null,
      []
    ]

    res.json(response)
  } catch (error: any) {
    logger.error(`Google translate single error: ${error.message}`)
    res.status(500).json({ error: `Translation failed: ${error.message}` })
  }
}
