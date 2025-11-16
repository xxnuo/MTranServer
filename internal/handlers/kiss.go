package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// kissToBCP47 将 Kiss Translator 语言代码转换为 BCP 47 标准
// 只列出与 BCP47 不同的映射，其他直接返回原值
var kissToBCP47 = map[string]string{
	"zh-CN": "zh-Hans",
	"zh-TW": "zh-Hant",
}

// convertKissToBCP47 转换 Kiss 语言代码到 BCP 47
func convertKissToBCP47(kissLang string) string {
	if bcp47, ok := kissToBCP47[kissLang]; ok {
		return bcp47
	}
	return kissLang
}

// KissTranslateRequest 简约翻译请求（非聚合）
type KissTranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-CN"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
}

// KissTranslateResponse 简约翻译响应（非聚合）
type KissTranslateResponse struct {
	Text string `json:"text" example:"你好，世界！"`
	Src  string `json:"src" example:"en"`
}

// KissBatchTranslateRequest 简约翻译请求（聚合）
type KissBatchTranslateRequest struct {
	From  string   `json:"from" binding:"required" example:"auto"`
	To    string   `json:"to" binding:"required" example:"zh-CN"`
	Texts []string `json:"texts" binding:"required" example:"Hello,World"`
}

// KissBatchTranslateItem 聚合翻译单项响应
type KissBatchTranslateItem struct {
	Text string `json:"text" example:"你好"`
	Src  string `json:"src" example:"en"`
}

// KissBatchTranslateResponse 简约翻译响应（聚合，v2.0.4+格式）
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
		// 检查 token
		if apiToken != "" {
			token := c.GetHeader("KEY")
			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

		// 解析请求体为通用 map 以判断类型
		var rawReq map[string]interface{}
		if err := c.ShouldBindJSON(&rawReq); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": err.Error(),
			})
			return
		}

		// 判断是批量请求还是单个请求
		if texts, ok := rawReq["texts"].([]interface{}); ok && len(texts) > 0 {
			// 批量请求
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

		// 单个请求
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

		// 转换 Kiss 语言代码到 BCP 47
		fromLang := convertKissToBCP47(req.From)
		toLang := convertKissToBCP47(req.To)

		// 获取或创建翻译引擎
		m, err := services.GetOrCreateEngine(fromLang, toLang)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Failed to get engine: %v", err),
			})
			return
		}

		// 翻译
		ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
		defer cancel()

		result, err := m.Translate(ctx, req.Text)
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

// handleBatchTranslate 处理批量翻译请求
func handleBatchTranslate(c *gin.Context, req KissBatchTranslateRequest) {
	// 转换 Kiss 语言代码到 BCP 47
	fromLang := convertKissToBCP47(req.From)
	toLang := convertKissToBCP47(req.To)

	// 获取或创建翻译引擎
	m, err := services.GetOrCreateEngine(fromLang, toLang)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译所有文本
	ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
	defer cancel()

	translations := make([]KissBatchTranslateItem, 0, len(req.Texts))
	for _, text := range req.Texts {
		result, err := m.Translate(ctx, text)
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
