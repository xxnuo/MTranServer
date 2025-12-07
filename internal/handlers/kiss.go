package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
	"github.com/xxnuo/MTranServer/internal/utils"
)

type KissTranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-CN"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
}

type KissTranslateResponse struct {
	Text string `json:"text" example:"你好，世界！"`
	Src  string `json:"src" example:"en"`
}

type KissBatchTranslateRequest struct {
	From  string   `json:"from" binding:"required" example:"auto"`
	To    string   `json:"to" binding:"required" example:"zh-CN"`
	Texts []string `json:"texts" binding:"required" example:"Hello,World"`
}

type KissBatchTranslateItem struct {
	Text string `json:"text" example:"你好"`
	Src  string `json:"src" example:"en"`
}

type KissBatchTranslateResponse struct {
	Translations []KissBatchTranslateItem `json:"translations"`
}

// HandleKissTranslate 简约翻译插件接口（非聚合）
// @Summary      简约翻译插件接口（非聚合）
// @Description  为简约翻译插件提供的单文本翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        KEY      header    string                false  "API Token"
// @Param        request  body      KissTranslateRequest  true   "简约翻译请求"
// @Success      200      {object}  KissTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /kiss [post]
func HandleKissTranslate(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {

		if apiToken != "" {
			token := c.GetHeader("KEY")
			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

		var rawReq map[string]interface{}
		if err := c.ShouldBindJSON(&rawReq); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": err.Error(),
			})
			return
		}

		if texts, ok := rawReq["texts"].([]interface{}); ok && len(texts) > 0 {

			var batchReq KissBatchTranslateRequest
			batchReq.From, _ = rawReq["from"].(string)
			batchReq.To, _ = rawReq["to"].(string)
			for _, t := range texts {
				if str, ok := t.(string); ok {
					batchReq.Texts = append(batchReq.Texts, str)
				}
			}
			if batchReq.From == "" || batchReq.To == "" || len(batchReq.Texts) == 0 {
				c.JSON(http.StatusBadRequest, gin.H{
					"error": "Invalid batch request",
				})
				return
			}
			handleBatchTranslate(c, batchReq)
			return
		}

		var req KissTranslateRequest
		req.From, _ = rawReq["from"].(string)
		req.To, _ = rawReq["to"].(string)
		req.Text, _ = rawReq["text"].(string)

		if req.From == "" || req.To == "" || req.Text == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "Missing required fields: from, to, text",
			})
			return
		}

		fromLang := utils.NormalizeLanguageCode(req.From)
		toLang := utils.NormalizeLanguageCode(req.To)

		ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
		defer cancel()

		result, err := services.TranslateWithPivot(ctx, fromLang, toLang, req.Text, false)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed: %v", err),
			})
			return
		}

		c.JSON(http.StatusOK, gin.H{
			"text": result,
			"src":  req.From,
		})
	}
}

func handleBatchTranslate(c *gin.Context, req KissBatchTranslateRequest) {

	fromLang := utils.NormalizeLanguageCode(req.From)
	toLang := utils.NormalizeLanguageCode(req.To)

	ctx, cancel := context.WithTimeout(c.Request.Context(), 120*time.Second)
	defer cancel()

	translations := make([]KissBatchTranslateItem, 0, len(req.Texts))
	for _, text := range req.Texts {
		result, err := services.TranslateWithPivot(ctx, fromLang, toLang, text, false)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed: %v", err),
			})
			return
		}
		translations = append(translations, KissBatchTranslateItem{
			Text: result,
			Src:  req.From,
		})
	}

	c.JSON(http.StatusOK, KissBatchTranslateResponse{
		Translations: translations,
	})
}
