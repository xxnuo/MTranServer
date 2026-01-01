const languageAliases: Record<string, string> = {
  'zh': 'zh-Hans',
  'zh-cn': 'zh-Hans',
  'zh-sg': 'zh-Hans',
  'zh-hans': 'zh-Hans',
  'cmn': 'zh-Hans',
  'chinese': 'zh-Hans',
  'zh-tw': 'zh-Hant',
  'zh-hk': 'zh-Hant',
  'zh-mo': 'zh-Hant',
  'zh-hant': 'zh-Hant',
  'cht': 'zh-Hant',
  'en-us': 'en',
  'en-gb': 'en',
  'en-au': 'en',
  'en-ca': 'en',
  'en-nz': 'en',
  'en-ie': 'en',
  'en-za': 'en',
  'en-jm': 'en',
  'en-bz': 'en',
  'en-tt': 'en',
  'fr-fr': 'fr',
  'fr-ca': 'fr',
  'fr-be': 'fr',
  'fr-ch': 'fr',
  'es-es': 'es',
  'es-mx': 'es',
  'es-ar': 'es',
  'es-co': 'es',
  'es-cl': 'es',
  'es-pe': 'es',
  'es-ve': 'es',
  'pt-pt': 'pt',
  'pt-br': 'pt',
  'de-de': 'de',
  'de-at': 'de',
  'de-ch': 'de',
  'it-it': 'it',
  'it-ch': 'it',
  'ja-jp': 'ja',
  'jp': 'ja',
  'ko-kr': 'ko',
  'kr': 'ko',
  'ru-ru': 'ru',
  'nb': 'no',
};

export function NormalizeLanguageCode(code: string): string {
  if (!code) return '';

  const normalized = code.toLowerCase().replace(/_/g, '-');

  if (languageAliases[normalized]) {
    return languageAliases[normalized];
  }

  const mainCode = normalized.split('-')[0];
  if (languageAliases[mainCode]) {
    return languageAliases[mainCode];
  }

  return mainCode;
}
