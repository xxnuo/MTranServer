package services

import (
	"strings"
	"sync"

	"github.com/pemistahl/lingua-go"
	"github.com/xxnuo/MTranServer/internal/logger"
)

var (
	detector     lingua.LanguageDetector
	detectorOnce sync.Once
)

func initDetector() {
	detectorOnce.Do(func() {
		logger.Debug("Initializing language detector")
		detector = lingua.NewLanguageDetectorBuilder().
			FromAllLanguages().
			WithPreloadedLanguageModels().
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

func NormalizeLanguageCode(code string) string {
	if code == "" {
		return ""
	}

	code = strings.ToLower(strings.ReplaceAll(code, "_", "-"))

	switch code {
	case "zh", "zh-cn", "zh-hans", "chinese", "cmn":
		return "zh-Hans"
	case "zh-tw", "zh-hk", "zh-hant":
		return "zh-Hant"
	}

	parts := strings.Split(code, "-")
	mainCode := parts[0]

	return mainCode
}
