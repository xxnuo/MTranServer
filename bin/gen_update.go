//go:build ignore

package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"runtime"
	"strings"

	"github.com/xxnuo/MTranServer/internal/downloader"
)

const (
	GithubRepo = "xxnuo/MTranCore"
	ReleaseTag = "latest"
)

func getLatestVersion() (string, error) {
	url := fmt.Sprintf("https://github.com/%s/releases/%s", GithubRepo, ReleaseTag)

	client := &http.Client{
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}

	resp, err := client.Get(url)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusFound && resp.StatusCode != http.StatusMovedPermanently {
		return "", fmt.Errorf("unexpected status code: %d", resp.StatusCode)
	}

	location := resp.Header.Get("Location")
	if location == "" {
		return "", fmt.Errorf("no location header found")
	}

	parts := strings.Split(location, "/")
	if len(parts) < 2 {
		return "", fmt.Errorf("invalid location format: %s", location)
	}

	version := parts[len(parts)-1]
	return version, nil
}

func main() {
	goos := os.Getenv("TARGET_GOOS")
	goarch := os.Getenv("TARGET_GOARCH")
	if goos == "" {
		goos = runtime.GOOS
	}
	if goarch == "" {
		goarch = runtime.GOARCH
	}

	var suffix string
	switch goos {
	case "windows":
		suffix = ".exe"
	case "js":
		suffix = ".wasm"
	default:
		suffix = ""
	}

	version, err := getLatestVersion()
	if err != nil {
		log.Fatalf("Failed to get latest version: %v", err)
	}
	log.Printf("Latest MTranCore version: %s", version)

	workerBinary := fmt.Sprintf("worker-%s-%s%s", goos, goarch, suffix)
	downloadURL := fmt.Sprintf("https://github.com/%s/releases/download/%s/%s", GithubRepo, version, workerBinary)

	log.Printf("Detecting platform: %s-%s", goos, goarch)
	log.Printf("Downloading %s from %s...", workerBinary, downloadURL)

	if err := os.MkdirAll(".", 0755); err != nil {
		log.Fatalf("Failed to create bin directory: %v", err)
	}

	targetFile := "worker"
	os.Remove(targetFile)

	d := downloader.New(".")
	if err := d.Download(downloadURL, targetFile, &downloader.DownloadOptions{
		Context:   context.Background(),
		Overwrite: true,
	}); err != nil {
		log.Fatalf("Failed to download worker binary: %v", err)
	}

	if err := os.Chmod(targetFile, 0755); err != nil {
		log.Printf("Warning: Failed to set executable permission: %v", err)
	}

	log.Printf("Downloaded successfully to %s", targetFile)

	versionFile := "worker.version"
	if err := os.WriteFile(versionFile, []byte(version), 0644); err != nil {
		log.Fatalf("Failed to write version file: %v", err)
	}
	log.Printf("Version %s written to %s", version, versionFile)
}
