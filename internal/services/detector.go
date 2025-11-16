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

// initDetector 初始化语言检测器（懒加载）
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

// linguaToBCP47 将 lingua 语言代码转换为 BCP 47 格式
func linguaToBCP47(lang lingua.Language) string {
	// 特殊处理中文
	switch lang {
	case lingua.Chinese:
		// 默认返回简体中文
		return "zh-Hans"
	default:
		// 其他语言直接返回 ISO 639-1 代码（小写）
		code := lang.IsoCode639_1()
		return strings.ToLower(code.String())
	}
}

// DetectLanguage 检测文本语言并返回 BCP 47 格式的语言代码
// 如果检测失败或无法确定，返回空字符串
func DetectLanguage(text string) string {
	if text == "" {
		return ""
	}

	// 初始化检测器
	initDetector()

	// 检测语言
	lang, exists := detector.DetectLanguageOf(text)
	if !exists {
		return ""
	}

	return linguaToBCP47(lang)
}

// DetectLanguageWithConfidence 检测文本语言并返回置信度
// 返回 BCP 47 格式的语言代码和置信度（0.0-1.0）
func DetectLanguageWithConfidence(text string, minConfidence float64) (string, float64) {
	if text == "" {
		return "", 0.0
	}

	// 初始化检测器
	initDetector()

	// 获取所有语言的置信度
	confidenceValues := detector.ComputeLanguageConfidenceValues(text)
	if len(confidenceValues) == 0 {
		return "", 0.0
	}

	// 获取置信度最高的语言
	topResult := confidenceValues[0]
	confidence := topResult.Value()

	// 如果置信度低于阈值，返回空
	if confidence < minConfidence {
		return "", confidence
	}

	return linguaToBCP47(topResult.Language()), confidence
}

// NormalizeLanguageCode 标准化语言代码为 BCP 47 格式
// 支持多种输入格式：zh, zh-CN, zh_CN, Chinese 等
func NormalizeLanguageCode(code string) string {
	if code == "" {
		return ""
	}

	// 转为小写并替换下划线为连字符
	code = strings.ToLower(strings.ReplaceAll(code, "_", "-"))

	// 特殊处理中文
	switch code {
	case "zh", "zh-cn", "zh-hans", "chinese", "cmn":
		return "zh-Hans"
	case "zh-tw", "zh-hk", "zh-hant":
		return "zh-Hant"
	}

	// 如果是标准的 BCP 47 格式（如 en-US），提取主语言代码
	parts := strings.Split(code, "-")
	mainCode := parts[0]

	// 返回主语言代码
	return mainCode
}
