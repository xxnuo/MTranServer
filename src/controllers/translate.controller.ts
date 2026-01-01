import { Controller, Post, Body, Route, Tags, Security, SuccessResponse } from 'tsoa';

interface TranslateRequest {
  from: string;
  to: string;
  text: string;
  html?: boolean;
}

interface TranslateResponse {
  result: string;
}

interface TranslateBatchRequest {
  from: string;
  to: string;
  texts: string[];
  html?: boolean;
}

interface TranslateBatchResponse {
  results: string[];
}

@Route('translate')
@Tags('Translation')
export class TranslateController extends Controller {
  @Post('/')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translate(@Body() body: TranslateRequest): Promise<TranslateResponse> {
    const { normalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');

    const normalizedFrom = normalizeLanguageCode(body.from);
    const normalizedTo = normalizeLanguageCode(body.to);

    const result = await translateWithPivot(
      normalizedFrom,
      normalizedTo,
      body.text,
      body.html || false
    );

    return { result };
  }

  @Post('batch')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translateBatch(
    @Body() body: TranslateBatchRequest
  ): Promise<TranslateBatchResponse> {
    const { normalizeLanguageCode } = await import('@/utils/index.js');
    const { translateWithPivot } = await import('@/services/index.js');

    const normalizedFrom = normalizeLanguageCode(body.from);
    const normalizedTo = normalizeLanguageCode(body.to);

    const results: string[] = [];

    for (let i = 0; i < body.texts.length; i++) {
      const result = await translateWithPivot(
        normalizedFrom,
        normalizedTo,
        body.texts[i],
        body.html || false
      );
      results.push(result);
    }

    return { results };
  }
}
