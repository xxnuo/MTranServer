package handlers

import (
	"context"
	"fmt"
	"net/http"
	"strings"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// deeplLangToBCP47 DeepL 语言代码转 BCP47
// 只列出与 BCP47 不同的映射（DeepL 使用大写和特殊代码）
var deeplLangToBCP47 = map[string]string{
	"NB":    "no",
	"ZH":    "zh-Hans",
	"ZH-CN": "zh-Hans",
	"ZH-TW": "zh-Hant",
}

// bcp47ToDeeplLang BCP47 转 DeepL 语言代码
// 只列出与 BCP47 不同的映射（DeepL 使用大写和特殊代码）
var bcp47ToDeeplLang = map[string]string{
	"no":      "NB",
	"zh-Hans": "ZH",
	"zh-CN":   "ZH-CN",
	"zh-Hant": "ZH-TW",
	"zh-TW":   "ZH-TW",
}

// convertDeeplLangToBCP47 将 DeepL 语言代码转换为 BCP47
func convertDeeplLangToBCP47(deeplLang string) string {
	// 转换为大写进行匹配
	upperLang := strings.ToUpper(deeplLang)
	if bcp47, ok := deeplLangToBCP47[upperLang]; ok {
		return bcp47
	}
	// 如果不在映射表中，返回小写版本
	return strings.ToLower(deeplLang)
}

// convertBCP47ToDeeplLang 将 BCP47 语言代码转换为 DeepL
func convertBCP47ToDeeplLang(bcp47Lang string) string {
	if deeplLang, ok := bcp47ToDeeplLang[bcp47Lang]; ok {
		return deeplLang
	}
	// 如果不在映射表中，返回大写版本
	return strings.ToUpper(bcp47Lang)
}

// DeeplTranslateRequest DeepL 翻译请求
type DeeplTranslateRequest struct {
	Text                []string `json:"text" binding:"required" example:"Hello, world!"`
	SourceLang          string   `json:"source_lang,omitempty" example:"EN"`
	TargetLang          string   `json:"target_lang" binding:"required" example:"DE"`
	SplitSentences      string   `json:"split_sentences,omitempty" example:"1"`
	PreserveFormatting  string   `json:"preserve_formatting,omitempty" example:"0"`
	Formality           string   `json:"formality,omitempty" example:"default"`
	GlossaryID          string   `json:"glossary_id,omitempty"`
	TagHandling         string   `json:"tag_handling,omitempty" example:"xml"`
	OutlineDetection    string   `json:"outline_detection,omitempty" example:"1"`
	NonSplittingTags    []string `json:"non_splitting_tags,omitempty"`
	SplittingTags       []string `json:"splitting_tags,omitempty"`
	IgnoreTags          []string `json:"ignore_tags,omitempty"`
	ModelType           string   `json:"model_type,omitempty" example:"quality_optimized"`
	Context             string   `json:"context,omitempty"`
	EnableBetaLanguages bool     `json:"enable_beta_languages,omitempty"`
}

// DeeplTranslation 翻译结果
type DeeplTranslation struct {
	DetectedSourceLanguage string `json:"detected_source_language" example:"EN"`
	Text                   string `json:"text" example:"Hallo, Welt!"`
}

// DeeplTranslateResponse DeepL 翻译响应
type DeeplTranslateResponse struct {
	Translations []DeeplTranslation `json:"translations"`
}

// HandleDeeplTranslate DeepL 翻译兼容接口
// @Summary      DeepL 翻译兼容接口
// @Description  兼容 DeepL API v2 的翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        token    query     string                  false  "API Token"
// @Param        request  body      DeeplTranslateRequest   true   "DeepL 翻译请求"
// @Success      200      {object}  DeeplTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /deepl [post]
func HandleDeeplTranslate(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 检查 token - 兼容 DeepL API 认证方式
		if apiToken != "" {
			// 支持 DeepL 标准认证: Authorization: DeepL-Auth-Key [key]
			authHeader := c.GetHeader("Authorization")
			token := ""

			if strings.HasPrefix(authHeader, "DeepL-Auth-Key ") {
				token = strings.TrimPrefix(authHeader, "DeepL-Auth-Key ")
			} else if authHeader != "" {
				// 也支持标准 Bearer token
				token = strings.TrimPrefix(authHeader, "Bearer ")
			} else {
				// 兼容 query 参数方式
				token = c.Query("token")
			}

			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}
		var req DeeplTranslateRequest

		if err := c.ShouldBindJSON(&req); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": err.Error(),
			})
			return
		}

		// 转换语言代码：DeepL -> BCP47
		sourceLang := "auto"
		if req.SourceLang != "" {
			sourceLang = convertDeeplLangToBCP47(req.SourceLang)
		}
		targetLang := convertDeeplLangToBCP47(req.TargetLang)

		// 批量翻译
		translations := make([]DeeplTranslation, len(req.Text))
		ctx, cancel := context.WithTimeout(c.Request.Context(), 120*time.Second)
		defer cancel()

		// 确定是否需要 HTML 处理
		isHTML := req.TagHandling == "html" || req.TagHandling == "xml"

		for i, text := range req.Text {
			result, err := services.TranslateWithPivot(ctx, sourceLang, targetLang, text, isHTML)
			if err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
				})
				return
			}

			// 返回 DeepL 格式的语言代码
			detectedLang := req.SourceLang
			if detectedLang == "" {
				detectedLang = convertBCP47ToDeeplLang(sourceLang)
			}

			translations[i] = DeeplTranslation{
				DetectedSourceLanguage: detectedLang,
				Text:                   result,
			}
		}

		c.JSON(http.StatusOK, DeeplTranslateResponse{
			Translations: translations,
		})
	}
}
