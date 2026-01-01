package middleware

import (
	"fmt"
	"time"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/logger"
)

func Logger() gin.HandlerFunc {
	return func(c *gin.Context) {

		start := time.Now()
		path := c.Request.URL.Path
		raw := c.Request.URL.RawQuery

		c.Next()

		end := time.Now()
		latency := end.Sub(start)

		clientIP := c.ClientIP()
		method := c.Request.Method
		statusCode := c.Writer.Status()
		errorMessage := c.Errors.ByType(gin.ErrorTypePrivate).String()

		if raw != "" {
			path = path + "?" + raw
		}

		logFunc := logger.Info
		if statusCode >= 500 {
			logFunc = logger.Error
		} else if statusCode >= 400 {
			logFunc = logger.Warn
		}

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
