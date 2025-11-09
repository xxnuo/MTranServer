package handlers

import (
	"net/http"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/models"
)

// handleLanguages 获取支持的语言列表
// @Summary      获取支持的语言列表
// @Description  返回所有支持的翻译语言代码
// @Tags         翻译
// @Produce      json
// @Success      200  {object}  map[string][]string
// @Failure      500  {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /languages [get]
func HandleLanguages(c *gin.Context) {
	if models.GlobalRecords == nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Records not initialized",
		})
		return
	}

	// 从 records 中提取所有支持的语言
	langMap := make(map[string]bool)
	for _, record := range models.GlobalRecords.Data {
		langMap[record.FromLang] = true
		langMap[record.ToLang] = true
	}

	languages := make([]string, 0, len(langMap))
	for lang := range langMap {
		languages = append(languages, lang)
	}

	c.JSON(http.StatusOK, gin.H{
		"languages": languages,
	})
}
