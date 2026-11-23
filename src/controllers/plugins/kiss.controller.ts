import { Controller, Post, Body, Route, Tags, Security, SuccessResponse } from 'tsoa';

interface KissTranslateRequest {
  from: string;
  to: string;
  text?: string;
  texts?: string[];
}

interface KissTranslateResponse {
  text?: string;
  src?: string;
  translations?: Array<{
    text: string;
    src: string;
  }>;
}

@Route('kiss')
@Tags('Compatible APIs')
export class KissController extends Controller {
  @Post('/')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translate(@Body() body: KissTranslateRequest): Promise<KissTranslateResponse> {
    const { NormalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot, translateBatchWithPivot } = await import('@/services/index.js');

    const fromLang = NormalizeLanguageCode(body.from);
    const toLang = NormalizeLanguageCode(body.to);

    if (body.texts && Array.isArray(body.texts) && body.texts.length > 0) {
      const results = await translateBatchWithPivot(fromLang, toLang, body.texts, false);
      return {
        translations: results.map(text => ({ text, src: body.from })),
      };
    }

    if (body.text) {
      const result = await translateWithPivot(fromLang, toLang, body.text, false);
      return {
        text: result,
        src: body.from,
      };
    }

    throw new Error('Missing text or texts field');
  }
}
