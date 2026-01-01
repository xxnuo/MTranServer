package server

import (
	"net/http"
	"net/http/httptest"
	"os"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/routes"
	"github.com/xxnuo/MTranServer/internal/version"
)

func TestMain(m *testing.M) {
	gin.SetMode(gin.TestMode)

	models.GlobalRecords = &models.RecordsData{
		Data: []models.RecordItem{
			{SourceLanguage: "en", TargetLanguage: "zh-Hans"},
		},
	}

	os.Exit(m.Run())
}

func TestGetVersion(t *testing.T) {
	v := version.GetVersion()
	assert.NotEmpty(t, v)
}

func TestServerRoutes(t *testing.T) {
	r := gin.New()
	routes.Setup(r, "")

	tests := []struct {
		name           string
		method         string
		path           string
		expectedStatus int
	}{
		{"Version", "GET", "/version", http.StatusOK},
		{"Health", "GET", "/health", http.StatusOK},
		{"Heartbeat", "GET", "/__heartbeat__", http.StatusOK},
		{"LBHeartbeat", "GET", "/__lbheartbeat__", http.StatusOK},
		{"Languages", "GET", "/languages", http.StatusOK},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := httptest.NewRecorder()
			req, _ := http.NewRequest(tt.method, tt.path, nil)
			r.ServeHTTP(w, req)

			assert.Equal(t, tt.expectedStatus, w.Code)
		})
	}
}

func TestServerRoutesWithAuth(t *testing.T) {
	r := gin.New()
	routes.Setup(r, "test-token")

	tests := []struct {
		name           string
		method         string
		path           string
		token          string
		expectedStatus int
	}{
		{"LanguagesWithToken", "GET", "/languages", "test-token", http.StatusOK},
		{"LanguagesWithoutToken", "GET", "/languages", "", http.StatusUnauthorized},
		{"LanguagesWithWrongToken", "GET", "/languages", "wrong-token", http.StatusUnauthorized},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			w := httptest.NewRecorder()
			req, _ := http.NewRequest(tt.method, tt.path, nil)
			if tt.token != "" {
				req.Header.Set("Authorization", tt.token)
			}
			r.ServeHTTP(w, req)

			assert.Equal(t, tt.expectedStatus, w.Code)
		})
	}
}
