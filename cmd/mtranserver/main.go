package main

import (
	"flag"
	"fmt"
	"log"
	"os"

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
	versionFlag := flag.Bool("version", false, "显示版本信息")
	flag.Parse()

	if *versionFlag {
		fmt.Printf("MTranServer %s\n", version.GetVersion())
		os.Exit(0)
	}

	if err := server.Run(); err != nil {
		log.Fatalf("Server error: %v", err)
	}

	services.CleanupAllEngines()
}
