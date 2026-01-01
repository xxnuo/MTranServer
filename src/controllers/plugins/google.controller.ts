import { Controller, Post, Get, Body, Route, Tags, Security, SuccessResponse, Query } from 'tsoa';

interface GoogleTranslateRequest {
  q: string | string[];
  source: string;
  target: string;
  format?: 'text' | 'html';
}

interface GoogleTranslateResponse {
  data: {
    translations: Array<{
      translatedText: string;
      detectedSourceLanguage?: string;
    }>;
  };
}

@Route('google')
@Tags('Compatible APIs')
export class GoogleController extends Controller {
  @Post('language/translate/v2')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translateCompat(@Body() body: GoogleTranslateRequest): Promise<GoogleTranslateResponse> {
    const { normalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');

    const queries = Array.isArray(body.q) ? body.q : [body.q];
    const sourceBCP47 = normalizeLanguageCode(body.source);
    const targetBCP47 = normalizeLanguageCode(body.target);
    const isHTML = body.format === 'html';

    const translations = [];
    for (const q of queries) {
      const result = await translateWithPivot(sourceBCP47, targetBCP47, q, isHTML);
      translations.push({
        translatedText: result,
        detectedSourceLanguage: body.source,
      });
    }

    return {
      data: { translations },
    };
  }

  @Get('translate_a/single')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translateSingle(
    @Query() sl: string = 'auto',
    @Query() tl: string,
    @Query() q: string
  ): Promise<any> {
    const { normalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');

    const bcp47ToGoogleLang: Record<string, string> = {
      'zh-Hans': 'zh-CN',
      'zh-Hant': 'zh-TW',
    };

    function convertBCP47ToGoogleLang(bcp47Lang: string): string {
      return bcp47ToGoogleLang[bcp47Lang] || bcp47Lang;
    }

    const sourceBCP47 = normalizeLanguageCode(sl);
    const targetBCP47 = normalizeLanguageCode(tl);

    const result = await translateWithPivot(sourceBCP47, targetBCP47, q, false);

    const detectedLang = convertBCP47ToGoogleLang(sourceBCP47);
    return [
      [[result, q, null, null, 1]],
      null,
      detectedLang,
      null,
      null,
      null,
      null,
      [],
    ];
  }
}
