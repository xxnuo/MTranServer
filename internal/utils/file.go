package utils

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
)

func VerifySHA256(filepath, expectedHash string) error {
	file, err := os.Open(filepath)
	if err != nil {
		return fmt.Errorf("Failed to open file: %w", err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return fmt.Errorf("Failed to calculate SHA256: %w", err)
	}

	actualHash := hex.EncodeToString(hash.Sum(nil))
	if actualHash != expectedHash {
		return fmt.Errorf("SHA256 mismatch: expected %s, actual %s", expectedHash, actualHash)
	}

	return nil
}

func ComputeSHA256(filepath string) (string, error) {
	file, err := os.Open(filepath)
	if err != nil {
		return "", fmt.Errorf("Failed to open file: %w", err)
	}
	defer file.Close()

	hash := sha256.New()
	if _, err := io.Copy(hash, file); err != nil {
		return "", fmt.Errorf("Failed to calculate SHA256: %w", err)
	}

	return hex.EncodeToString(hash.Sum(nil)), nil
}
