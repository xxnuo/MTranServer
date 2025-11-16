//go:build ignore

package main

import (
	"context"
	"log"

	"github.com/xxnuo/MTranServer/internal/downloader"
	"github.com/xxnuo/MTranServer/internal/models"
)

func main() {
	log.Printf("Downloading records.json from %s...", models.RecordsUrl)

	// Download using downloader
	d := downloader.New(".")
	err := d.Download(models.RecordsUrl, models.RecordsFileName, &downloader.DownloadOptions{
		Context:   context.Background(),
		Overwrite: true,
	})
	if err != nil {
		log.Fatalf("Failed to download records.json: %v", err)
	}

	log.Printf("Successfully downloaded to %s", models.RecordsFileName)
}
