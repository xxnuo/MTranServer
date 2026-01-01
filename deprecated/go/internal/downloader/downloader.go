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

type Downloader struct {
	DestDir string

	ProgressFunc getter.ProgressTracker
}

type DownloadOptions struct {
	SHA256 string

	Overwrite bool

	Context context.Context
}

func New(destDir string) *Downloader {
	return &Downloader{
		DestDir: destDir,
	}
}

func (d *Downloader) SetProgressFunc(fn getter.ProgressTracker) {
	d.ProgressFunc = fn
}

func (d *Downloader) Download(urlStr, filename string, opts *DownloadOptions) error {
	if opts == nil {
		opts = &DownloadOptions{
			Context: context.Background(),
		}
	}
	if opts.Context == nil {
		opts.Context = context.Background()
	}

	if err := os.MkdirAll(d.DestDir, 0755); err != nil {
		return fmt.Errorf("Failed to create directory: %w", err)
	}

	dst := filepath.Join(d.DestDir, filename)

	if !opts.Overwrite {
		if _, err := os.Stat(dst); err == nil {

			if opts.SHA256 != "" {
				if err := utils.VerifySHA256(dst, opts.SHA256); err == nil {

					logger.Debug("File %s already exists and verified, skipping download", filename)
					return nil
				}
			}
		}
	}

	logger.Info("Downloading %s from %s", filename, urlStr)

	tmpFile := dst + ".tmp"
	defer os.Remove(tmpFile)

	httpClient := &http.Client{
		Timeout: 30 * time.Minute,
		CheckRedirect: func(req *http.Request, via []*http.Request) error {

			if len(via) >= 10 {
				return fmt.Errorf("stopped after 10 redirects")
			}
			return nil
		},
	}

	transport := &http.Transport{
		TLSClientConfig: &tls.Config{InsecureSkipVerify: false},
	}

	if proxyURL := os.Getenv("HTTP_PROXY"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	} else if proxyURL := os.Getenv("http_proxy"); proxyURL != "" {
		if parsedURL, err := url.Parse(proxyURL); err == nil {
			transport.Proxy = http.ProxyURL(parsedURL)
		}
	}

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

	httpGetter := &getter.HttpGetter{
		Client: httpClient,
	}

	clientOpts := []getter.ClientOption{
		getter.WithContext(opts.Context),
		getter.WithGetters(map[string]getter.Getter{
			"http":  httpGetter,
			"https": httpGetter,
		}),
		getter.WithDecompressors(map[string]getter.Decompressor{}),
	}

	if d.ProgressFunc != nil {
		clientOpts = append(clientOpts, getter.WithProgress(d.ProgressFunc))
	}

	client := &getter.Client{
		Src:  urlStr,
		Dst:  tmpFile,
		Mode: getter.ClientModeFile,
	}

	if err := client.Configure(clientOpts...); err != nil {
		return fmt.Errorf("Failed to configure downloader: %w", err)
	}

	if err := client.Get(); err != nil {
		return fmt.Errorf("Failed to download: %w", err)
	}

	logger.Debug("Download completed: %s", filename)

	if opts.SHA256 != "" {
		logger.Debug("Verifying SHA256 for %s", filename)
		if err := utils.VerifySHA256(tmpFile, opts.SHA256); err != nil {
			return fmt.Errorf("Failed to verify SHA256: %w", err)
		}
		logger.Debug("SHA256 verification passed for %s", filename)
	}

	if err := os.Rename(tmpFile, dst); err != nil {
		return fmt.Errorf("Failed to move file: %w", err)
	}

	logger.Info("Successfully downloaded: %s", filename)
	return nil
}

func DownloadFile(url, destPath, sha256sum string) error {
	dir := filepath.Dir(destPath)
	filename := filepath.Base(destPath)

	d := New(dir)
	return d.Download(url, filename, &DownloadOptions{
		SHA256:  sha256sum,
		Context: context.Background(),
	})
}
