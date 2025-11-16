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
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        key      query     string                  false  "API Key"
// @Param        request  body      GoogleTranslateRequest  true   "Google 翻译请求"
// @Success      200      {object}  GoogleTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /google/language/translate/v2 [post]
func HandleGoogleCompatTranslate(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 检查 token - 兼容 Google API 认证方式
		if apiToken != "" {
			// 支持 Google API 标准认证: ?key=xxx
			token := c.Query("key")

			// 也支持标准 Authorization header
			if token == "" {
				authHeader := c.GetHeader("Authorization")
				if strings.HasPrefix(authHeader, "Bearer ") {
					token = strings.TrimPrefix(authHeader, "Bearer ")
				} else if authHeader != "" {
					token = authHeader
				}
			}

			// 兼容通用 token 参数
			if token == "" {
				token = c.Query("token")
			}

			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

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
}

// HandleGoogleTranslateSingle Google translate_a/single 兼容接口
// @Summary      Google translate_a/single 兼容接口
// @Description  兼容 Google translate_a/single 的翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        client  query     string  false  "客户端类型"  default(gtx)
// @Param        sl      query     string  true   "源语言代码"  example(en)
// @Param        tl      query     string  true   "目标语言代码"  example(zh-CN)
// @Param        dt      query     string  false  "数据类型"  default(t)
// @Param        q       query     string  true   "待翻译文本"  example(Hello, world!)
// @Param        key     query     string  false  "API Key"
// @Success      200     {array}   interface{}
// @Failure      400     {object}  map[string]string
// @Failure      401     {object}  map[string]string
// @Failure      500     {object}  map[string]string
// @Router       /google/translate_a/single [get]
func HandleGoogleTranslateSingle(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 检查 token
		if apiToken != "" {
			token := c.Query("key")

			if token == "" {
				authHeader := c.GetHeader("Authorization")
				if strings.HasPrefix(authHeader, "Bearer ") {
					token = strings.TrimPrefix(authHeader, "Bearer ")
				} else if authHeader != "" {
					token = authHeader
				}
			}

			if token == "" {
				token = c.Query("token")
			}

			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

		// 获取参数
		sl := c.Query("sl")
		tl := c.Query("tl")
		q := c.Query("q")

		if tl == "" || q == "" {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "Missing required parameters: tl, q",
			})
			return
		}

		// 支持 auto 自动检测源语言
		if sl == "" {
			sl = "auto"
		}

		// q 参数已经由 Gin 自动进行了 URL 解码
		text := q

		// 获取或创建翻译引擎
		m, err := services.GetOrCreateEngine(sl, tl)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Failed to get engine: %v", err),
			})
			return
		}

		// 翻译
		ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
		defer cancel()

		result, err := m.Translate(ctx, text)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed: %v", err),
			})
			return
		}

		// 返回 translate_a/single 格式的响应
		// 格式: [[["翻译结果","原文",null,null,1]],null,"检测到的源语言",null,null,null,null,[]]
		// response[0][0][0] 是翻译结果
		// response[2] 是检测到的源语言
		response := []interface{}{
			[]interface{}{
				[]interface{}{result, text, nil, nil, 1},
			},
			nil,
			sl,
			nil,
			nil,
			nil,
			nil,
			[]interface{}{},
		}

		c.JSON(http.StatusOK, response)
	}
}
