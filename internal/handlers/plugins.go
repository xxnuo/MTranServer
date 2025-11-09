package handlers

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/services"
)

// ImmeTranslateRequest 沉浸式翻译请求
type ImmeTranslateRequest struct {
	From  string   `json:"from" binding:"required" example:"en"`
	To    string   `json:"to" binding:"required" example:"zh-Hans"`
	Trans []string `json:"trans" binding:"required" example:"Hello, world!,Good morning!"`
}

// ImmeTranslateResponse 沉浸式翻译响应
type ImmeTranslateResponse struct {
	Trans []string `json:"trans" example:"你好，世界！,早上好！"`
}

// handleImmeTranslate 沉浸式翻译插件接口
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

		// 获取或创建翻译引擎
		m, err := services.GetOrCreateEngine(req.From, req.To)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Failed to get engine: %v", err),
			})
			return
		}

		// 批量翻译
		results := make([]string, len(req.Trans))
		ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
		defer cancel()

		for i, text := range req.Trans {
			result, err := m.Translate(ctx, text)
			if err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
				})
				return
			}
			results[i] = result
		}

		c.JSON(http.StatusOK, gin.H{
			"trans": results,
		})
	}
}

// KissTranslateRequest 简约翻译请求
type KissTranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-Hans"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
}

// KissTranslateResponse 简约翻译响应
type KissTranslateResponse struct {
	Text string `json:"text" example:"你好，世界！"`
}

// handleKissTranslate 简约翻译插件接口
// @Summary      简约翻译插件接口
// @Description  为简约翻译插件提供的翻译接口
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

		var req KissTranslateRequest

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

		result, err := m.Translate(ctx, req.Text)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed: %v", err),
			})
			return
		}

		c.JSON(http.StatusOK, gin.H{
			"text": result,
		})
	}
}
