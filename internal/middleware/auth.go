package middleware

import (
	"net/http"

	"github.com/gin-gonic/gin"
)

// Auth 认证中间件
func Auth(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.GetHeader("Authorization")
		if token == "" {
			token = c.Query("token")
		}

		if token != apiToken {
			c.JSON(http.StatusUnauthorized, gin.H{
				"error": "Unauthorized",
			})
			c.Abort()
			return
		}

		c.Next()
	}
}
