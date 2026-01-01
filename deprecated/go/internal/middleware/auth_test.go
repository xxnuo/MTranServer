package middleware

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
)

func TestAuthWithValidToken(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(Auth("test-token"))
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "success")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("GET", "/test", nil)
	req.Header.Set("Authorization", "test-token")
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "success", w.Body.String())
}

func TestAuthWithValidTokenInQuery(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(Auth("test-token"))
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "success")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("GET", "/test?token=test-token", nil)
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "success", w.Body.String())
}

func TestAuthWithInvalidToken(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(Auth("test-token"))
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "success")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("GET", "/test", nil)
	req.Header.Set("Authorization", "wrong-token")
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "Unauthorized")
}

func TestAuthWithNoToken(t *testing.T) {
	gin.SetMode(gin.TestMode)

	r := gin.New()
	r.Use(Auth("test-token"))
	r.GET("/test", func(c *gin.Context) {
		c.String(http.StatusOK, "success")
	})

	w := httptest.NewRecorder()
	req, _ := http.NewRequest("GET", "/test", nil)
	r.ServeHTTP(w, req)

	assert.Equal(t, http.StatusUnauthorized, w.Code)
	assert.Contains(t, w.Body.String(), "Unauthorized")
}
