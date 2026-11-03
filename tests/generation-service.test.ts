import { afterEach, expect, mock, spyOn, test } from 'bun:test';
import { GenerationLimitError, TranslationEngine } from '../src/core/engine';
import { cleanupAllEngines, translateWithPivot } from '../src/services/engine';
import * as models from '../src/models';
import * as detector from '../src/services/detector';
import * as config from '../src/config';

afterEach(async () => { await cleanupAllEngines(); mock.restore(); });

function failingEngine() {
  spyOn(models, 'prepareModel').mockResolvedValue({} as any);
  spyOn(TranslationEngine.prototype, 'init').mockResolvedValue(undefined);
  return spyOn(TranslationEngine.prototype, 'translateAsync').mockRejectedValue(new GenerationLimitError());
}

test('automatic long-text translation does not return untranslated sentences after recovery is exhausted', async () => {
  failingEngine();
  const text = 'First sentence. Second sentence.';
  spyOn(config, 'getConfig').mockReturnValue({ ...config.getConfig(), maxSentenceLength: 8 });
  spyOn(detector, 'detectMultipleLanguages').mockResolvedValue([{ language: 'en', text, start: 0, end: text.length }] as any);
  await expect(translateWithPivot('auto', 'zh-Hans', text)).rejects.toBeInstanceOf(GenerationLimitError);
});

test('automatic multilingual translation does not return a partially untranslated successful result', async () => {
  const translate = failingEngine();
  spyOn(detector, 'detectMultipleLanguages').mockResolvedValue([
    { language: 'en', text: 'Hello ', start: 0, end: 6 },
    { language: 'fr', text: 'bonjour', start: 6, end: 13 },
  ] as any);
  await expect(translateWithPivot('auto', 'zh-Hans', 'Hello bonjour')).rejects.toBeInstanceOf(GenerationLimitError);
  expect(translate).toHaveBeenCalledTimes(1);
});
