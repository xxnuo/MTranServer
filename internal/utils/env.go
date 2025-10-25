package utils

import (
	"os"
	"strconv"
	"strings"
)

// GetEnv gets the environment variable value or returns the default value
func GetEnv(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

// ParseBoolEnv parses boolean values from environment variables
// Supports: true/false, 1/0, yes/no (case insensitive)
func ParseBoolEnv(value string) bool {
	value = strings.ToLower(strings.TrimSpace(value))
	switch value {
	case "true", "1", "yes":
		return true
	case "false", "0", "no":
		return false
	default:
		// Try standard parsing as fallback
		result, _ := strconv.ParseBool(value)
		return result
	}
}
