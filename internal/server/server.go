package server

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/routes"
	"github.com/xxnuo/MTranServer/internal/services"
	"github.com/xxnuo/MTranServer/internal/utils"
)

const Version = "v3.0.0"

// Run 启动服务器
func Run() error {
	// 加载配置
	cfg := config.GetConfig()

	// 初始化 records
	if err := models.InitRecords(); err != nil {
		return fmt.Errorf("failed to initialize records: %w", err)
	}

	// 创建必要的目录
	if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
		return fmt.Errorf("failed to create model directory: %w", err)
	}

	// 初始化 worker 二进制文件
	if err := manager.EnsureWorkerBinary(cfg); err != nil {
		return fmt.Errorf("failed to initialize worker binary: %w", err)
	}

	// 获取 API Token
	apiToken := utils.GetEnv("API_TOKEN", utils.GetEnv("CORE_API_TOKEN", ""))

	// 设置 Gin 模式
	if cfg.LogLevel == "debug" {
		gin.SetMode(gin.DebugMode)
	} else {
		gin.SetMode(gin.ReleaseMode)
	}

	// 创建 Gin 路由
	r := gin.Default()

	// 注册路由
	routes.Setup(r, apiToken)

	// 启动服务器
	addr := fmt.Sprintf("%s:%s", cfg.Host, cfg.Port)
	srv := &http.Server{
		Addr:    addr,
		Handler: r,
	}

	// 用于等待优雅关闭完成的通道
	shutdownDone := make(chan struct{})

	// 优雅关闭
	go func() {
		sigChan := make(chan os.Signal, 1)
		signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
		<-sigChan

		log.Println("Shutting down server...")

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		// 关闭所有翻译引擎
		services.CleanupAllEngines()

		if err := srv.Shutdown(ctx); err != nil {
			log.Printf("Server forced to shutdown: %v", err)
		}

		close(shutdownDone)
	}()

	log.Printf("HTTP Service URL: http://%s", addr)
	log.Printf("Swagger UI: http://%s/docs/index.html", addr)

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		// 服务器启动失败，确保清理资源
		services.CleanupAllEngines()
		return fmt.Errorf("failed to start server: %w", err)
	}

	// 等待优雅关闭完成
	<-shutdownDone
	log.Println("Server shutdown complete")

	return nil
}

// GetVersion 获取版本号
func GetVersion() string {
	return Version
}
