package routes

import (
	"net/http"

	"github.com/gin-gonic/gin"
	swaggerFiles "github.com/swaggo/files"
	ginSwagger "github.com/swaggo/gin-swagger"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/docs"
	"github.com/xxnuo/MTranServer/internal/handlers"
	"github.com/xxnuo/MTranServer/internal/middleware"
	"github.com/xxnuo/MTranServer/ui"
)

func Setup(r *gin.Engine, apiToken string) {

	r.Use(middleware.CORS())

	docs.SwaggerInfo.BasePath = "/"
	r.GET("/docs/*any", ginSwagger.WrapHandler(swaggerFiles.Handler))

	r.GET("/version", handlers.HandleVersion)
	r.GET("/health", handlers.HandleHealth)
	r.GET("/__heartbeat__", handlers.HandleHeartbeat)
	r.GET("/__lbheartbeat__", handlers.HandleLBHeartbeat)

	auth := r.Group("/")
	if apiToken != "" {
		auth.Use(middleware.Auth(apiToken))
	}

	auth.GET("/languages", handlers.HandleLanguages)
	auth.POST("/translate", handlers.HandleTranslate)
	auth.POST("/translate/batch", handlers.HandleTranslateBatch)

	r.POST("/imme", handlers.HandleImmeTranslate(apiToken))
	r.POST("/kiss", handlers.HandleKissTranslate(apiToken))
	r.POST("/deepl", handlers.HandleDeeplTranslate(apiToken))
	r.POST("/google/language/translate/v2", handlers.HandleGoogleCompatTranslate(apiToken))
	r.GET("/google/translate_a/single", handlers.HandleGoogleTranslateSingle(apiToken))
	r.POST("/hcfy", handlers.HandleHcfyTranslate(apiToken))

	cfg := config.GetConfig()
	if cfg.EnableWebUI {
		distFS, err := ui.GetDistFS()
		if err == nil {
			r.StaticFS("/ui", http.FS(distFS))

			r.GET("/", func(c *gin.Context) {
				c.Redirect(http.StatusMovedPermanently, "/ui/")
			})
		}
	}
}
