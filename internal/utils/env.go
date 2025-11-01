package utils

import (
	"os"
	"strconv"
)

// GetEnv gets the environment variable value or returns the default value
func GetEnv(key string, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

// ParseBoolEnv parses boolean values from environment variables
func GetBoolEnv(key string, defaultValue bool) bool {
	if value := os.Getenv(key); value != "" {
		result, err := strconv.ParseBool(value)
		if err == nil && result {
			return result
		}
	}
	return defaultValue
}
