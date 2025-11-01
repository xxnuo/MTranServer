package bin

import "crypto/sha256"

//go:generate go run gen_hash.go

// GetWorkerInfo returns information about the embedded worker binary
func GetWorkerInfo() (hash string, size int) {
	return WorkerHash, len(WorkerBinary)
}

// ComputeHash computes the SHA256 hash of the given data
func ComputeHash(data []byte) [32]byte {
	return sha256.Sum256(data)
}
