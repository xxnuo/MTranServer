package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// GoogleTranslateRequest Google 翻译兼容请求
type GoogleTranslateRequest struct {
	Q      string `json:"q" binding:"required" example:"The Great Pyramid of Giza"`
	Source string `json:"source" binding:"required" example:"en"`
	Target string `json:"target" binding:"required" example:"zh-Hans"`
	Format string `json:"format" example:"text"`
}

// GoogleTranslateResponse Google 翻译兼容响应
type GoogleTranslateResponse struct {
	Data struct {
		Translations []struct {
			TranslatedText string `json:"translatedText" example:"吉萨大金字塔"`
		} `json:"translations"`
	} `json:"data"`
}

// HandleGoogleCompatTranslate Google 翻译兼容接口
// @Summary      Google 翻译兼容接口
// @Description  兼容 Google Translate API v2 的翻译接口
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      GoogleTranslateRequest  true  "Google 翻译请求"
// @Success      200      {object}  GoogleTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /language/translate/v2 [post]
func HandleGoogleCompatTranslate(c *gin.Context) {
	var req GoogleTranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := services.GetOrCreateEngine(req.Source, req.Target)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()

	isHTML := req.Format == "html"
	var result string
	if isHTML {
		result, err = m.TranslateHTML(ctx, req.Q)
	} else {
		result, err = m.Translate(ctx, req.Q)
	}

	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"data": gin.H{
			"translations": []gin.H{
				{
					"translatedText": result,
				},
			},
		},
	})
}
