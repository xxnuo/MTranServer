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
  utils: typeof import('@/utils/index.js');
  services: typeof import('@/services/index.js');
} | undefined;

async function getDeps() {
  if (cachedDeps) return cachedDeps;
  const [utils, services] = await Promise.all([
    import('@/utils/index.js'),
    import('@/services/index.js'),
  ]);
  return cachedDeps = { utils, services };
}

@Route('translate')
@Tags('Translation')
export class TranslateController extends Controller {
  @Post('/')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async translate(@Body() body: TranslateRequest): Promise<TranslateResponse> {
    const { utils, services } = cachedDeps ?? await getDeps();

    const normalizedFrom = utils.NormalizeLanguageCode(body.from);
    const normalizedTo = utils.NormalizeLanguageCode(body.to);

    const result = await services.translateWithPivot(
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
    const { utils, services } = cachedDeps ?? await getDeps();

    const normalizedFrom = utils.NormalizeLanguageCode(body.from);
    const normalizedTo = utils.NormalizeLanguageCode(body.to);
    const html = body.html || false;
    const results = await services.translateBatchWithPivot(normalizedFrom, normalizedTo, body.texts, html);

    return { results };
  }
}
