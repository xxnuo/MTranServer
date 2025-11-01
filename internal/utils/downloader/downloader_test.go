package downloader

import (
	"context"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func TestDownload(t *testing.T) {
	// 创建测试 HTTP 服务器
	testContent := []byte("Hello, World!")
	expectedSHA256 := "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(testContent)
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 测试下载
	d := New(tempDir)
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:  expectedSHA256,
		Context: context.Background(),
	})

	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	// 验证文件存在
	filePath := filepath.Join(tempDir, "test.txt")
	if _, err := os.Stat(filePath); os.IsNotExist(err) {
		t.Fatal("文件不存在")
	}

	// 验证文件内容
	content, err := os.ReadFile(filePath)
	if err != nil {
		t.Fatal(err)
	}

	if string(content) != string(testContent) {
		t.Fatalf("文件内容不匹配: 期望 %s, 实际 %s", testContent, content)
	}
}

func TestDownloadWithWrongSHA256(t *testing.T) {
	// 创建测试 HTTP 服务器
	testContent := []byte("Hello, World!")

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(testContent)
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 测试下载（使用错误的 SHA256）
	d := New(tempDir)
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:  "0000000000000000000000000000000000000000000000000000000000000000",
		Context: context.Background(),
	})

	if err == nil {
		t.Fatal("应该返回 SHA256 校验失败错误")
	}
}

func TestDownloadSkipExisting(t *testing.T) {
	// 创建测试 HTTP 服务器
	testContent := []byte("Hello, World!")
	expectedSHA256 := "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"
	requestCount := 0

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requestCount++
		w.Write(testContent)
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 第一次下载
	d := New(tempDir)
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:  expectedSHA256,
		Context: context.Background(),
	})

	if err != nil {
		t.Fatalf("第一次下载失败: %v", err)
	}

	firstRequestCount := requestCount

	// 第二次下载（应该跳过）
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:  expectedSHA256,
		Context: context.Background(),
	})

	if err != nil {
		t.Fatalf("第二次下载失败: %v", err)
	}

	if requestCount != firstRequestCount {
		t.Fatalf("期望 %d 次请求（第二次应跳过）, 实际 %d 次", firstRequestCount, requestCount)
	}
}

func TestDownloadWithOverwrite(t *testing.T) {
	// 创建测试 HTTP 服务器
	testContent := []byte("Hello, World!")
	expectedSHA256 := "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"
	requestCount := 0

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requestCount++
		w.Write(testContent)
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 第一次下载
	d := New(tempDir)
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:  expectedSHA256,
		Context: context.Background(),
	})

	if err != nil {
		t.Fatalf("第一次下载失败: %v", err)
	}

	firstRequestCount := requestCount

	// 第二次下载（强制覆盖）
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		SHA256:    expectedSHA256,
		Overwrite: true,
		Context:   context.Background(),
	})

	if err != nil {
		t.Fatalf("第二次下载失败: %v", err)
	}

	if requestCount <= firstRequestCount {
		t.Fatalf("期望至少 %d 次请求, 实际 %d 次", firstRequestCount+1, requestCount)
	}
}

func TestDownloadWithContext(t *testing.T) {
	// 创建测试 HTTP 服务器（延迟响应）
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		time.Sleep(2 * time.Second)
		w.Write([]byte("Hello, World!"))
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 创建带超时的 context
	ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
	defer cancel()

	// 测试下载（应该超时）
	d := New(tempDir)
	err = d.Download(server.URL, "test.txt", &DownloadOptions{
		Context: ctx,
	})

	if err == nil {
		t.Fatal("应该返回超时错误")
	}
}

func TestDownloadFile(t *testing.T) {
	// 创建测试 HTTP 服务器
	testContent := []byte("Hello, World!")
	expectedSHA256 := "dffd6021bb2bd5b0af676290809ec3a53191dd81c7f70a4b28688a362182986f"

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(testContent)
	}))
	defer server.Close()

	// 创建临时目录
	tempDir, err := os.MkdirTemp("", "downloader-test-*")
	if err != nil {
		t.Fatal(err)
	}
	defer os.RemoveAll(tempDir)

	// 测试快捷方法
	destPath := filepath.Join(tempDir, "test.txt")
	err = DownloadFile(server.URL, destPath, expectedSHA256)

	if err != nil {
		t.Fatalf("下载失败: %v", err)
	}

	// 验证文件存在
	if _, err := os.Stat(destPath); os.IsNotExist(err) {
		t.Fatal("文件不存在")
	}

	// 验证文件内容
	content, err := os.ReadFile(destPath)
	if err != nil {
		t.Fatal(err)
	}

	if string(content) != string(testContent) {
		t.Fatalf("文件内容不匹配: 期望 %s, 实际 %s", testContent, content)
	}
}
