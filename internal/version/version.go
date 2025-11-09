package version

// Version 会在编译时通过 -ldflags 注入
var Version = "v0.0.0-dev"

// GetVersion 获取版本号
func GetVersion() string {
	return Version
}
