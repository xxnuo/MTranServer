package services

import (
	"strings"
	"sync"

	"github.com/pemistahl/lingua-go"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/models"
)

const defaultConfidenceThreshold = 0.5

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
				WithMinimumRelativeDistance(0.99).
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
	logger.Debug("DetectMultipleLanguages: fallback=%s, threshold=%.2f, text=%q", fallbackBCP47, threshold, text)

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
		originalLang := linguaToBCP47(r.Language())

		confidenceValues := detector.ComputeLanguageConfidenceValues(segmentText)
		var confidence float64
		var lang string
		var usedFallback bool
		if len(confidenceValues) > 0 {
			confidence = confidenceValues[0].Value()
			detectedLang := linguaToBCP47(confidenceValues[0].Language())
			if confidence >= threshold && isSupportedLanguage(detectedLang) {
				lang = detectedLang
			} else {
				lang = fallbackBCP47
				usedFallback = true
			}
		} else {
			lang = fallbackBCP47
			confidence = 0.0
			usedFallback = true
		}

		logger.Debug("DetectMultipleLanguages: segment[%d] original=%s, final=%s, conf=%.3f, fallback=%v, text=%q",
			i, originalLang, lang, confidence, usedFallback, segmentText)

		rawSegments = append(rawSegments, TextSegment{
			Text:       segmentText,
			Language:   lang,
			Start:      start,
			End:        end,
			Confidence: confidence,
		})
	}

	segments := mergeAdjacentSegments(rawSegments, text)
	logger.Debug("DetectMultipleLanguages: merged %d -> %d segments", len(rawSegments), len(segments))

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
