package routes

import (
	"github.com/gin-gonic/gin"
	swaggerFiles "github.com/swaggo/files"
	ginSwagger "github.com/swaggo/gin-swagger"

	"github.com/xxnuo/MTranServer/internal/docs"
	"github.com/xxnuo/MTranServer/internal/handlers"
	"github.com/xxnuo/MTranServer/internal/middleware"
)

// Setup 设置所有路由
func Setup(r *gin.Engine, apiToken string) {
	// 添加 CORS 中间件
	r.Use(middleware.CORS())

	// 配置 Swagger
	docs.SwaggerInfo.BasePath = "/"
	r.GET("/docs/*any", ginSwagger.WrapHandler(swaggerFiles.Handler))

	// 无需认证的路由
	r.GET("/version", handlers.HandleVersion)
	r.GET("/health", handlers.HandleHealth)
	r.GET("/__heartbeat__", handlers.HandleHeartbeat)
	r.GET("/__lbheartbeat__", handlers.HandleLBHeartbeat)

	// 需要认证的路由
	auth := r.Group("/")
	if apiToken != "" {
		auth.Use(middleware.Auth(apiToken))
	}

	// 内置接口
	auth.GET("/languages", handlers.HandleLanguages)
	auth.POST("/translate", handlers.HandleTranslate)
	auth.POST("/translate/batch", handlers.HandleTranslateBatch)

	// 插件兼容接口
	r.POST("/imme", handlers.HandleImmeTranslate(apiToken))
	r.POST("/kiss", handlers.HandleKissTranslate(apiToken))
	r.POST("/deepl", handlers.HandleDeeplTranslate(apiToken))
	r.POST("/google/language/translate/v2", handlers.HandleGoogleCompatTranslate(apiToken))
	r.GET("/google/translate_a/single", handlers.HandleGoogleTranslateSingle(apiToken))
	r.POST("/hcfy", handlers.HandleHcfyTranslate(apiToken))
}
