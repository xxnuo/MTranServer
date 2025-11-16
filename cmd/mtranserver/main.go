package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/server"
	"github.com/xxnuo/MTranServer/internal/services"
	"github.com/xxnuo/MTranServer/internal/version"
)

// @title           MTranServer API
// @version         3.0.0
// @description     超低资源消耗超快的离线翻译服务器 API
// @termsOfService  https://github.com/xxnuo/MTranServer

// @contact.name   API Support
// @contact.url    https://github.com/xxnuo/MTranServer/issues
// @contact.email  support@example.com

// @license.name  MIT
// @license.url   https://github.com/xxnuo/MTranServer/blob/main/LICENSE

// @host      localhost:8989
// @BasePath  /

// @securityDefinitions.apikey ApiKeyAuth
// @in header
// @name Authorization

// @securityDefinitions.apikey ApiKeyQuery
// @in query
// @name token

func main() {
	// 定义 version 和 help 标志
	versionFlag := flag.Bool("version", false, "Show version information")
	versionShortFlag := flag.Bool("v", false, "Show version information (shorthand)")

	// 自定义 Usage 函数
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "MTranServer %s - Ultra-low resource consumption, ultra-fast offline translation server\n\n", version.GetVersion())
		fmt.Fprintf(os.Stderr, "Usage:\n")
		fmt.Fprintf(os.Stderr, "  %s [options]\n\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "Options:\n")
		flag.PrintDefaults()
		fmt.Fprintf(os.Stderr, "\nEnvironment Variables:\n")
		fmt.Fprintf(os.Stderr, "  MT_LOG_LEVEL           Log level (debug, info, warn, error)\n")
		fmt.Fprintf(os.Stderr, "  MT_CONFIG_DIR          Configuration directory\n")
		fmt.Fprintf(os.Stderr, "  MT_MODEL_DIR           Model directory\n")
		fmt.Fprintf(os.Stderr, "  MT_HOST                Server host address\n")
		fmt.Fprintf(os.Stderr, "  MT_PORT                Server port\n")
		fmt.Fprintf(os.Stderr, "  MT_UI                  Enable Web UI (true/false)\n")
		fmt.Fprintf(os.Stderr, "  MT_OFFLINE             Enable offline mode (true/false)\n")
		fmt.Fprintf(os.Stderr, "  MT_WORKER_IDLE_TIMEOUT Worker idle timeout in seconds\n")
		fmt.Fprintf(os.Stderr, "  API_TOKEN              API access token\n")
		fmt.Fprintf(os.Stderr, "  CORE_API_TOKEN         API access token (alternative)\n")
		fmt.Fprintf(os.Stderr, "\nExamples:\n")
		fmt.Fprintf(os.Stderr, "  %s --host 127.0.0.1 --port 8080\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  %s --ui --offline\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "  MT_PORT=9000 %s\n", os.Args[0])
		fmt.Fprintf(os.Stderr, "\nMore information: https://github.com/xxnuo/MTranServer\n")
	}

	// 加载配置（会注册其他标志）
	cfg := config.GetConfig()

	// 解析命令行参数
	flag.Parse()

	// 设置日志级别
	logger.SetLevel(cfg.LogLevel)

	// 处理 version 标志
	if *versionFlag || *versionShortFlag {
		fmt.Printf("MTranServer %s\n", version.GetVersion())
		os.Exit(0)
	}

	// 启动服务器
	if err := server.Run(); err != nil {
		logger.Fatal("Server error: %v", err)
	}

	services.CleanupAllEngines()
}
