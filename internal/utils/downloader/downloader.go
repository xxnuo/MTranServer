package downloader

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/hashicorp/go-getter"
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
func (d *Downloader) Download(url, filename string, opts *DownloadOptions) error {
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
				if err := verifySHA256(dst, opts.SHA256); err == nil {
					// 文件已存在且校验通过，跳过下载
					return nil
				}
			}
		}
	}

	// 创建临时文件
	tmpFile := dst + ".tmp"
	defer os.Remove(tmpFile)

	// 配置 getter 客户端选项
	clientOpts := []getter.ClientOption{
		getter.WithContext(opts.Context),
	}

	if d.ProgressFunc != nil {
		clientOpts = append(clientOpts, getter.WithProgress(d.ProgressFunc))
	}

	// 创建 getter 客户端
	client := &getter.Client{
		Src:  url,
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

	// 校验 SHA256
	if opts.SHA256 != "" {
		if err := verifySHA256(tmpFile, opts.SHA256); err != nil {
			return fmt.Errorf("Failed to verify SHA256: %w", err)
		}
	}

	// 移动临时文件到目标位置
	if err := os.Rename(tmpFile, dst); err != nil {
		return fmt.Errorf("Failed to move file: %w", err)
	}

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

// verifySHA256 校验文件的 SHA256
func verifySHA256(filepath, expectedHash string) error {
	file, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("Failed to open file: %w", err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return fmt.Errorf("Failed to calculate SHA256: %w", err)
	}

	actualHash := hex.EncodeToString(hash.Sum(nil))
	if actualHash != expectedHash {
		return fmt.Errorf("SHA256 mismatch: expected %s, actual %s", expectedHash, actualHash)
	}

	return nil
}

// CalculateSHA256 计算文件的 SHA256
func CalculateSHA256(filepath string) (string, error) {
	file, err := os.Open(filepath)
	if err != nil {
		return "", fmt.Errorf("Failed to open file: %w", err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", fmt.Errorf("Failed to calculate SHA256: %w", err)
	}

	return hex.EncodeToString(hash.Sum(nil)), nil
}
