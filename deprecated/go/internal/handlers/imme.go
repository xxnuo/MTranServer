package handlers

import (
	"context"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/services"
	"github.com/xxnuo/MTranServer/internal/utils"
)

type ImmeTranslateRequest struct {
	SourceLang string   `json:"source_lang" binding:"required" example:"en"`
	TargetLang string   `json:"target_lang" binding:"required" example:"zh-CN"`
	TextList   []string `json:"text_list" binding:"required" example:"Hello, world!,Good morning!"`
}

type ImmeTranslation struct {
	DetectedSourceLang string `json:"detected_source_lang" example:"en"`
	Text               string `json:"text" example:"你好，世界！"`
}

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

		sourceLang := utils.NormalizeLanguageCode(req.SourceLang)
		targetLang := utils.NormalizeLanguageCode(req.TargetLang)

		translations := make([]ImmeTranslation, len(req.TextList))
		ctx, cancel := context.WithTimeout(c.Request.Context(), 120*time.Second)
		defer cancel()

		logger.Debug("Imme request: %s -> %s, count: %d", sourceLang, targetLang, len(req.TextList))
		for i, text := range req.TextList {
			logger.Debug("Imme translating [%d/%d]: %s -> %s, text length: %d, text: %q", i+1, len(req.TextList), sourceLang, targetLang, len(text), text)
			result, err := services.TranslateWithPivot(ctx, sourceLang, targetLang, text, true)
			if err != nil {
				logger.Error("Imme translation failed at index %d (%s -> %s): %v", i, sourceLang, targetLang, err)
				result = text // Fallback to original text
			} else {
				logger.Debug("Imme translated [%d/%d] success", i+1, len(req.TextList))
			}

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
