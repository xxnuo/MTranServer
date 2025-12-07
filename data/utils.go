package data

import "crypto/sha256"

//go:generate go run gen_update.go
//go:generate go run gen_hash.go

func GetRecordsInfo() (hash string, size int) {
	return RecordsHash, len(RecordsJson)
}

func ComputeHash(data []byte) [32]byte {
	return sha256.Sum256(data)
}
