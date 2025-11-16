package middleware

import (
	"fmt"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
)

// Logger 自定义日志中间件，将 Gin 的日志输出到我们的日志系统
func Logger() gin.HandlerFunc {
	return func(c *gin.Context) {
		// 开始时间
		start := time.Now()
		path := c.Request.URL.Path
		raw := c.Request.URL.RawQuery

		// 处理请求
		c.Next()

		// 结束时间
		end := time.Now()
		latency := end.Sub(start)

		clientIP := c.ClientIP()
		method := c.Request.Method
		statusCode := c.Writer.Status()
		errorMessage := c.Errors.ByType(gin.ErrorTypePrivate).String()

		if raw != "" {
			path = path + "?" + raw
		}

		// 根据状态码选择日志级别
		logFunc := logger.Info
		if statusCode >= 500 {
			logFunc = logger.Error
		} else if statusCode >= 400 {
			logFunc = logger.Warn
		}

		// 构建日志消息
		msg := fmt.Sprintf("%s %s %d %v %s",
			method,
			path,
			statusCode,
			latency,
			clientIP,
		)

		if errorMessage != "" {
			msg = fmt.Sprintf("%s | Error: %s", msg, errorMessage)
		}

		logFunc(msg)
	}
}

// Recovery 自定义恢复中间件，将 panic 信息输出到我们的日志系统
func Recovery() gin.HandlerFunc {
	return func(c *gin.Context) {
		defer func() {
			if err := recover(); err != nil {
				logger.Error("Panic recovered: %v", err)
				c.AbortWithStatus(500)
			}
		}()
		c.Next()
	}
}
