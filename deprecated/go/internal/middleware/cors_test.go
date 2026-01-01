package middleware

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
)

func TestCORS(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(CORS())
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "test")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("GET", "/test", nil)
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "*", w.Header().Get("Access-Control-Allow-Origin"))
	assert.Equal(t, "true", w.Header().Get("Access-Control-Allow-Credentials"))
	assert.NotEmpty(t, w.Header().Get("Access-Control-Allow-Headers"))
	assert.NotEmpty(t, w.Header().Get("Access-Control-Allow-Methods"))
}

func TestCORSOptions(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(CORS())
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "test")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("OPTIONS", "/test", nil)
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusNoContent, w.Code)
}
