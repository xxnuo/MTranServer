package utils

import (
	"os"
	"path/filepath"
	"testing"
)

func TestCalculateSHA256(t *testing.T) {
	// 创建临时文件
	tempDir, err := os.MkdirTemp("", "file-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	testContent := []byte("Hello, World!")
	expectedSHA256 := "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

	filePath := filepath.Join(tempDir, "test.txt")
	if err := os.WriteFile(filePath, testContent, 0644); err != nil {
		t.Fatal(err)
	}

	// 计算 SHA256
	hash, err := ComputeSHA256(filePath)
	if err != nil {
		t.Fatalf("计算 SHA256 失败: %v", err)
	}

	if hash != expectedSHA256 {
		t.Fatalf("SHA256 不匹配: 期望 %s, 实际 %s", expectedSHA256, hash)
	}
}
