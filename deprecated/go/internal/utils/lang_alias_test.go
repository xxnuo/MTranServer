package utils

import (
	"testing"
)

func TestNormalizeLanguageCode(t *testing.T) {
	tests := []struct {
		input    string
		expected string
	}{
		{"", ""},
		{"en", "en"},
		{"EN", "en"},
		{"en-US", "en"},
		{"en_US", "en"},
		{"zh", "zh-Hans"},
		{"zh-CN", "zh-Hans"},
		{"zh_CN", "zh-Hans"},
		{"zh-Hans", "zh-Hans"},
		{"zh-SG", "zh-Hans"},
		{"zh-TW", "zh-Hant"},
		{"zh-HK", "zh-Hant"},
		{"zh-Hant", "zh-Hant"},
		{"fr-CA", "fr"},
		{"es-MX", "es"},
		{"pt-BR", "pt"},
		{"ja", "ja"},
		{"jp", "ja"},
		{"ko", "ko"},
		{"kr", "ko"},
		{"unknown-Code", "unknown"},
	}

	for _, tt := range tests {
		t.Run(tt.input, func(t *testing.T) {
			got := NormalizeLanguageCode(tt.input)
			if got != tt.expected {
				t.Errorf("NormalizeLanguageCode(%q) = %q, want %q", tt.input, got, tt.expected)
			}
		})
	}
}
