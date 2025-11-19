package middleware

import (
	"net/http"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
)

// Auth 认证中间件
func Auth(apiToken string) gin.HandlerFunc {
	return func(c *gin.Context) {
		// 如果未设置 token，则不进行认证
		if apiToken == "" {
			c.Next()
			return
		}

		// 从 Authorization header 获取 token
		token := c.GetHeader("Authorization")
		if token != "" {
			// 支持 Bearer token 格式
			if strings.HasPrefix(token, "Bearer ") {
				token = strings.TrimPrefix(token, "Bearer ")
			}
		} else {
			// 从 query 参数获取 token
			token = c.Query("token")
		}

		// 验证 token
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
