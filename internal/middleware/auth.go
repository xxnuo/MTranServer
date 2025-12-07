package middleware

import (
	"net/http"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
)

func Auth(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {

		if apiToken == "" {
			c.Next()
			return
		}

		token := c.GetHeader("Authorization")
		if token != "" {
			token = strings.TrimPrefix(token, "Bearer ")
		} else {
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
