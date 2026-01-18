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

let cachedDeps: {
  NormalizeLanguageCode: typeof import('@/utils/index.js').NormalizeLanguageCode;
  translateWithPivot: typeof import('@/services/index.js').translateWithPivot;
} | undefined;

async function getDeps() {
  if (cachedDeps) return cachedDeps;
  const [{ NormalizeLanguageCode }, { translateWithPivot }] = await Promise.all([
    import('@/utils/index.js'),
    import('@/services/index.js'),
  ]);
  return cachedDeps = { NormalizeLanguageCode, translateWithPivot };
}

@Route('translate')
@Tags('Translation')
export class TranslateController extends Controller {
  @Post('/')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translate(@Body() body: TranslateRequest): Promise<TranslateResponse> {
    const { NormalizeLanguageCode, translateWithPivot } = cachedDeps ?? await getDeps();

    const normalizedFrom = NormalizeLanguageCode(body.from);
    const normalizedTo = NormalizeLanguageCode(body.to);

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
    const { NormalizeLanguageCode, translateWithPivot } = cachedDeps ?? await getDeps();

    const normalizedFrom = NormalizeLanguageCode(body.from);
    const normalizedTo = NormalizeLanguageCode(body.to);
    const html = body.html || false;
    const results = await Promise.all(
      body.texts.map(text => translateWithPivot(normalizedFrom, normalizedTo, text, html))
    );

    return { results };
  }
}
