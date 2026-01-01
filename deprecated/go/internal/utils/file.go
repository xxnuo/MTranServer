package utils

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"

	"github.com/klauspost/compress/zstd"
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

func DecompressZstd(src, dst string) error {
	// Open the source file
	inputFile, err := os.Open(src)
	if err != nil {
		return fmt.Errorf("failed to open source file: %w", err)
	}
	defer inputFile.Close()

	// Create the decoder
	decoder, err := zstd.NewReader(inputFile)
	if err != nil {
		return fmt.Errorf("failed to create zstd reader: %w", err)
	}
	defer decoder.Close()

	// Create the destination file
	outputFile, err := os.Create(dst)
	if err != nil {
		return fmt.Errorf("failed to create destination file: %w", err)
	}
	defer outputFile.Close()

	// Copy the decompressed data
	if _, err := io.Copy(outputFile, decoder); err != nil {
		return fmt.Errorf("failed to decompress data: %w", err)
	}

	return nil
}
