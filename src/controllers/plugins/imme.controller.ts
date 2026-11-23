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
    const { NormalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot, translateBatchWithPivot } = await import('@/services/index.js');
    const { getConfig } = await import('@/config/index.js');
    const logger = await import('@/logger/index.js');

    const config = getConfig();
    const apiToken = config.apiToken;

    if (apiToken && token !== apiToken) {
      this.setStatus(401);
      throw new Error('Unauthorized');
    }

    const sourceLang = 'auto';
    const targetLang = NormalizeLanguageCode(body.target_lang);

    const translations: ImmeTranslation[] = new Array(body.text_list.length);
    const validIndices: number[] = [];
    const validTexts: string[] = [];
    for (let i = 0; i < body.text_list.length; i++) {
      const text = body.text_list[i];
      if (!text || typeof text !== 'string') {
        translations[i] = { detected_source_lang: body.source_lang, text: '' };
      } else {
        validIndices.push(i);
        validTexts.push(text);
      }
    }

    if (validTexts.length > 0) {
      try {
        const results = await translateBatchWithPivot(sourceLang, targetLang, validTexts, false);
        for (let i = 0; i < validIndices.length; i++) {
          translations[validIndices[i]] = { detected_source_lang: body.source_lang, text: results[i] };
        }
      } catch {
        await Promise.all(
          validIndices.map(async (origIdx, j) => {
            const text = validTexts[j];
            let result: string;
            try {
              result = await translateWithPivot(sourceLang, targetLang, text, false);
            } catch (err) {
              logger.error(`Imme translation failed at index ${origIdx}: ${err}`);
              result = text;
            }
            translations[origIdx] = { detected_source_lang: body.source_lang, text: result };
          })
        );
      }
    }

    return { translations };
  }
}
