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

func Run() error {

	cfg := config.GetConfig()

	if err := models.InitRecords(); err != nil {
		return fmt.Errorf("failed to initialize records: %w", err)
	}

	if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
		return fmt.Errorf("failed to create model directory: %w", err)
	}

	if err := manager.EnsureWorkerBinary(cfg); err != nil {
		return fmt.Errorf("failed to initialize worker binary: %w", err)
	}

	gin.SetMode(gin.ReleaseMode)

	r := gin.New()

	r.Use(middleware.Recovery())
	r.Use(middleware.Logger())

	routes.Setup(r, cfg.APIToken)

	addr := fmt.Sprintf("%s:%s", cfg.Host, cfg.Port)
	srv := &http.Server{
		Addr:    addr,
		Handler: r,
	}

	shutdownDone := make(chan struct{})

	go func() {
		sigChan := make(chan os.Signal, 1)
		signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
		<-sigChan

		logger.Info("Shutting down server...")

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		services.CleanupAllEngines()

		if err := srv.Shutdown(ctx); err != nil {
			logger.Error("Server forced to shutdown: %v", err)
		}

		close(shutdownDone)
	}()

	fmt.Fprintf(os.Stderr, "[INFO] %s HTTP Service URL: http://%s\n",
		time.Now().Format("2006/01/02 15:04:05"), addr)
	fmt.Fprintf(os.Stderr, "[INFO] %s Swagger UI: http://%s/docs/index.html\n",
		time.Now().Format("2006/01/02 15:04:05"), addr)

	fmt.Fprintf(os.Stderr, "[INFO] %s Log level set to: %s\n",
		time.Now().Format("2006/01/02 15:04:05"), cfg.LogLevel)

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {

		services.CleanupAllEngines()
		return fmt.Errorf("failed to start server: %w", err)
	}

	<-shutdownDone
	logger.Info("Server shutdown complete")

	return nil
}
