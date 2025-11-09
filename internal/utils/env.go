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

// GetIntEnv gets the environment variable value as int or returns the default value
func GetIntEnv(key string, defaultValue int) int {
	if value := os.Getenv(key); value != "" {
		result, err := strconv.Atoi(value)
		if err == nil {
			return result
		}
	}
	return defaultValue
}
