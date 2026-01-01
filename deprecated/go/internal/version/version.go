package version

import (
	"strings"

	"github.com/xxnuo/MTranServer/bin"
)

var Version = "v0.0.0-dev"

func GetVersion() string {
	return Version
}

func GetWorkerVersion() string {
	return strings.TrimSpace(bin.WorkerVersion)
}
