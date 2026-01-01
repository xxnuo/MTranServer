package bin

import (
	_ "embed"
)

//go:embed worker
var WorkerBinary []byte

//go:embed worker.version
var WorkerVersion string
