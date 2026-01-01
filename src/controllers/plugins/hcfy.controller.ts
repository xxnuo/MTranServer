import { Controller, Post, Body, Route, Tags, Security, SuccessResponse } from 'tsoa';

interface HcfyTranslateRequest {
  name?: string;
  text: string;
  destination: string[];
  source?: string;
}

interface HcfyTranslateResponse {
  text: string;
  from: string;
  to: string;
  ttsURI?: string;
  link?: string;
  phonetic?: any[];
  dict?: any[];
  result?: string[];
}

@Route('hcfy')
@Tags('Compatible APIs')
export class HcfyController extends Controller {
  @Post('/')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translate(@Body() body: HcfyTranslateRequest): Promise<HcfyTranslateResponse> {
    const { translateWithPivot } = await import('@/services/index.js');

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
    };

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
    };

    function convertHcfyLangToBCP47(hcfyLang: string): string {
      return hcfyLangToBCP47[hcfyLang] || hcfyLang;
    }

    function convertBCP47ToHcfyLang(bcp47Lang: string): string {
      return bcp47ToHcfyLang[bcp47Lang] || bcp47Lang;
    }

    function containsChinese(text: string): boolean {
      for (const r of text) {
        const code = r.charCodeAt(0);
        if (code >= 0x4e00 && code <= 0x9fff) {
          return true;
        }
      }
      return false;
    }

    function containsJapanese(text: string): boolean {
      for (const r of text) {
        const code = r.charCodeAt(0);
        if ((code >= 0x3040 && code <= 0x309f) || (code >= 0x30a0 && code <= 0x30ff)) {
          return true;
        }
      }
      return false;
    }

    function containsKorean(text: string): boolean {
      for (const r of text) {
        const code = r.charCodeAt(0);
        if (code >= 0xac00 && code <= 0xd7af) {
          return true;
        }
      }
      return false;
    }

    let sourceLang = 'auto';
    if (body.source) {
      sourceLang = convertHcfyLangToBCP47(body.source);
    }

    const targetLangName = body.destination[0];
    let targetLang = convertHcfyLangToBCP47(targetLangName);

    let detectedSourceLang = sourceLang;
    if (sourceLang === 'auto') {
      if (containsChinese(body.text)) {
        detectedSourceLang = 'zh-Hans';
      } else if (containsJapanese(body.text)) {
        detectedSourceLang = 'ja';
      } else if (containsKorean(body.text)) {
        detectedSourceLang = 'ko';
      } else {
        detectedSourceLang = 'en';
      }
    }

    if (detectedSourceLang === targetLang && body.destination.length > 1) {
      const altTargetLangName = body.destination[1];
      targetLang = convertHcfyLangToBCP47(altTargetLangName);
    }

    const paragraphs = body.text.split('\n');
    const results: string[] = [];

    for (const paragraph of paragraphs) {
      if (!paragraph.trim()) {
        results.push('');
        continue;
      }

      const result = await translateWithPivot(detectedSourceLang, targetLang, paragraph, false);
      results.push(result);
    }

    return {
      text: body.text,
      from: convertBCP47ToHcfyLang(detectedSourceLang),
      to: body.destination[0],
      result: results,
    };
  }
}
