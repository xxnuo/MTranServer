package downloader

import (
	"context"
	"crypto/tls"
	"fmt"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"time"

	"github.com/hashicorp/go-getter"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/utils"
)

// Downloader 下载器结构
type Downloader struct {
	// 下载目录
	DestDir string
	// 进度回调函数
	ProgressFunc getter.ProgressTracker
}

// DownloadOptions 下载选项
type DownloadOptions struct {
	// SHA256 校验和
	SHA256 string
	// 是否覆盖已存在的文件
	Overwrite bool
	// Context 用于取消下载
	Context context.Context
}

// New 创建新的下载器
func New(destDir string) *Downloader {
	return &Downloader{
		DestDir: destDir,
	}
}

// SetProgressFunc 设置进度回调函数
func (d *Downloader) SetProgressFunc(fn getter.ProgressTracker) {
	d.ProgressFunc = fn
}

// Download 下载文件到指定目录
func (d *Downloader) Download(urlStr, filename string, opts *DownloadOptions) error {
	if opts == nil {
		opts = &DownloadOptions{
			Context: context.Background(),
		}
	}
	if opts.Context == nil {
		opts.Context = context.Background()
	}

	// 确保目标目录存在
	if err := os.MkdirAll(d.DestDir, 0755); err != nil {
		return fmt.Errorf("Failed to create directory: %w", err)
	}

	// 目标文件路径
	dst := filepath.Join(d.DestDir, filename)

	// 检查文件是否已存在
	if !opts.Overwrite {
		if _, err := os.Stat(dst); err == nil {
			// 文件存在，检查 SHA256
			if opts.SHA256 != "" {
				if err := utils.VerifySHA256(dst, opts.SHA256); err == nil {
					// 文件已存在且校验通过，跳过下载
					logger.Debug("File %s already exists and verified, skipping download", filename)
					return nil
				}
			}
		}
	}

	logger.Info("Downloading %s from %s", filename, urlStr)

	// 创建临时文件
	tmpFile := dst + ".tmp"
	defer os.Remove(tmpFile)

	// 创建 HTTP 客户端，支持代理和重定向
	httpClient := &http.Client{
		Timeout: 30 * time.Minute,
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			// 允许最多 10 次重定向
			if len(via) >= 10 {
				return fmt.Errorf("stopped after 10 redirects")
			}
			return nil
		},
	}

	// 配置代理
	transport := &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: false},
	}

	// 从环境变量读取代理设置
	if proxyURL := os.Getenv("HTTP_PROXY"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	} else if proxyURL := os.Getenv("http_proxy"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	}

	// 支持 HTTPS_PROXY
	if proxyURL := os.Getenv("HTTPS_PROXY"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	} else if proxyURL := os.Getenv("https_proxy"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	}

	httpClient.Transport = transport

	// 配置 HttpGetter
	httpGetter := &getter.HttpGetter{
		Client: httpClient,
	}

	// 配置 getter 客户端选项
	clientOpts := []getter.ClientOption{
		getter.WithContext(opts.Context),
		getter.WithGetters(map[string]getter.Getter{
			"http":  httpGetter,
			"https": httpGetter,
		}),
	}

	if d.ProgressFunc != nil {
		clientOpts = append(clientOpts, getter.WithProgress(d.ProgressFunc))
	}

	// 创建 getter 客户端
	client := &getter.Client{
		Src:  urlStr,
		Dst:  tmpFile,
		Mode: getter.ClientModeFile,
	}

	// 应用客户端选项
	if err := client.Configure(clientOpts...); err != nil {
		return fmt.Errorf("Failed to configure downloader: %w", err)
	}

	// 执行下载
	if err := client.Get(); err != nil {
		return fmt.Errorf("Failed to download: %w", err)
	}

	logger.Debug("Download completed: %s", filename)

	// 校验 SHA256
	if opts.SHA256 != "" {
		logger.Debug("Verifying SHA256 for %s", filename)
		if err := utils.VerifySHA256(tmpFile, opts.SHA256); err != nil {
			return fmt.Errorf("Failed to verify SHA256: %w", err)
		}
		logger.Debug("SHA256 verification passed for %s", filename)
	}

	// 移动临时文件到目标位置
	if err := os.Rename(tmpFile, dst); err != nil {
		return fmt.Errorf("Failed to move file: %w", err)
	}

	logger.Info("Successfully downloaded: %s", filename)
	return nil
}

// DownloadFile 快捷方法：下载文件
func DownloadFile(url, destPath, sha256sum string) error {
	dir := filepath.Dir(destPath)
	filename := filepath.Base(destPath)

	d := New(dir)
	return d.Download(url, filename, &DownloadOptions{
		SHA256:  sha256sum,
		Context: context.Background(),
	})
}
