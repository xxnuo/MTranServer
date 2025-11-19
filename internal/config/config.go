package config

import (
	"flag"
	"os"
	"path/filepath"

	"github.com/xxnuo/MTranServer/internal/utils"
)

// Config 包含服务器配置
type Config struct {
	// 内部配置
	LogLevel  string
	HomeDir   string
	ConfigDir string
	ModelDir  string

	// 服务器配置
	Host              string
	Port              string
	EnableWebUI       bool
	EnableOfflineMode bool
	WorkerIdleTimeout int    // Worker 空闲超时时间（秒）
	APIToken          string // API 访问令牌
}

var (
	GlobalConfig *Config = nil
)

// GetConfig 加载配置，优先级：命令行参数 > 环境变量 > 默认值
func GetConfig() *Config {
	if GlobalConfig != nil {
		return GlobalConfig
	}
	cfg := &Config{}
	homeDir, err := os.UserHomeDir()
	if err != nil {
		panic(err)
	}
	cfg.HomeDir = filepath.Join(homeDir, ".config", "mtran")
	cfg.ConfigDir = filepath.Join(cfg.HomeDir, "server")
	cfg.ModelDir = filepath.Join(cfg.HomeDir, "models")

	flag.StringVar(&cfg.LogLevel, "log-level", utils.GetEnv("MT_LOG_LEVEL", "warn"), "Log level (debug, info, warn, error)")
	flag.StringVar(&cfg.ConfigDir, "config-dir", utils.GetEnv("MT_CONFIG_DIR", cfg.ConfigDir), "Config directory")
	flag.StringVar(&cfg.ModelDir, "model-dir", utils.GetEnv("MT_MODEL_DIR", cfg.ModelDir), "Model directory")
	flag.StringVar(&cfg.Host, "host", utils.GetEnv("MT_HOST", "0.0.0.0"), "Server host address")
	flag.StringVar(&cfg.Port, "port", utils.GetEnv("MT_PORT", "8989"), "Server port")
	flag.BoolVar(&cfg.EnableWebUI, "ui", utils.GetBoolEnv("MT_ENABLE_UI", true), "Enable web UI")
	flag.BoolVar(&cfg.EnableOfflineMode, "offline", utils.GetBoolEnv("MT_OFFLINE", false), "Enable offline mode")
	flag.IntVar(&cfg.WorkerIdleTimeout, "worker-idle-timeout", utils.GetIntEnv("MT_WORKER_IDLE_TIMEOUT", 300), "Worker idle timeout in seconds")
	flag.StringVar(&cfg.APIToken, "api-token", utils.GetEnv("MT_API_TOKEN", ""), "API access token")

	GlobalConfig = cfg
	return cfg
}
