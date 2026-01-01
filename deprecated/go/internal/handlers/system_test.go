package handlers

import (
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/gin-gonic/gin"
	"github.com/stretchr/testify/assert"
	"github.com/xxnuo/MTranServer/internal/version"
)

func TestHandleVersion(t *testing.T) {
	gin.SetMode(gin.TestMode)

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleVersion(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), "version")
	assert.Contains(t, w.Body.String(), version.GetVersion())
}

func TestHandleHealth(t *testing.T) {
	gin.SetMode(gin.TestMode)

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleHealth(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Contains(t, w.Body.String(), "status")
	assert.Contains(t, w.Body.String(), "ok")
}

func TestHandleHeartbeat(t *testing.T) {
	gin.SetMode(gin.TestMode)

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleHeartbeat(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "Ready", w.Body.String())
}

func TestHandleLBHeartbeat(t *testing.T) {
	gin.SetMode(gin.TestMode)

	w := httptest.NewRecorder()
	c, _ := gin.CreateTestContext(w)

	HandleLBHeartbeat(c)

	assert.Equal(t, http.StatusOK, w.Code)
	assert.Equal(t, "Ready", w.Body.String())
}
