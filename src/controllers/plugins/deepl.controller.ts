import { Controller, Post, Body, Route, Tags, Security, SuccessResponse, Header } from 'tsoa';

interface DeeplTranslateRequest {
  text: string | string[];
  source_lang?: string;
  target_lang: string;
  tag_handling?: 'html' | 'xml' | 'text';
}

interface DeeplTranslation {
  detected_source_language: string;
  text: string;
}

interface DeeplTranslateResponse {
  translations: DeeplTranslation[];
}

@Route('deepl')
@Tags('Compatible APIs')
export class DeeplController extends Controller {
  @Post('/')
  @SuccessResponse('200', 'Success')
  public async translate(
    @Body() body: DeeplTranslateRequest,
    @Header('Authorization') authorization?: string
  ): Promise<DeeplTranslateResponse> {
    const { NormalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');
    const { getConfig } = await import('@/config/index.js');

    const config = getConfig();
    const apiToken = config.apiToken;

    if (apiToken) {
      let token = '';
      if (authorization) {
        if (authorization.startsWith('DeepL-Auth-Key ')) {
          token = authorization.replace('DeepL-Auth-Key ', '');
        } else {
          token = authorization.replace('Bearer ', '');
        }
      }

      if (token !== apiToken) {
        this.setStatus(401);
        throw new Error('Unauthorized');
      }
    }

    const bcp47ToDeeplLang: Record<string, string> = {
      'no': 'NB',
      'zh-Hans': 'ZH',
      'zh-CN': 'ZH-CN',
      'zh-Hant': 'ZH-TW',
      'zh-TW': 'ZH-TW',
    };

    function convertBCP47ToDeeplLang(bcp47Lang: string): string {
      return bcp47ToDeeplLang[bcp47Lang] || bcp47Lang.toUpperCase();
    }

    const textArray = Array.isArray(body.text) ? body.text : [body.text];
    const sourceLang = body.source_lang ? NormalizeLanguageCode(body.source_lang) : 'auto';
    const targetLang = NormalizeLanguageCode(body.target_lang);
    const isHTML = body.tag_handling === 'html' || body.tag_handling === 'xml';
    const detectedLang = body.source_lang || convertBCP47ToDeeplLang(sourceLang);
    const translations: DeeplTranslation[] = await Promise.all(
      textArray.map(async item => ({
        detected_source_language: detectedLang,
        text: await translateWithPivot(sourceLang, targetLang, item, isHTML),
      }))
    );

    return { translations };
  }
}
