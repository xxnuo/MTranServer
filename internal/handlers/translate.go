package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// TranslateRequest 翻译请求
type TranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-Hans"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
	HTML bool   `json:"html" example:"false"`
}

// TranslateResponse 翻译响应
type TranslateResponse struct {
	Result string `json:"result" example:"你好，世界！"`
}

// handleTranslate 单文本翻译
// @Summary      单文本翻译
// @Description  翻译单个文本
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      TranslateRequest  true  "翻译请求"
// @Success      200      {object}  TranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /translate [post]
func HandleTranslate(c *gin.Context) {
	var req TranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := services.GetOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()

	var result string
	if req.HTML {
		result, err = m.TranslateHTML(ctx, req.Text)
	} else {
		result, err = m.Translate(ctx, req.Text)
	}

	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"result": result,
	})
}

// TranslateBatchRequest 批量翻译请求
type TranslateBatchRequest struct {
	From  string   `json:"from" binding:"required" example:"en"`
	To    string   `json:"to" binding:"required" example:"zh-Hans"`
	Texts []string `json:"texts" binding:"required" example:"Hello, world!,Good morning!"`
	HTML  bool     `json:"html" example:"false"`
}

// TranslateBatchResponse 批量翻译响应
type TranslateBatchResponse struct {
	Results []string `json:"results" example:"你好，世界！,早上好！"`
}

// handleTranslateBatch 批量翻译
// @Summary      批量翻译
// @Description  批量翻译多个文本
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      TranslateBatchRequest  true  "批量翻译请求"
// @Success      200      {object}  TranslateBatchResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /translate/batch [post]
func HandleTranslateBatch(c *gin.Context) {
	var req TranslateBatchRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := services.GetOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 批量翻译
	results := make([]string, len(req.Texts))
	ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
	defer cancel()

	for i, text := range req.Texts {
		var result string
		if req.HTML {
			result, err = m.TranslateHTML(ctx, text)
		} else {
			result, err = m.Translate(ctx, text)
		}

		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
			})
			return
		}
		results[i] = result
	}

	c.JSON(http.StatusOK, gin.H{
		"results": results,
	})
}

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

// handleGoogleCompatTranslate Google 翻译兼容接口
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
