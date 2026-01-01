import { Controller, Post, Body, Route, Tags, Query, SuccessResponse } from 'tsoa';

interface ImmeTranslateRequest {
  source_lang: string;
  target_lang: string;
  text_list: string[];
}

interface ImmeTranslation {
  detected_source_lang: string;
  text: string;
}

interface ImmeTranslateResponse {
  translations: ImmeTranslation[];
}

@Route('imme')
@Tags('Compatible APIs')
export class ImmeController extends Controller {
  @Post('/')
  @SuccessResponse('200', 'Success')
  public async translate(
    @Body() body: ImmeTranslateRequest,
    @Query() token?: string
  ): Promise<ImmeTranslateResponse> {
    const { normalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');
    const { getConfig } = await import('@/config/index.js');
    const logger = await import('@/logger/index.js');

    const config = getConfig();
    const apiToken = config.apiToken;

    if (apiToken && token !== apiToken) {
      this.setStatus(401);
      throw new Error('Unauthorized');
    }

    const sourceLang = normalizeLanguageCode(body.source_lang);
    const targetLang = normalizeLanguageCode(body.target_lang);

    const translations: ImmeTranslation[] = [];

    for (let i = 0; i < body.text_list.length; i++) {
      const text = body.text_list[i];

      if (!text || typeof text !== 'string') {
        translations.push({
          detected_source_lang: body.source_lang,
          text: '',
        });
        continue;
      }

      let result: string;
      try {
        result = await translateWithPivot(sourceLang, targetLang, text, false);
      } catch (err) {
        logger.error(`Imme translation failed at index ${i}: ${err}`);
        result = text;
      }

      translations.push({
        detected_source_lang: body.source_lang,
        text: result,
      });
    }

    return { translations };
  }
}
