//go:build ignore

package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"runtime"

	"github.com/xxnuo/MTranServer/internal/downloader"
)

const (
	GithubRepo = "xxnuo/MTranCore"
	ReleaseTag = "latest"
)

func main() {
	// Detect platform
	goos := runtime.GOOS
	goarch := runtime.GOARCH

	var suffix string
	switch goos {
	case "windows":
		suffix = ".exe"
	case "js":
		suffix = ".wasm"
	default:
		suffix = ""
	}

	workerBinary := fmt.Sprintf("worker-%s-%s%s", goos, goarch, suffix)
	downloadURL := fmt.Sprintf("https://github.com/%s/releases/%s/download/%s", GithubRepo, ReleaseTag, workerBinary)

	log.Printf("Detecting platform: %s-%s", goos, goarch)
	log.Printf("Downloading %s from %s...", workerBinary, downloadURL)

	// Ensure bin directory exists
	if err := os.MkdirAll(".", 0755); err != nil {
		log.Fatalf("Failed to create bin directory: %v", err)
	}

	targetFile := "worker"
	os.Remove(targetFile)

	// Download
	d := downloader.New(".")
	err := d.Download(downloadURL, targetFile, &downloader.DownloadOptions{
		Context:   context.Background(),
		Overwrite: true,
	})
	if err != nil {
		log.Fatalf("Failed to download worker binary: %v", err)
	}

	// Set executable permission
	if err := os.Chmod(targetFile, 0755); err != nil {
		log.Printf("Warning: Failed to set executable permission: %v", err)
	}

	log.Printf("Downloaded successfully to %s", targetFile)
}
