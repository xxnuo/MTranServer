package handlers

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/xxnuo/MTranServer/internal/models"
)

func TestHandleLanguages(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{SourceLanguage: "en", TargetLanguage: "zh-Hans"},
			{SourceLanguage: "zh-Hans", TargetLanguage: "en"},
			{SourceLanguage: "en", TargetLanguage: "ja"},
		},
	}

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleLanguages(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), "languages")
	assert.Contains(t, w.Body.String(), "en")
	assert.Contains(t, w.Body.String(), "zh-Hans")
	assert.Contains(t, w.Body.String(), "ja")
}

func TestHandleLanguagesNotInitialized(t *testing.T) {
	gin.SetMode(gin.TestMode)

	originalRecords := models.GlobalRecords
	models.GlobalRecords = nil
	defer func() {
		models.GlobalRecords = originalRecords
	}()

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleLanguages(c)

	assert.Equal(t, http.StatusInternalServerError, w.Code)
	assert.Contains(t, w.Body.String(), "error")
	assert.Contains(t, w.Body.String(), "Records not initialized")
}
