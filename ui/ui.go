package ui

import (
	"embed"
	"io/fs"
)

//go:embed all:dist
var distFS embed.FS

// GetDistFS returns the embedded dist filesystem
func GetDistFS() (fs.FS, error) {
	return fs.Sub(distFS, "dist")
}
