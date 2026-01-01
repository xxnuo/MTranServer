import { Controller, Get, Post, Body, Route, Tags, Security, SuccessResponse } from 'tsoa';

interface LanguagePair {
  from: string;
  to: string;
}

interface LanguageListResponse {
  languages: string[];
  pairs: LanguagePair[];
}

interface DetectLanguageRequest {
  text: string;
  minConfidence?: number;
}

interface DetectLanguageResponse {
  language: string;
  confidence?: number;
}

@Route('')
@Tags('Language')
export class LanguageController extends Controller {
  @Get('languages')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async getLanguages(): Promise<LanguageListResponse> {
    const { getSupportedLanguages, getLanguagePairs } = await import('@/models/index.js');
    const languages = getSupportedLanguages();
    const pairStrings = getLanguagePairs();
    const pairs: LanguagePair[] = pairStrings.map((p) => {
      const [from, to] = p.split('-');
      return { from, to };
    });
    return { languages, pairs };
  }

  @Post('detect')
  @Security('api_token')
  @SuccessResponse('200', 'Success')
  public async detectLanguage(
    @Body() body: DetectLanguageRequest
  ): Promise<DetectLanguageResponse> {
    const { detectLanguage, detectLanguageWithConfidence } = await import(
      '@/services/detector.js'
    );

    if (body.minConfidence !== undefined) {
      const result = await detectLanguageWithConfidence(body.text, body.minConfidence);
      return result;
    } else {
      const language = await detectLanguage(body.text);
      return { language };
    }
  }
}
