package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/services"
	"github.com/xxnuo/MTranServer/internal/utils"
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

	req.From = utils.NormalizeLanguageCode(req.From)
	req.To = utils.NormalizeLanguageCode(req.To)

	logger.Debug("Translation request: %s -> %s, text length: %d", req.From, req.To, len(req.Text))
	ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
	defer cancel()

	result, err := services.TranslateWithPivot(ctx, req.From, req.To, req.Text, req.HTML)
	if err != nil {
		logger.Error("Translation failed (%s -> %s): %v", req.From, req.To, err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	logger.Debug("Translation completed: %s -> %s", req.From, req.To)
	c.JSON(http.StatusOK, gin.H{
		"result": result,
	})
}

type TranslateBatchRequest struct {
	From  string   `json:"from" binding:"required" example:"en"`
	To    string   `json:"to" binding:"required" example:"zh-Hans"`
	Texts []string `json:"texts" binding:"required" example:"Hello, world!,Good morning!"`
	HTML  bool     `json:"html" example:"false"`
}

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

	req.From = utils.NormalizeLanguageCode(req.From)
	req.To = utils.NormalizeLanguageCode(req.To)

	logger.Debug("Batch translation request: %s -> %s, count: %d", req.From, req.To, len(req.Texts))
	results := make([]string, len(req.Texts))
	ctx, cancel := context.WithTimeout(c.Request.Context(), 120*time.Second)
	defer cancel()

	for i, text := range req.Texts {
		result, err := services.TranslateWithPivot(ctx, req.From, req.To, text, req.HTML)
		if err != nil {
			logger.Error("Batch translation failed at index %d (%s -> %s): %v", i, req.From, req.To, err)
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
			})
			return
		}
		results[i] = result
	}

	logger.Debug("Batch translation completed: %s -> %s, count: %d", req.From, req.To, len(req.Texts))
	c.JSON(http.StatusOK, gin.H{
		"results": results,
	})
}
