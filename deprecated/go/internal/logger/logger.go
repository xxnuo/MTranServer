package logger

import (
	"fmt"
	"log"
	"os"
	"strings"
)

type LogLevel int

const (
	DEBUG LogLevel = iota
	INFO
	WARN
	ERROR
)

const (
	colorReset  = "\033[0m"
	colorCyan   = "\033[36m"
	colorGreen  = "\033[32m"
	colorYellow = "\033[33m"
	colorRed    = "\033[31m"
)

var (
	currentLevel LogLevel = INFO
	debugLogger  *log.Logger
	infoLogger   *log.Logger
	warnLogger   *log.Logger
	errorLogger  *log.Logger
)

func init() {
	debugLogger = log.New(os.Stdout, colorCyan+"[DEBUG]"+colorReset+" ", log.Ldate|log.Ltime|log.Lshortfile)
	infoLogger = log.New(os.Stdout, colorGreen+"[INFO]"+colorReset+" ", log.Ldate|log.Ltime)
	warnLogger = log.New(os.Stdout, colorYellow+"[WARN]"+colorReset+" ", log.Ldate|log.Ltime)
	errorLogger = log.New(os.Stderr, colorRed+"[ERROR]"+colorReset+" ", log.Ldate|log.Ltime|log.Lshortfile)
}

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

func Debug(format string, v ...interface{}) {
	if currentLevel <= DEBUG {
		debugLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

func Info(format string, v ...interface{}) {
	if currentLevel <= INFO {
		infoLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

func Warn(format string, v ...interface{}) {
	if currentLevel <= WARN {
		warnLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

func Error(format string, v ...interface{}) {
	if currentLevel <= ERROR {
		errorLogger.Output(2, fmt.Sprintf(format, v...))
	}
}

func Fatal(format string, v ...interface{}) {
	errorLogger.Output(2, fmt.Sprintf(format, v...))
	os.Exit(1)
}

func Debugf(format string, v ...interface{}) {
	Debug(format, v...)
}

func Infof(format string, v ...interface{}) {
	Info(format, v...)
}

func Warnf(format string, v ...interface{}) {
	Warn(format, v...)
}

func Errorf(format string, v ...interface{}) {
	Error(format, v...)
}

func Fatalf(format string, v ...interface{}) {
	Fatal(format, v...)
}
