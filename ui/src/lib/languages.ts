
export interface LanguageOption {
  code: string;
  name: string;
}

export const getLanguageName = (code: string, locale: string): string => {
  try {
    const displayNames = new Intl.DisplayNames([locale], { type: 'language' });
    return displayNames.of(code) || code;
  } catch (e) {
    return code;
  }
};

export const getSortedLanguages = (languages: string[], locale: string): LanguageOption[] => {
  if (!languages || languages.length === 0) return [];

  console.log('getSortedLanguages input:', { languagesCount: languages.length, locale });

  let displayNames: Intl.DisplayNames | null = null;
  try {
    displayNames = new Intl.DisplayNames([locale], { type: 'language' });
  } catch (e) {
    console.warn('Intl.DisplayNames not supported or invalid locale', e);
  }

  const mapped = languages
    .filter(code => code !== 'auto')
    .map(code => {
      let name = code;
      if (displayNames) {
        try {
          name = displayNames.of(code) || code;
          // console.log(`Mapped ${code} -> ${name}`);
        } catch {
          // ignore error
        }
      }
      return { code, name };
    });
  
  // console.log('First mapped item:', mapped[0]);

  return mapped.sort((a, b) => a.name.localeCompare(b.name, locale));
};
