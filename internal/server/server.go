package server

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/middleware"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/routes"
	"github.com/xxnuo/MTranServer/internal/services"
)

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

	// 设置 Gin 模式
	// 始终使用 ReleaseMode，我们使用自定义的日志中间件
	gin.SetMode(gin.ReleaseMode)

	// 创建 Gin 引擎（不使用默认中间件）
	r := gin.New()

	// 添加自定义中间件
	r.Use(middleware.Recovery())
	r.Use(middleware.Logger())

	// 注册路由
	routes.Setup(r, cfg.APIToken)

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

		logger.Info("Shutting down server...")

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		// 关闭所有翻译引擎
		services.CleanupAllEngines()

		if err := srv.Shutdown(ctx); err != nil {
			logger.Error("Server forced to shutdown: %v", err)
		}

		close(shutdownDone)
	}()

	// 总是输出服务启动信息（即使在 warn/error 模式下）
	fmt.Fprintf(os.Stderr, "[INFO] %s HTTP Service URL: http://%s\n",
		time.Now().Format("2006/01/02 15:04:05"), addr)
	fmt.Fprintf(os.Stderr, "[INFO] %s Swagger UI: http://%s/docs/index.html\n",
		time.Now().Format("2006/01/02 15:04:05"), addr)

	// 总是输出日志级别信息（即使在 warn/error 模式下）
	fmt.Fprintf(os.Stderr, "[INFO] %s Log level set to: %s\n",
		time.Now().Format("2006/01/02 15:04:05"), cfg.LogLevel)

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		// 服务器启动失败，确保清理资源
		services.CleanupAllEngines()
		return fmt.Errorf("failed to start server: %w", err)
	}

	// 等待优雅关闭完成
	<-shutdownDone
	logger.Info("Server shutdown complete")

	return nil
}
