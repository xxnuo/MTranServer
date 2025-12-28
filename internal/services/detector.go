package services

import (
	"strings"
	"sync"

	"github.com/pemistahl/lingua-go"
	"github.com/xxnuo/MTranServer/internal/logger"
)

const defaultConfidenceThreshold = 0.5

var (
	detector     lingua.LanguageDetector
	detectorOnce sync.Once
)

func initDetector() {
	detectorOnce.Do(func() {
		logger.Debug("Initializing language detector")
		detector = lingua.NewLanguageDetectorBuilder().
			FromAllLanguages().
			WithMinimumRelativeDistance(0.99).
			Build()
		logger.Debug("Language detector initialized")
	})
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
	logger.Debug("DetectMultipleLanguages: fallback=%s, threshold=%.2f, text=%q", fallbackBCP47, threshold, text)

	results := detector.DetectMultipleLanguagesOf(text)
	if len(results) == 0 {
		logger.Debug("DetectMultipleLanguages: no segments detected")
		return nil
	}

	segments := make([]TextSegment, 0, len(results))
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
			if confidence >= threshold {
				lang = linguaToBCP47(confidenceValues[0].Language())
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

		segments = append(segments, TextSegment{
			Text:       segmentText,
			Language:   lang,
			Start:      start,
			End:        end,
			Confidence: confidence,
		})
	}

	return segments
}
