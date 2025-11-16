package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// immeLangToBCP47 沉浸式翻译语言代码转 BCP47
// 只列出与 BCP47 不同的映射，其他直接返回原值
var immeLangToBCP47 = map[string]string{
	"zh-CN": "zh-Hans",
	"zh-TW": "zh-Hant",
}

// convertImmeLangToBCP47 将沉浸式翻译语言代码转换为 BCP47
func convertImmeLangToBCP47(immeLang string) string {
	// 直接从映射表转换
	if bcp47, ok := immeLangToBCP47[immeLang]; ok {
		return bcp47
	}

	// 如果不在映射表中，返回原值
	return immeLang
}

// ImmeTranslateRequest 沉浸式翻译请求
type ImmeTranslateRequest struct {
	SourceLang string   `json:"source_lang" binding:"required" example:"en"`
	TargetLang string   `json:"target_lang" binding:"required" example:"zh-CN"`
	TextList   []string `json:"text_list" binding:"required" example:"Hello, world!,Good morning!"`
}

// ImmeTranslation 翻译结果
type ImmeTranslation struct {
	DetectedSourceLang string `json:"detected_source_lang" example:"en"`
	Text               string `json:"text" example:"你好，世界！"`
}

// ImmeTranslateResponse 沉浸式翻译响应
type ImmeTranslateResponse struct {
	Translations []ImmeTranslation `json:"translations"`
}

// HandleImmeTranslate 沉浸式翻译插件接口
// @Summary      沉浸式翻译插件接口
// @Description  为沉浸式翻译插件提供的翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        token    query     string                  false  "API Token"
// @Param        request  body      ImmeTranslateRequest    true   "沉浸式翻译请求"
// @Success      200      {object}  ImmeTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /imme [post]
func HandleImmeTranslate(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 检查 token
		if apiToken != "" {
			token := c.Query("token")
			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

		var req ImmeTranslateRequest

		if err := c.ShouldBindJSON(&req); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": err.Error(),
			})
			return
		}

		// 转换语言代码：沉浸式翻译 -> BCP47
		sourceLang := convertImmeLangToBCP47(req.SourceLang)
		targetLang := convertImmeLangToBCP47(req.TargetLang)

		// 获取或创建翻译引擎
		m, err := services.GetOrCreateEngine(sourceLang, targetLang)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Failed to get engine: %v", err),
			})
			return
		}

		// 批量翻译
		translations := make([]ImmeTranslation, len(req.TextList))
		ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
		defer cancel()

		for i, text := range req.TextList {
			result, err := m.Translate(ctx, text)
			if err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
				})
				return
			}
			// 返回沉浸式翻译格式的语言代码
			translations[i] = ImmeTranslation{
				DetectedSourceLang: req.SourceLang,
				Text:               result,
			}
		}

		c.JSON(http.StatusOK, ImmeTranslateResponse{
			Translations: translations,
		})
	}
}
