package server

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/routes"
)

func TestIntegrationServerSetup(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{SourceLanguage: "en", TargetLanguage: "zh-Hans"},
			{SourceLanguage: "zh-Hans", TargetLanguage: "en"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	t.Run("PublicEndpoints", func(t *testing.T) {
		endpoints := []string{"/version", "/health", "/__heartbeat__", "/__lbheartbeat__"}
		for _, endpoint := range endpoints {
			w := httptest.NewRecorder()
			req, _ := http.NewRequest("GET", endpoint, nil)
			r.ServeHTTP(w, req)
			assert.Equal(t, http.StatusOK, w.Code, "Endpoint %s should return 200", endpoint)
		}
	})

	t.Run("AuthenticatedEndpoints", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("GET", "/languages", nil)
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)
		assert.Equal(t, http.StatusOK, w.Code)

		var response map[string]interface{}
		err := json.Unmarshal(w.Body.Bytes(), &response)
		assert.NoError(t, err)
		assert.Contains(t, response, "languages")
	})

	t.Run("AuthenticationFailure", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("GET", "/languages", nil)
		r.ServeHTTP(w, req)
		assert.Equal(t, http.StatusUnauthorized, w.Code)
	})
}

func TestIntegrationCORS(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	routes.Setup(r, "")

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("OPTIONS", "/version", nil)
	req.Header.Set("Origin", "http://example.com")
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusNoContent, w.Code)
	assert.Equal(t, "*", w.Header().Get("Access-Control-Allow-Origin"))
}

func TestIntegrationPluginEndpoints(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{SourceLanguage: "en", TargetLanguage: "zh-Hans"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	t.Run("ImmeEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"source_lang": "en",
			"target_lang": "zh-Hans",
			"text_list":   []string{"Hello"},
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/imme?token=test-token", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		r.ServeHTTP(w, req)

		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	t.Run("KissEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from": "en",
			"to":   "zh-Hans",
			"text": "Hello",
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/kiss", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("KEY", "test-token")
		r.ServeHTTP(w, req)

		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})
}

func TestIntegrationAPIEndpoints(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{SourceLanguage: "en", TargetLanguage: "zh-Hans"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	t.Run("TranslateEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from": "en",
			"to":   "zh-Hans",
			"text": "Hello",
			"html": false,
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/translate", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	t.Run("TranslateBatchEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from":  "en",
			"to":    "zh-Hans",
			"texts": []string{"Hello", "World"},
			"html":  false,
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/translate/batch", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	t.Run("GoogleCompatEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"q":      "Hello",
			"source": "en",
			"target": "zh-Hans",
			"format": "text",
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/language/translate/v2", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})
}

func TestIntegrationInvalidRequests(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	routes.Setup(r, "test-token")

	t.Run("InvalidJSON", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/translate", bytes.NewBufferString("invalid json"))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.Equal(t, http.StatusBadRequest, w.Code)
	})

	t.Run("MissingRequiredFields", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from": "en",
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/translate", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.Equal(t, http.StatusBadRequest, w.Code)
	})
}
