package bin

import "crypto/sha256"

//go:generate go run gen_update.go
//go:generate go run gen_hash.go

func GetWorkerInfo() (hash string, size int) {
	return WorkerHash, len(WorkerBinary)
}

func ComputeHash(data []byte) [32]byte {
	return sha256.Sum256(data)
}
