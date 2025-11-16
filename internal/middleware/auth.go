package middleware

import (
	"net/http"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
)

// Auth 认证中间件
func Auth(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.GetHeader("Authorization")
		if token == "" {
			token = c.Query("token")
		}

		if token != apiToken {
			logger.Warn("Unauthorized access attempt from %s to %s", c.ClientIP(), c.Request.URL.Path)
			c.JSON(http.StatusUnauthorized, gin.H{
				"error": "Unauthorized",
			})
			c.Abort()
			return
		}

		c.Next()
	}
}
