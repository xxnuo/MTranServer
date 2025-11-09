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

// TestIntegrationServerSetup 测试服务器完整设置
func TestIntegrationServerSetup(t *testing.T) {
	gin.SetMode(gin.TestMode)

	// 初始化测试数据
	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{FromLang: "en", ToLang: "zh-Hans"},
			{FromLang: "zh-Hans", ToLang: "en"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	// 测试无需认证的端点
	t.Run("PublicEndpoints", func(t *testing.T) {
		endpoints := []string{"/version", "/health", "/__heartbeat__", "/__lbheartbeat__"}
		for _, endpoint := range endpoints {
			w := httptest.NewRecorder()
			req, _ := http.NewRequest("GET", endpoint, nil)
			r.ServeHTTP(w, req)
			assert.Equal(t, http.StatusOK, w.Code, "Endpoint %s should return 200", endpoint)
		}
	})

	// 测试需要认证的端点
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

	// 测试认证失败
	t.Run("AuthenticationFailure", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("GET", "/languages", nil)
		r.ServeHTTP(w, req)
		assert.Equal(t, http.StatusUnauthorized, w.Code)
	})
}

// TestIntegrationCORS 测试 CORS 完整流程
func TestIntegrationCORS(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	routes.Setup(r, "")

	// 测试 OPTIONS 请求
	w := httptest.NewRecorder()
	req, _ := http.NewRequest("OPTIONS", "/version", nil)
	req.Header.Set("Origin", "http://example.com")
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusNoContent, w.Code)
	assert.Equal(t, "*", w.Header().Get("Access-Control-Allow-Origin"))
}

// TestIntegrationPluginEndpoints 测试插件端点
func TestIntegrationPluginEndpoints(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{FromLang: "en", ToLang: "zh-Hans"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	// 测试沉浸式翻译端点（需要 token 在 query）
	t.Run("ImmeEndpoint", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from":  "en",
			"to":    "zh-Hans",
			"trans": []string{"Hello"},
		}
		body, _ := json.Marshal(reqBody)

		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/imme?token=test-token", bytes.NewBuffer(body))
		req.Header.Set("Content-Type", "application/json")
		r.ServeHTTP(w, req)

		// 由于没有实际的翻译引擎，期望返回错误
		// 但至少应该通过认证和请求解析
		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	// 测试简约翻译端点（需要 token 在 header KEY）
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

		// 由于没有实际的翻译引擎，期望返回错误
		// 但至少应该通过认证和请求解析
		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})
}

// TestIntegrationAPIEndpoints 测试 API 端点
func TestIntegrationAPIEndpoints(t *testing.T) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{FromLang: "en", ToLang: "zh-Hans"},
		},
	}

	r := gin.New()
	routes.Setup(r, "test-token")

	// 测试翻译端点
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

		// 由于没有实际的翻译引擎，期望返回错误
		// 但至少应该通过认证和请求解析
		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	// 测试批量翻译端点
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

		// 由于没有实际的翻译引擎，期望返回错误
		// 但至少应该通过认证和请求解析
		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})

	// 测试 Google 兼容端点
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

		// 由于没有实际的翻译引擎，期望返回错误
		// 但至少应该通过认证和请求解析
		assert.NotEqual(t, http.StatusUnauthorized, w.Code)
		assert.NotEqual(t, http.StatusBadRequest, w.Code)
	})
}

// TestIntegrationInvalidRequests 测试无效请求
func TestIntegrationInvalidRequests(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	routes.Setup(r, "test-token")

	// 测试无效的 JSON
	t.Run("InvalidJSON", func(t *testing.T) {
		w := httptest.NewRecorder()
		req, _ := http.NewRequest("POST", "/translate", bytes.NewBufferString("invalid json"))
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Authorization", "test-token")
		r.ServeHTTP(w, req)

		assert.Equal(t, http.StatusBadRequest, w.Code)
	})

	// 测试缺少必需字段
	t.Run("MissingRequiredFields", func(t *testing.T) {
		reqBody := map[string]interface{}{
			"from": "en",
			// 缺少 to 和 text
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
