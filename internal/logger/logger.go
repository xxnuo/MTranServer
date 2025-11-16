package logger

import (
	"fmt"
	"log"
	"os"
	"strings"
)

// LogLevel 日志级别
type LogLevel int

const (
	DEBUG LogLevel = iota
	INFO
	WARN
	ERROR
)

var (
	currentLevel LogLevel = INFO
	debugLogger  *log.Logger
	infoLogger   *log.Logger
	warnLogger   *log.Logger
	errorLogger  *log.Logger
)

func init() {
	// 初始化各级别日志器
	debugLogger = log.New(os.Stdout, "[DEBUG] ", log.Ldate|log.Ltime|log.Lshortfile)
	infoLogger = log.New(os.Stdout, "[INFO] ", log.Ldate|log.Ltime)
	warnLogger = log.New(os.Stdout, "[WARN] ", log.Ldate|log.Ltime)
	errorLogger = log.New(os.Stderr, "[ERROR] ", log.Ldate|log.Ltime|log.Lshortfile)
}

// SetLevel 设置日志级别
func SetLevel(level string) {
	switch strings.ToLower(level) {
	case "debug":
		currentLevel = DEBUG
	case "info":
		currentLevel = INFO
	case "warn", "warning":
		currentLevel = WARN
	case "error":
		currentLevel = ERROR
	default:
		currentLevel = INFO
	}
}

// GetLevel 获取当前日志级别
func GetLevel() string {
	switch currentLevel {
	case DEBUG:
		return "debug"
	case INFO:
		return "info"
	case WARN:
		return "warn"
	case ERROR:
		return "error"
	default:
		return "info"
	}
}

// Debug 输出调试日志
func Debug(format string, v ...interface{}) {
	if currentLevel <= DEBUG {
		debugLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

// Info 输出信息日志
func Info(format string, v ...interface{}) {
	if currentLevel <= INFO {
		infoLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

// Warn 输出警告日志
func Warn(format string, v ...interface{}) {
	if currentLevel <= WARN {
		warnLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

// Error 输出错误日志
func Error(format string, v ...interface{}) {
	if currentLevel <= ERROR {
		errorLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

// Fatal 输出致命错误日志并退出程序
func Fatal(format string, v ...interface{}) {
	errorLogger.Output(2, fmt.Sprintf(format, v...))
	os.Exit(1)
}

// Debugf Debug 的别名
func Debugf(format string, v ...interface{}) {
	Debug(format, v...)
}

// Infof Info 的别名
func Infof(format string, v ...interface{}) {
	Info(format, v...)
}

// Warnf Warn 的别名
func Warnf(format string, v ...interface{}) {
	Warn(format, v...)
}

// Errorf Error 的别名
func Errorf(format string, v ...interface{}) {
	Error(format, v...)
}

// Fatalf Fatal 的别名
func Fatalf(format string, v ...interface{}) {
	Fatal(format, v...)
}
