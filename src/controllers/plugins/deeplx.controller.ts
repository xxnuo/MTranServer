import { Controller, Post, Body, Route, Tags, SuccessResponse, Header, Query } from 'tsoa';

interface DeeplxTranslateRequest {
  text: string;
  source_lang: string;
  target_lang: string;
}

interface DeeplxTranslateResponse {
  alternatives: string[];
  code: number;
  data: string;
  id: number;
  method: string;
  source_lang: string;
  target_lang: string;
}

@Route('deeplx')
@Tags('Compatible APIs')
export class DeeplxController extends Controller {
  @Post('/')
  @SuccessResponse('200', 'Success')
  public async translate(
    @Body() body: DeeplxTranslateRequest,
    @Header('Authorization') authorization?: string,
    @Query('token') tokenQuery?: string
  ): Promise<DeeplxTranslateResponse> {
    const { NormalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');
    const { getConfig } = await import('@/config/index.js');

    const config = getConfig();
    const apiToken = config.apiToken;

    if (apiToken) {
      let token = '';
      if (authorization) {
        if (authorization.startsWith('Bearer ')) {
          token = authorization.replace('Bearer ', '');
        } else {
           token = authorization;
        }
      } else if (tokenQuery) {
        token = tokenQuery;
      }

      if (token !== apiToken) {
        this.setStatus(401);
        throw new Error('Unauthorized');
      }
    }

    const sourceLang = body.source_lang ? NormalizeLanguageCode(body.source_lang) : 'auto';
    const targetLang = NormalizeLanguageCode(body.target_lang);

    const text = body.text;

    // Use pivot translation. Assuming plain text for simplicity as per DeepLX usual usage.
    const result = await translateWithPivot(sourceLang, targetLang, text, false);

    const id = Math.floor(Math.random() * 10000000000);

    return {
      alternatives: [],
      code: 200,
      data: result,
      id: id,
      method: 'Free',
      source_lang: sourceLang.toUpperCase(),
      target_lang: targetLang.toUpperCase()
    };
  }
}
