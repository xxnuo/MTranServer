import { Request, Response } from 'express'
import * as services from '../services/index.js'
import * as logger from '../logger/index.js'
import { normalizeLanguageCode } from '../utils/lang-alias.js'

export interface KissTranslateRequest {
  from: string
  to: string
  text: string
}

export interface KissBatchTranslateRequest {
  from: string
  to: string
  texts: string[]
}

export interface KissTranslateResponse {
  text: string
  src: string
}

export interface KissBatchTranslateResponse {
  translations: Array<{
    text: string
    src: string
  }>
}

export async function handleKissTranslate(req: Request, res: Response): Promise<void> {
  try {
    const rawReq = req.body

    if (rawReq.texts && Array.isArray(rawReq.texts) && rawReq.texts.length > 0) {
      await handleKissBatchTranslate(req, res)
      return
    }

    const { from, to, text } = rawReq as KissTranslateRequest

    if (!from || !to || !text) {
      res.status(400).json({ error: 'Missing required fields: from, to, text' })
      return
    }

    const fromLang = normalizeLanguageCode(from)
    const toLang = normalizeLanguageCode(to)

    const result = await services.translateWithPivot(fromLang, toLang, text, false)

    res.json({
      text: result,
      src: from
    })
  } catch (error: any) {
    logger.error(`Kiss translate error: ${error.message}`)
    res.status(500).json({ error: `Translation failed: ${error.message}` })
  }
}

async function handleKissBatchTranslate(req: Request, res: Response): Promise<void> {
  try {
    const { from, to, texts } = req.body as KissBatchTranslateRequest

    if (!from || !to || !texts || texts.length === 0) {
      res.status(400).json({ error: 'Invalid batch request' })
      return
    }

    const fromLang = normalizeLanguageCode(from)
    const toLang = normalizeLanguageCode(to)

    const translations = []
    for (const text of texts) {
      const result = await services.translateWithPivot(fromLang, toLang, text, false)
      translations.push({
        text: result,
        src: from
      })
    }

    res.json({ translations })
  } catch (error: any) {
    logger.error(`Kiss batch translate error: ${error.message}`)
    res.status(500).json({ error: `Translation failed: ${error.message}` })
  }
}
