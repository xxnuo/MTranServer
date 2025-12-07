package utils

import (
	"os"
	"strconv"
)

func GetEnv(key string, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}

func GetBoolEnv(key string, defaultValue bool) bool {
	if value := os.Getenv(key); value != "" {
		result, err := strconv.ParseBool(value)
		if err == nil && result {
			return result
		}
	}
	return defaultValue
}

func GetIntEnv(key string, defaultValue int) int {
	if value := os.Getenv(key); value != "" {
		result, err := strconv.Atoi(value)
		if err == nil {
			return result
		}
	}
	return defaultValue
}
