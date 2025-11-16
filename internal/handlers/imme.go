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
var immeLangToBCP47 = map[string]string{
	"auto":     "auto",
	"af":       "af",
	"am":       "am",
	"ar":       "ar",
	"az":       "az",
	"be":       "be",
	"bg":       "bg",
	"tn":       "tn",
	"bn":       "bn",
	"bs":       "bs",
	"ca":       "ca",
	"ceb":      "ceb",
	"co":       "co",
	"cs":       "cs",
	"cy":       "cy",
	"da":       "da",
	"de":       "de",
	"el":       "el",
	"en":       "en",
	"eo":       "eo",
	"es":       "es",
	"et":       "et",
	"eu":       "eu",
	"fa":       "fa",
	"fi":       "fi",
	"fil":      "fil",
	"fj":       "fj",
	"fr":       "fr",
	"fy":       "fy",
	"ga":       "ga",
	"gd":       "gd",
	"gl":       "gl",
	"gu":       "gu",
	"ha":       "ha",
	"haw":      "haw",
	"he":       "he",
	"hi":       "hi",
	"hmn":      "hmn",
	"hr":       "hr",
	"ht":       "ht",
	"hu":       "hu",
	"hy":       "hy",
	"id":       "id",
	"ig":       "ig",
	"is":       "is",
	"it":       "it",
	"ja":       "ja",
	"jw":       "jw",
	"ka":       "ka",
	"kk":       "kk",
	"km":       "km",
	"kn":       "kn",
	"ko":       "ko",
	"ku":       "ku",
	"ky":       "ky",
	"la":       "la",
	"lb":       "lb",
	"lo":       "lo",
	"lt":       "lt",
	"lv":       "lv",
	"mg":       "mg",
	"mi":       "mi",
	"mk":       "mk",
	"ml":       "ml",
	"mn":       "mn",
	"mr":       "mr",
	"ms":       "ms",
	"mt":       "mt",
	"mww":      "mww",
	"my":       "my",
	"ne":       "ne",
	"nl":       "nl",
	"no":       "no",
	"ny":       "ny",
	"otq":      "otq",
	"pa":       "pa",
	"pl":       "pl",
	"ps":       "ps",
	"pt":       "pt",
	"ro":       "ro",
	"ru":       "ru",
	"sd":       "sd",
	"si":       "si",
	"sk":       "sk",
	"sl":       "sl",
	"sm":       "sm",
	"sn":       "sn",
	"so":       "so",
	"sq":       "sq",
	"sr":       "sr",
	"sr-Cyrl":  "sr-Cyrl",
	"sr-Latn":  "sr-Latn",
	"st":       "st",
	"su":       "su",
	"sv":       "sv",
	"sw":       "sw",
	"ta":       "ta",
	"te":       "te",
	"tg":       "tg",
	"th":       "th",
	"tlh":      "tlh",
	"tlh-Qaak": "tlh-Qaak",
	"to":       "to",
	"tr":       "tr",
	"ty":       "ty",
	"ug":       "ug",
	"uk":       "uk",
	"ur":       "ur",
	"uz":       "uz",
	"vi":       "vi",
	"wyw":      "wyw",
	"xh":       "xh",
	"yi":       "yi",
	"yo":       "yo",
	"yua":      "yua",
	"yue":      "yue",
	"zh-CN":    "zh-Hans",
	"zh-TW":    "zh-Hant",
	"zu":       "zu",
}

// bcp47ToImmeLang BCP47 转沉浸式翻译语言代码
var bcp47ToImmeLang = map[string]string{
	"auto":    "auto",
	"zh-Hans": "zh-CN",
	"zh-Hant": "zh-TW",
	"zh-CN":   "zh-CN",
	"zh-TW":   "zh-TW",
	"zh-HK":   "zh-TW",
	"zh-MO":   "zh-TW",
	"zh-SG":   "zh-CN",
	"pt-BR":   "pt",
	"pt-PT":   "pt",
	"en-US":   "en",
	"en-GB":   "en",
	"es-ES":   "es",
	"es-MX":   "es",
	"fr-FR":   "fr",
	"fr-CA":   "fr",
	"de-DE":   "de",
	"de-AT":   "de",
	"de-CH":   "de",
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
