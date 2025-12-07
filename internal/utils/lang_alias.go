package utils

import (
	"strings"
)

// languageAliases maps common language code aliases to the internal BCP 47 representation
var languageAliases = map[string]string{
	// Chinese
	"zh":      "zh-Hans",
	"zh-cn":   "zh-Hans",
	"zh-sg":   "zh-Hans",
	"zh-hans": "zh-Hans",
	"cmn":     "zh-Hans",
	"chinese": "zh-Hans",

	"zh-tw":   "zh-Hant",
	"zh-hk":   "zh-Hant",
	"zh-mo":   "zh-Hant",
	"zh-hant": "zh-Hant",
	"cht":     "zh-Hant",

	// English
	"en-us": "en",
	"en-gb": "en",
	"en-au": "en",
	"en-ca": "en",
	"en-nz": "en",
	"en-ie": "en",
	"en-za": "en",
	"en-jm": "en",
	"en-bz": "en",
	"en-tt": "en",

	// French
	"fr-fr": "fr",
	"fr-ca": "fr",
	"fr-be": "fr",
	"fr-ch": "fr",

	// Spanish
	"es-es": "es",
	"es-mx": "es",
	"es-ar": "es",
	"es-co": "es",
	"es-cl": "es",
	"es-pe": "es",
	"es-ve": "es",

	// Portuguese
	"pt-pt": "pt",
	"pt-br": "pt",

	// German
	"de-de": "de",
	"de-at": "de",
	"de-ch": "de",

	// Italian
	"it-it": "it",
	"it-ch": "it",

	// Japanese
	"ja-jp": "ja",
	"jp":    "ja",

	// Korean
	"ko-kr": "ko",
	"kr":    "ko",

	// Russian
	"ru-ru": "ru",

	// Norwegian
	"nb": "no",
}

// NormalizeLanguageCode normalizes a language code to the internal BCP 47 format
func NormalizeLanguageCode(code string) string {
	if code == "" {
		return ""
	}

	// Normalize format: lowercase and replace underscore with dash
	code = strings.ToLower(strings.ReplaceAll(code, "_", "-"))

	// Check direct aliases first
	if normalized, ok := languageAliases[code]; ok {
		return normalized
	}

	// Handle standard BCP 47 fallback (e.g., en-US -> en) if not explicitly in map
	parts := strings.Split(code, "-")
	mainCode := parts[0]

	// Check if the main code itself is an alias or needs normalization (unlikely for simple ones but good for safety)
	if normalized, ok := languageAliases[mainCode]; ok {
		return normalized
	}

	return mainCode
}
