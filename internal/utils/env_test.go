package utils

import (
	"os"
	"testing"
)

func TestGetEnv(t *testing.T) {
	tests := []struct {
		name         string
		key          string
		defaultValue string
		envValue     string
		setEnv       bool
		want         string
	}{
		{
			name:         "environment variable exists",
			key:          "TEST_VAR",
			defaultValue: "default",
			envValue:     "custom",
			setEnv:       true,
			want:         "custom",
		},
		{
			name:         "environment variable not set",
			key:          "TEST_VAR_NOT_SET",
			defaultValue: "default",
			envValue:     "",
			setEnv:       false,
			want:         "default",
		},
		{
			name:         "environment variable is empty string",
			key:          "TEST_VAR_EMPTY",
			defaultValue: "default",
			envValue:     "",
			setEnv:       true,
			want:         "default",
		},
		{
			name:         "default value is empty",
			key:          "TEST_VAR_DEFAULT_EMPTY",
			defaultValue: "",
			envValue:     "",
			setEnv:       false,
			want:         "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			// Setup
			if tt.setEnv {
				os.Setenv(tt.key, tt.envValue)
				defer os.Unsetenv(tt.key)
			}

			// Test
			got := GetEnv(tt.key, tt.defaultValue)
			if got != tt.want {
				t.Errorf("GetEnv() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestParseBoolEnv(t *testing.T) {
	tests := []struct {
		name  string
		value string
		want  bool
	}{
		// true cases
		{name: "true lowercase", value: "true", want: true},
		{name: "true uppercase", value: "TRUE", want: true},
		{name: "true mixed case", value: "True", want: true},
		{name: "1", value: "1", want: true},
		{name: "yes lowercase", value: "yes", want: true},
		{name: "yes uppercase", value: "YES", want: true},
		{name: "yes mixed case", value: "Yes", want: true},
		{name: "true with spaces", value: "  true  ", want: true},
		{name: "yes with spaces", value: "  yes  ", want: true},

		// false cases
		{name: "false lowercase", value: "false", want: false},
		{name: "false uppercase", value: "FALSE", want: false},
		{name: "false mixed case", value: "False", want: false},
		{name: "0", value: "0", want: false},
		{name: "no lowercase", value: "no", want: false},
		{name: "no uppercase", value: "NO", want: false},
		{name: "no mixed case", value: "No", want: false},
		{name: "false with spaces", value: "  false  ", want: false},
		{name: "no with spaces", value: "  no  ", want: false},

		// invalid/edge cases
		{name: "empty string", value: "", want: false},
		{name: "invalid string", value: "invalid", want: false},
		{name: "random text", value: "xyz", want: false},
		{name: "number 2", value: "2", want: false},
		{name: "spaces only", value: "   ", want: false},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got := ParseBoolEnv(tt.value)
			if got != tt.want {
				t.Errorf("ParseBoolEnv(%q) = %v, want %v", tt.value, got, tt.want)
			}
		})
	}
}
