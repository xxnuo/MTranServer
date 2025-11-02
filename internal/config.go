package internal

import (
	"flag"

	"github.com/xxnuo/MTranServer/internal/utils"
)

// Config 包含服务器配置
type Config struct {
	LogLevel  string
	ConfigDir string
	ModelDir  string

	// 服务器配置
	Host              string
	Port              string
	EnableWebUI       bool
	EnableOfflineMode bool
}

// LoadConfig 加载配置，优先级：命令行参数 > 环境变量 > 默认值
func LoadConfig() *Config {
	// Define command line flags
	logLevel := flag.String("log-level", "", "Log level (debug, info, warn, error)")
	configDir := flag.String("config-dir", "", "Config directory")
	modelDir := flag.String("model-dir", "", "Model directory")
	host := flag.String("host", "", "Server host address")
	port := flag.String("port", "", "Server port")
	enableWebUI := flag.String("ui", "", "Enable web UI (true/false)")
	enableOfflineMode := flag.String("offline", "", "Enable offline mode (true/false)")

	flag.Parse()
	return &Config{
		LogLevel:  getConfigValue(*logLevel, "LOG_LEVEL", "info"),
		ConfigDir: getConfigValue(*configDir, "CONFIG_DIR", "./"),
		ModelDir:  getConfigValue(*modelDir, "MODEL_DIR", "./"),

		// 服务器配置
		Host:              getConfigValue(*host, "HOST", "0.0.0.0"),
		Port:              getConfigValue(*port, "PORT", "8989"),
		EnableWebUI:       getBoolConfigValue(*enableWebUI, "UI", "true"),
		EnableOfflineMode: getBoolConfigValue(*enableOfflineMode, "OFFLINE", "true"),
	}
}

// getConfigValue 获取字符串值，优先级：命令行参数 > 环境变量 > 默认值
func getConfigValue(flagValue, envKey, defaultValue string) string {
	if flagValue != "" {
		return flagValue
	}
	return utils.GetEnv(envKey, defaultValue)
}

// getBoolConfigValue 获取布尔值，优先级：命令行参数 > 环境变量 > 默认值
func getBoolConfigValue(flagValue, envKey, defaultValue string) bool {
	if flagValue != "" {
		return utils.ParseBoolEnv(flagValue)
	}
	return utils.ParseBoolEnv(utils.GetEnv(envKey, defaultValue))
}
