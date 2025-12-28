package services

import (
	"strings"
	"sync"
	"unicode"

	"github.com/pemistahl/lingua-go"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/models"
)

const (
	defaultConfidenceThreshold = 0.5
	minimumRelativeDistance    = 0.99
	maximumLanguagesInOneText  = 2
)

var (
	detector           lingua.LanguageDetector
	detectorOnce       sync.Once
	supportedLanguages map[string]bool
)

func initDetector() {
	detectorOnce.Do(func() {
		logger.Debug("Initializing language detector")

		supportedLanguages = make(map[string]bool)
		langs, err := models.GetSupportedLanguages()
		if err != nil {
			logger.Warn("Failed to get supported languages: %v, using all languages", err)
			detector = lingua.NewLanguageDetectorBuilder().
				FromAllLanguages().
				WithMinimumRelativeDistance(minimumRelativeDistance).
				WithLowAccuracyMode().
				WithPreloadedLanguageModels().
				Build()
			return
		}

		for _, lang := range langs {
			supportedLanguages[lang] = true
		}

		linguaLangs := make([]lingua.Language, 0, len(langs))
		for _, lang := range langs {
			linguaLang := bcp47ToLingua(lang)
			if linguaLang != lingua.Unknown {
				linguaLangs = append(linguaLangs, linguaLang)
			}
		}

		if len(linguaLangs) < 2 {
			logger.Warn("Not enough supported languages (%d), using all languages", len(linguaLangs))
			detector = lingua.NewLanguageDetectorBuilder().
				FromAllLanguages().
				WithLowAccuracyMode().
				WithPreloadedLanguageModels().
				Build()
		} else {
			detector = lingua.NewLanguageDetectorBuilder().
				FromLanguages(linguaLangs...).
				WithLowAccuracyMode().
				WithPreloadedLanguageModels().
				Build()
		}

		logger.Debug("Language detector initialized, %d supported languages", len(supportedLanguages))
	})
}

func bcp47ToLingua(code string) lingua.Language {
	switch code {
	case "zh-Hans", "zh":
		return lingua.Chinese
	default:
		isoCode := lingua.GetIsoCode639_1FromValue(strings.ToUpper(code))
		return lingua.GetLanguageFromIsoCode639_1(isoCode)
	}
}

func isSupportedLanguage(lang string) bool {
	if len(supportedLanguages) == 0 {
		return true
	}
	return supportedLanguages[lang]
}

func linguaToBCP47(lang lingua.Language) string {

	switch lang {
	case lingua.Chinese:

		return "zh-Hans"
	default:

		code := lang.IsoCode639_1()
		return strings.ToLower(code.String())
	}
}

func DetectLanguage(text string) string {
	if text == "" {
		return ""
	}

	initDetector()

	lang, exists := detector.DetectLanguageOf(text)
	if !exists {
		return ""
	}

	return linguaToBCP47(lang)
}

func DetectLanguageWithConfidence(text string, minConfidence float64) (string, float64) {
	if text == "" {
		return "", 0.0
	}

	initDetector()

	confidenceValues := detector.ComputeLanguageConfidenceValues(text)
	if len(confidenceValues) == 0 {
		return "", 0.0
	}

	topResult := confidenceValues[0]
	confidence := topResult.Value()

	if confidence < minConfidence {
		return "", confidence
	}

	return linguaToBCP47(topResult.Language()), confidence
}

type TextSegment struct {
	Text       string
	Language   string
	Start      int
	End        int
	Confidence float64
}

func DetectMultipleLanguages(text string) []TextSegment {
	return DetectMultipleLanguagesWithThreshold(text, defaultConfidenceThreshold)
}

func hasMixedScripts(text string) bool {
	var hasCJK, hasLatin bool
	for _, r := range text {
		if unicode.Is(unicode.Han, r) || unicode.Is(unicode.Hiragana, r) || unicode.Is(unicode.Katakana, r) || unicode.Is(unicode.Hangul, r) {
			hasCJK = true
		} else if unicode.Is(unicode.Latin, r) {
			hasLatin = true
		}
		if hasCJK && hasLatin {
			return true
		}
	}
	return false
}

func DetectMultipleLanguagesWithThreshold(text string, threshold float64) []TextSegment {
	if text == "" {
		return nil
	}

	initDetector()

	fallbackLang, _ := detector.DetectLanguageOf(text)
	fallbackBCP47 := linguaToBCP47(fallbackLang)
	if !isSupportedLanguage(fallbackBCP47) {
		fallbackBCP47 = "en"
	}

	if !hasMixedScripts(text) {
		logger.Debug("DetectMultipleLanguages: no mixed scripts, using single language: %s", fallbackBCP47)
		return []TextSegment{{
			Text:       text,
			Language:   fallbackBCP47,
			Start:      0,
			End:        len(text),
			Confidence: 1.0,
		}}
	}

	logger.Debug("DetectMultipleLanguages: mixed scripts detected, fallback=%s, threshold=%.2f", fallbackBCP47, threshold)

	results := detector.DetectMultipleLanguagesOf(text)
	if len(results) == 0 {
		logger.Debug("DetectMultipleLanguages: no segments detected")
		return nil
	}

	rawSegments := make([]TextSegment, 0, len(results))
	for i, r := range results {
		start := r.StartIndex()
		end := r.EndIndex()
		segmentText := text[start:end]
		detectedLang := linguaToBCP47(r.Language())

		var lang string
		var usedFallback bool
		if isSupportedLanguage(detectedLang) {
			lang = detectedLang
		} else {
			lang = fallbackBCP47
			usedFallback = true
		}

		logger.Debug("DetectMultipleLanguages: segment[%d] lang=%s, fallback=%v, text=%q",
			i, lang, usedFallback, segmentText)

		rawSegments = append(rawSegments, TextSegment{
			Text:       segmentText,
			Language:   lang,
			Start:      start,
			End:        end,
			Confidence: 1.0,
		})
	}

	segments := mergeAdjacentSegments(rawSegments, text)
	logger.Debug("DetectMultipleLanguages: merged %d -> %d segments", len(rawSegments), len(segments))

	segments = limitLanguages(segments, text, maximumLanguagesInOneText)

	return segments
}

func mergeAdjacentSegments(segments []TextSegment, originalText string) []TextSegment {
	if len(segments) <= 1 {
		return segments
	}

	merged := make([]TextSegment, 0, len(segments))
	current := segments[0]

	for i := 1; i < len(segments); i++ {
		next := segments[i]
		if current.Language == next.Language {
			current.Text = originalText[current.Start:next.End]
			current.End = next.End
			if next.Confidence > current.Confidence {
				current.Confidence = next.Confidence
			}
		} else {
			merged = append(merged, current)
			current = next
		}
	}
	merged = append(merged, current)

	return merged
}

func limitLanguages(segments []TextSegment, originalText string, maxLangs int) []TextSegment {
	if len(segments) <= 1 {
		return segments
	}

	langBytes := make(map[string]int)
	for _, seg := range segments {
		langBytes[seg.Language] += seg.End - seg.Start
	}

	if len(langBytes) <= maxLangs {
		return segments
	}

	type langSize struct {
		lang string
		size int
	}
	sorted := make([]langSize, 0, len(langBytes))
	for lang, size := range langBytes {
		sorted = append(sorted, langSize{lang, size})
	}
	for i := 0; i < len(sorted)-1; i++ {
		for j := i + 1; j < len(sorted); j++ {
			if sorted[j].size > sorted[i].size {
				sorted[i], sorted[j] = sorted[j], sorted[i]
			}
		}
	}

	keepLangs := make(map[string]bool)
	for i := 0; i < maxLangs && i < len(sorted); i++ {
		keepLangs[sorted[i].lang] = true
	}

	primaryLang := sorted[0].lang

	for i := range segments {
		if !keepLangs[segments[i].Language] {
			segments[i].Language = primaryLang
		}
	}

	result := mergeAdjacentSegments(segments, originalText)
	logger.Debug("limitLanguages: reduced to %d languages, %d segments", maxLangs, len(result))

	return result
}
