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

// 划词翻译语言名称到 BCP47 的映射
var hcfyLangToBCP47 = map[string]string{
	"中文(简体)": "zh-Hans",
	"中文(繁体)": "zh-Hant",
	"英语":     "en",
	"日语":     "ja",
	"韩语":     "ko",
	"法语":     "fr",
	"德语":     "de",
	"西班牙语":   "es",
	"俄语":     "ru",
	"意大利语":   "it",
	"葡萄牙语":   "pt",
	"阿拉伯语":   "ar",
	"荷兰语":    "nl",
	"波兰语":    "pl",
	"土耳其语":   "tr",
	"泰语":     "th",
	"越南语":    "vi",
	"印尼语":    "id",
	"马来语":    "ms",
	"希腊语":    "el",
	"捷克语":    "cs",
	"丹麦语":    "da",
	"芬兰语":    "fi",
	"匈牙利语":   "hu",
	"挪威语":    "no",
	"罗马尼亚语":  "ro",
	"瑞典语":    "sv",
	"乌克兰语":   "uk",
	"保加利亚语":  "bg",
	"爱沙尼亚语":  "et",
	"拉脱维亚语":  "lv",
	"立陶宛语":   "lt",
	"斯洛伐克语":  "sk",
	"斯洛文尼亚语": "sl",
	"印地语":    "hi",
	"孟加拉语":   "bn",
	"旁遮普语":   "pa",
	"泰米尔语":   "ta",
	"泰卢固语":   "te",
	"马拉地语":   "mr",
	"古吉拉特语":  "gu",
	"卡纳达语":   "kn",
	"马拉雅拉姆语": "ml",
	"僧伽罗语":   "si",
	"尼泊尔语":   "ne",
	"缅甸语":    "my",
	"高棉语":    "km",
	"老挝语":    "lo",
	"波斯语":    "fa",
	"希伯来语":   "he",
	"乌尔都语":   "ur",
	"斯瓦希里语":  "sw",
	"南非荷兰语":  "af",
	"冰岛语":    "is",
	"塞尔维亚语":  "sr",
	"克罗地亚语":  "hr",
	"波斯尼亚语":  "bs",
	"马其顿语":   "mk",
	"阿尔巴尼亚语": "sq",
	"亚美尼亚语":  "hy",
	"格鲁吉亚语":  "ka",
	"阿塞拜疆语":  "az",
	"哈萨克语":   "kk",
	"乌兹别克语":  "uz",
	"蒙古语":    "mn",
	"藏语":     "bo",
	"维吾尔语":   "ug",
	"菲律宾语":   "fil",
	"世界语":    "eo",
	"拉丁语":    "la",
	"加泰罗尼亚语": "ca",
	"巴斯克语":   "eu",
	"加利西亚语":  "gl",
	"威尔士语":   "cy",
	"爱尔兰语":   "ga",
	"苏格兰盖尔语": "gd",
	"马耳他语":   "mt",
	"卢森堡语":   "lb",
	"弗里西语":   "fy",
	"白俄罗斯语":  "be",
	"塔吉克语":   "tg",
	"吉尔吉斯语":  "ky",
	"土库曼语":   "tk",
	"普什图语":   "ps",
	"库尔德语":   "ku",
	"信德语":    "sd",
	"宿务语":    "ceb",
	"伊博语":    "ig",
	"约鲁巴语":   "yo",
	"祖鲁语":    "zu",
	"科萨语":    "xh",
	"索马里语":   "so",
	"豪萨语":    "ha",
	"阿姆哈拉语":  "am",
	"奥里亚语":   "or",
	"阿萨姆语":   "as",
	"迈蒂利语":   "mai",
	"桑塔利语":   "sat",
	"梵语":     "sa",
	"克什米尔语":  "ks",
	"多格拉语":   "doi",
	"孔卡尼语":   "kok",
	"曼尼普尔语":  "mni",
	"博多语":    "brx",
}

// bcp47ToHcfyLang BCP47 到划词翻译语言名称的映射
var bcp47ToHcfyLang = map[string]string{
	"zh-Hans": "中文(简体)",
	"zh-CN":   "中文(简体)",
	"zh-Hant": "中文(繁体)",
	"zh-TW":   "中文(繁体)",
	"en":      "英语",
	"ja":      "日语",
	"ko":      "韩语",
	"fr":      "法语",
	"de":      "德语",
	"es":      "西班牙语",
	"ru":      "俄语",
	"it":      "意大利语",
	"pt":      "葡萄牙语",
	"ar":      "阿拉伯语",
	"nl":      "荷兰语",
	"pl":      "波兰语",
	"tr":      "土耳其语",
	"th":      "泰语",
	"vi":      "越南语",
	"id":      "印尼语",
	"ms":      "马来语",
	"el":      "希腊语",
	"cs":      "捷克语",
	"da":      "丹麦语",
	"fi":      "芬兰语",
	"hu":      "匈牙利语",
	"no":      "挪威语",
	"ro":      "罗马尼亚语",
	"sv":      "瑞典语",
	"uk":      "乌克兰语",
	"bg":      "保加利亚语",
	"et":      "爱沙尼亚语",
	"lv":      "拉脱维亚语",
	"lt":      "立陶宛语",
	"sk":      "斯洛伐克语",
	"sl":      "斯洛文尼亚语",
	"hi":      "印地语",
	"bn":      "孟加拉语",
	"pa":      "旁遮普语",
	"ta":      "泰米尔语",
	"te":      "泰卢固语",
	"mr":      "马拉地语",
	"gu":      "古吉拉特语",
	"kn":      "卡纳达语",
	"ml":      "马拉雅拉姆语",
	"si":      "僧伽罗语",
	"ne":      "尼泊尔语",
	"my":      "缅甸语",
	"km":      "高棉语",
	"lo":      "老挝语",
	"fa":      "波斯语",
	"he":      "希伯来语",
	"ur":      "乌尔都语",
	"sw":      "斯瓦希里语",
	"af":      "南非荷兰语",
	"is":      "冰岛语",
	"sr":      "塞尔维亚语",
	"hr":      "克罗地亚语",
	"bs":      "波斯尼亚语",
	"mk":      "马其顿语",
	"sq":      "阿尔巴尼亚语",
	"hy":      "亚美尼亚语",
	"ka":      "格鲁吉亚语",
	"az":      "阿塞拜疆语",
	"kk":      "哈萨克语",
	"uz":      "乌兹别克语",
	"mn":      "蒙古语",
	"bo":      "藏语",
	"ug":      "维吾尔语",
	"fil":     "菲律宾语",
	"eo":      "世界语",
	"la":      "拉丁语",
	"ca":      "加泰罗尼亚语",
	"eu":      "巴斯克语",
	"gl":      "加利西亚语",
	"cy":      "威尔士语",
	"ga":      "爱尔兰语",
	"gd":      "苏格兰盖尔语",
	"mt":      "马耳他语",
	"lb":      "卢森堡语",
	"fy":      "弗里西语",
	"be":      "白俄罗斯语",
	"tg":      "塔吉克语",
	"ky":      "吉尔吉斯语",
	"tk":      "土库曼语",
	"ps":      "普什图语",
	"ku":      "库尔德语",
	"sd":      "信德语",
	"ceb":     "宿务语",
	"ig":      "伊博语",
	"yo":      "约鲁巴语",
	"zu":      "祖鲁语",
	"xh":      "科萨语",
	"so":      "索马里语",
	"ha":      "豪萨语",
	"am":      "阿姆哈拉语",
	"or":      "奥里亚语",
	"as":      "阿萨姆语",
	"mai":     "迈蒂利语",
	"sat":     "桑塔利语",
	"sa":      "梵语",
	"ks":      "克什米尔语",
	"doi":     "多格拉语",
	"kok":     "孔卡尼语",
	"mni":     "曼尼普尔语",
	"brx":     "博多语",
}

// convertHcfyLangToBCP47 将划词翻译语言名称转换为 BCP47
func convertHcfyLangToBCP47(hcfyLang string) string {
	if bcp47, ok := hcfyLangToBCP47[hcfyLang]; ok {
		return bcp47
	}
	// 如果不在映射表中，尝试直接返回（可能已经是 BCP47）
	return hcfyLang
}

// convertBCP47ToHcfyLang 将 BCP47 语言代码转换为划词翻译语言名称
func convertBCP47ToHcfyLang(bcp47Lang string) string {
	if hcfyLang, ok := bcp47ToHcfyLang[bcp47Lang]; ok {
		return hcfyLang
	}
	// 如果不在映射表中，返回原值
	return bcp47Lang
}

// HcfyTranslateRequest 划词翻译请求
type HcfyTranslateRequest struct {
	Name        string   `json:"name" binding:"required" example:"翻译一"`
	Text        string   `json:"text" binding:"required" example:"Hello, word translation."`
	Destination []string `json:"destination" binding:"required" example:"中文(简体),英语"`
	Source      string   `json:"source" example:"英语"`
}

// HcfyPhonetic 音标
type HcfyPhonetic struct {
	Name   string `json:"name,omitempty" example:"美"`
	TtsURI string `json:"ttsURI,omitempty" example:"https://..."`
	Value  string `json:"value,omitempty" example:"həˈloʊ"`
}

// HcfyDict 词典释义
type HcfyDict struct {
	Pos   string   `json:"pos,omitempty" example:"n."`
	Terms []string `json:"terms" example:"你好,问候"`
}

// HcfyTranslateResponse 划词翻译响应
type HcfyTranslateResponse struct {
	Text     string         `json:"text" example:"Hello, word translation."`
	From     string         `json:"from" example:"英语"`
	To       string         `json:"to" example:"中文(简体)"`
	TtsURI   string         `json:"ttsURI,omitempty" example:"https://..."`
	Link     string         `json:"link,omitempty" example:"https://..."`
	Phonetic []HcfyPhonetic `json:"phonetic,omitempty"`
	Dict     []HcfyDict     `json:"dict,omitempty"`
	Result   []string       `json:"result,omitempty" example:"你好，划词翻译。"`
}

// HandleHcfyTranslate 划词翻译兼容接口
// @Summary      划词翻译兼容接口
// @Description  兼容划词翻译自定义翻译源接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        token    query     string                false  "API Token"
// @Param        request  body      HcfyTranslateRequest  true   "划词翻译请求"
// @Success      200      {object}  HcfyTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /hcfy [post]
func HandleHcfyTranslate(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 检查 token
		if apiToken != "" {
			token := c.Query("token")
			if token == "" {
				authHeader := c.GetHeader("Authorization")
				if strings.HasPrefix(authHeader, "Bearer ") {
					token = strings.TrimPrefix(authHeader, "Bearer ")
				} else if authHeader != "" {
					token = authHeader
				}
			}

			if token != apiToken {
				c.JSON(http.StatusUnauthorized, gin.H{
					"error": "Unauthorized",
				})
				return
			}
		}

		var req HcfyTranslateRequest

		if err := c.ShouldBindJSON(&req); err != nil {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": err.Error(),
			})
			return
		}

		// 转换语言代码：划词翻译语言名称 -> BCP47
		sourceLang := "auto"
		if req.Source != "" {
			sourceLang = convertHcfyLangToBCP47(req.Source)
		}

		// 确定目标语种
		// destination 是一个数组，首要目标语种是第一个元素
		// 如果源语种与首要目标语种相同，则使用次要目标语种
		if len(req.Destination) == 0 {
			c.JSON(http.StatusBadRequest, gin.H{
				"error": "destination is required",
			})
			return
		}

		targetLangName := req.Destination[0]
		targetLang := convertHcfyLangToBCP47(targetLangName)

		// 检测源文本语种（简单判断）
		detectedSourceLang := sourceLang
		if sourceLang == "auto" {
			// 简单的语种检测逻辑
			if containsChinese(req.Text) {
				detectedSourceLang = "zh-Hans"
			} else if containsJapanese(req.Text) {
				detectedSourceLang = "ja"
			} else if containsKorean(req.Text) {
				detectedSourceLang = "ko"
			} else {
				detectedSourceLang = "en"
			}
		}

		// 如果检测到的源语种与目标语种相同，且有次要目标语种，则使用次要目标语种
		if detectedSourceLang == targetLang && len(req.Destination) > 1 {
			targetLangName = req.Destination[1]
			targetLang = convertHcfyLangToBCP47(targetLangName)
		}

		// 获取或创建翻译引擎
		m, err := services.GetOrCreateEngine(detectedSourceLang, targetLang)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Failed to get engine: %v", err),
			})
			return
		}

		// 翻译
		ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
		defer cancel()

		// 将文本按段落分割
		paragraphs := strings.Split(req.Text, "\n")
		results := make([]string, len(paragraphs))

		for i, paragraph := range paragraphs {
			if paragraph == "" {
				results[i] = ""
				continue
			}

			result, err := m.Translate(ctx, paragraph)
			if err != nil {
				c.JSON(http.StatusInternalServerError, gin.H{
					"error": fmt.Sprintf("Translation failed at paragraph %d: %v", i, err),
				})
				return
			}
			results[i] = result
		}

		// 构造响应
		response := HcfyTranslateResponse{
			Text:   req.Text,
			From:   convertBCP47ToHcfyLang(detectedSourceLang),
			To:     targetLangName,
			Result: results,
		}

		c.JSON(http.StatusOK, response)
	}
}

// containsChinese 检查文本是否包含中文字符
func containsChinese(text string) bool {
	for _, r := range text {
		if r >= 0x4E00 && r <= 0x9FFF {
			return true
		}
	}
	return false
}

// containsJapanese 检查文本是否包含日文字符
func containsJapanese(text string) bool {
	for _, r := range text {
		if (r >= 0x3040 && r <= 0x309F) || // 平假名
			(r >= 0x30A0 && r <= 0x30FF) { // 片假名
			return true
		}
	}
	return false
}

// containsKorean 检查文本是否包含韩文字符
func containsKorean(text string) bool {
	for _, r := range text {
		if r >= 0xAC00 && r <= 0xD7AF {
			return true
		}
	}
	return false
}
