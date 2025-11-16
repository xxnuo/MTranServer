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

// googleLangToBCP47 Google 语言代码转 BCP47
// 只列出与 BCP47 不同的映射，其他直接返回原值
var googleLangToBCP47 = map[string]string{
	"zh-CN": "zh-Hans",
	"zh-TW": "zh-Hant",
	"zh-HK": "zh-Hant",
	"zh-SG": "zh-Hans",
}

// bcp47ToGoogleLang BCP47 转 Google 语言代码
// 只列出与 BCP47 不同的映射，其他直接返回原值
var bcp47ToGoogleLang = map[string]string{
	"zh-Hans": "zh-CN",
	"zh-Hant": "zh-TW",
}

// convertGoogleLangToBCP47 将 Google 语言代码转换为 BCP47
func convertGoogleLangToBCP47(googleLang string) string {
	if bcp47, ok := googleLangToBCP47[googleLang]; ok {
		return bcp47
	}
	// 如果不在映射表中，直接返回（大部分语言代码相同）
	return googleLang
}

// convertBCP47ToGoogleLang 将 BCP47 语言代码转换为 Google
func convertBCP47ToGoogleLang(bcp47Lang string) string {
	if googleLang, ok := bcp47ToGoogleLang[bcp47Lang]; ok {
		return googleLang
	}
	// 如果不在映射表中，直接返回
	return bcp47Lang
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

		// 转换 Google 语言代码到 BCP47
		sourceBCP47 := convertGoogleLangToBCP47(req.Source)
		targetBCP47 := convertGoogleLangToBCP47(req.Target)

		// 翻译
		ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
		defer cancel()

		isHTML := req.Format == "html"
		result, err := services.TranslateWithPivot(ctx, sourceBCP47, targetBCP47, req.Q, isHTML)
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

		// 转换 Google 语言代码到 BCP47
		sourceBCP47 := convertGoogleLangToBCP47(sl)
		targetBCP47 := convertGoogleLangToBCP47(tl)

		// 翻译
		ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
		defer cancel()

		result, err := services.TranslateWithPivot(ctx, sourceBCP47, targetBCP47, text, false)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed: %v", err),
			})
			return
		}

		// 返回 translate_a/single 格式的响应
		// 格式: [[["翻译结果","原文",null,null,1]],null,"检测到的源语言",null,null,null,null,[]]
		// response[0][0][0] 是翻译结果
		// response[2] 是检测到的源语言（返回 Google 格式）
		detectedLang := convertBCP47ToGoogleLang(sourceBCP47)
		response := []interface{}{
			[]interface{}{
				[]interface{}{result, text, nil, nil, 1},
			},
			nil,
			detectedLang,
			nil,
			nil,
			nil,
			nil,
			[]interface{}{},
		}

		c.JSON(http.StatusOK, response)
	}
}
