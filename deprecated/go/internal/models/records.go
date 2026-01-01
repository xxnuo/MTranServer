package models

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/xxnuo/MTranServer/data"
	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/downloader"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/utils"
)

const (
	RecordsUrl         = "https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/translations-models-v2/records"
	RecordsFileName    = "records.json"
	AttachmentsBaseUrl = "https://firefox-settings-attachments.cdn.mozilla.net"
)

type RecordsData struct {
	Data []RecordItem `json:"data"`
}

type RecordItem struct {
	Name             string     `json:"name"`
	Schema           int64      `json:"schema"`
	Version          string     `json:"version"`
	FileType         string     `json:"fileType"`
	Attachment       Attachment `json:"attachment"`
	Architecture     string     `json:"architecture"`
	SourceLanguage   string     `json:"sourceLanguage"`
	TargetLanguage   string     `json:"targetLanguage"`
	DecompressedHash string     `json:"decompressedHash"`
	DecompressedSize int64      `json:"decompressedSize"`
	FilterExpression string     `json:"filter_expression"`
	ID               string     `json:"id"`
	LastModified     int64      `json:"last_modified"`
}

type Attachment struct {
	Hash     string `json:"hash"`
	Size     int64  `json:"size"`
	Filename string `json:"filename"`
	Location string `json:"location"`
	MimeType string `json:"mimetype"`
}

var (
	GlobalRecords *RecordsData
)

func (r *RecordsData) GetLanguagePairs() []string {
	pairMap := make(map[string]bool)
	for _, record := range r.Data {
		pair := fmt.Sprintf("%s-%s", record.SourceLanguage, record.TargetLanguage)
		pairMap[pair] = true
	}

	pairs := make([]string, 0, len(pairMap))
	for pair := range pairMap {
		pairs = append(pairs, pair)
	}
	return pairs
}

func (r *RecordsData) HasLanguagePair(fromLang, toLang string) bool {
	for _, record := range r.Data {
		if record.SourceLanguage == fromLang && record.TargetLanguage == toLang {
			return true
		}
	}
	return false
}

func (r *RecordsData) GetVersions(fromLang, toLang string) []string {
	versionMap := make(map[string]bool)
	for _, record := range r.Data {
		if record.SourceLanguage == fromLang && record.TargetLanguage == toLang {
			versionMap[record.Version] = true
		}
	}

	versions := make([]string, 0, len(versionMap))
	for version := range versionMap {
		versions = append(versions, version)
	}
	return versions
}

func InitRecords() error {
	cfg := config.GetConfig()
	recordsPath := filepath.Join(cfg.ConfigDir, "records.json")

	if err := os.MkdirAll(cfg.ConfigDir, 0755); err != nil {
		return fmt.Errorf("failed to create config directory: %w", err)
	}
	if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
		return fmt.Errorf("failed to create model directory: %w", err)
	}

	if cfg.EnableOfflineMode {
		return initRecordsOffline(recordsPath)
	}
	return initRecordsOnline(recordsPath)
}

func computeHash(data []byte) string {
	h := sha256.Sum256(data)
	return hex.EncodeToString(h[:])
}

func isValidRecordsFormat(jsonData []byte) bool {
	return strings.Contains(string(jsonData), `"sourceLanguage"`)
}

func loadRecordsFromBytes(jsonData []byte) error {
	var records RecordsData
	if err := json.Unmarshal(jsonData, &records); err != nil {
		return fmt.Errorf("failed to parse records.json: %w", err)
	}
	GlobalRecords = &records
	logger.Debug("Loaded %d model records", len(records.Data))
	return nil
}

func initRecordsOffline(recordsPath string) error {
	fileData, err := os.ReadFile(recordsPath)
	if err == nil {
		fileHash := computeHash(fileData)
		if fileHash == data.RecordsHash {
			logger.Debug("Loading records.json from %s (hash matched)", recordsPath)
			return loadRecordsFromBytes(fileData)
		}
		logger.Info("Records hash mismatch, updating from embedded data...")
	}

	logger.Info("Initializing records.json from embedded data")
	if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
		logger.Warn("Failed to write records.json to %s, using embedded data: %v", recordsPath, err)
		return loadRecordsFromBytes(data.RecordsJson)
	}

	logger.Debug("Loading records.json from %s", recordsPath)
	return loadRecordsFromBytes(data.RecordsJson)
}

func initRecordsOnline(recordsPath string) error {
	cfg := config.GetConfig()
	d := downloader.New(cfg.ConfigDir)

	logger.Info("Downloading latest records.json from remote...")
	if err := d.Download(RecordsUrl, RecordsFileName, &downloader.DownloadOptions{
		Overwrite: true,
	}); err != nil {
		logger.Warn("Failed to download records.json: %v, falling back to embedded data", err)
		if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
			logger.Warn("Failed to write embedded records.json: %v", err)
		}
		return loadRecordsFromBytes(data.RecordsJson)
	}

	fileData, err := os.ReadFile(recordsPath)
	if err != nil {
		logger.Warn("Failed to read downloaded records.json: %v, using embedded data", err)
		return loadRecordsFromBytes(data.RecordsJson)
	}

	if !isValidRecordsFormat(fileData) {
		logger.Warn("Downloaded records.json has invalid format, using embedded data")
		if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
			logger.Warn("Failed to write embedded records.json: %v", err)
		}
		return loadRecordsFromBytes(data.RecordsJson)
	}

	logger.Debug("Loading records.json from %s", recordsPath)
	return loadRecordsFromBytes(fileData)
}

func DownloadRecords() error {
	cfg := config.GetConfig()

	logger.Info("Updating records.json from remote")

	d := downloader.New(cfg.ConfigDir)
	if err := d.Download(RecordsUrl, RecordsFileName, &downloader.DownloadOptions{
		Overwrite: true,
	}); err != nil {
		return fmt.Errorf("Failed to download records.json: %w", err)
	}

	return InitRecords()
}

func computeFileHash(filePath string) (string, error) {
	fileData, err := os.ReadFile(filePath)
	if err != nil {
		return "", err
	}
	h := sha256.Sum256(fileData)
	return hex.EncodeToString(h[:]), nil
}

func DownloadModel(toLang string, fromLang string, version string) error {

	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return err
		}
	}

	var matchedRecords []RecordItem
	for _, record := range GlobalRecords.Data {
		if record.TargetLanguage == toLang && record.SourceLanguage == fromLang {
			if version == "" || record.Version == version {
				matchedRecords = append(matchedRecords, record)
			}
		}
	}

	if len(matchedRecords) == 0 {
		return fmt.Errorf("No model found for %s -> %s (version: %s)", fromLang, toLang, version)
	}

	targetRecords := matchedRecords
	if version == "" {

		fileTypeMap := make(map[string][]RecordItem)
		for _, record := range matchedRecords {
			fileTypeMap[record.FileType] = append(fileTypeMap[record.FileType], record)
		}

		targetRecords = []RecordItem{}
		for _, records := range fileTypeMap {
			versions := make([]string, len(records))
			recordMap := make(map[string]RecordItem)
			for i, r := range records {
				versions[i] = r.Version
				recordMap[r.Version] = r
			}
			latestVersion := utils.GetLargestVersion(versions)
			targetRecords = append(targetRecords, recordMap[latestVersion])
		}
	}

	cfg := config.GetConfig()
	langPairDir := filepath.Join(cfg.ModelDir, fmt.Sprintf("%s_%s", fromLang, toLang))

	if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
		return fmt.Errorf("Failed to create model directory: %w", err)
	}
	if err := os.MkdirAll(langPairDir, 0755); err != nil {
		return fmt.Errorf("Failed to create language pair directory: %w", err)
	}

	logger.Info("Downloading model files for %s -> %s", fromLang, toLang)

	d := downloader.New(langPairDir)

	for _, record := range targetRecords {
		filename := record.Attachment.Filename
		fileUrl := AttachmentsBaseUrl + "/" + record.Attachment.Location
		compressedHash := record.Attachment.Hash

		decompressedFilename := strings.TrimSuffix(filename, ".zst")
		decompressedPath := filepath.Join(langPairDir, decompressedFilename)

		needDownload := false
		if _, err := os.Stat(decompressedPath); os.IsNotExist(err) {
			needDownload = true
		} else if !cfg.EnableOfflineMode && record.DecompressedHash != "" {
			localHash, err := computeFileHash(decompressedPath)
			if err != nil {
				logger.Warn("Failed to compute hash for %s: %v, will re-download", decompressedFilename, err)
				needDownload = true
			} else if localHash != record.DecompressedHash {
				logger.Info("Model file %s hash mismatch (local: %s, expected: %s), updating...",
					decompressedFilename, localHash[:8], record.DecompressedHash[:8])
				needDownload = true
			}
		}

		if !needDownload {
			logger.Debug("Model file up to date: %s", decompressedFilename)
			continue
		}

		logger.Debug("Downloading model file: %s (type: %s)", filename, record.FileType)
		if err := d.Download(fileUrl, filename, &downloader.DownloadOptions{
			SHA256:    compressedHash,
			Overwrite: true,
		}); err != nil {
			return fmt.Errorf("Failed to download %s: %w", filename, err)
		}

		compressedPath := filepath.Join(langPairDir, filename)
		logger.Debug("Decompressing: %s -> %s", filename, decompressedFilename)
		if err := utils.DecompressZstd(compressedPath, decompressedPath); err != nil {
			return fmt.Errorf("Failed to decompress %s: %w", filename, err)
		}

		os.Remove(compressedPath)
	}

	logger.Info("Model files downloaded successfully for %s -> %s", fromLang, toLang)
	return nil
}

func GetModelFiles(modelDir, fromLang, toLang string) (map[string]string, error) {

	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return nil, fmt.Errorf("failed to init records: %w", err)
		}
	}

	langPairDir := filepath.Join(modelDir, fmt.Sprintf("%s_%s", fromLang, toLang))

	files := make(map[string]string)
	fileTypeMap := make(map[string]string)

	for _, record := range GlobalRecords.Data {
		if record.SourceLanguage == fromLang && record.TargetLanguage == toLang {
			filename := strings.TrimSuffix(record.Attachment.Filename, ".zst")
			fullPath := filepath.Join(langPairDir, filename)

			if _, err := os.Stat(fullPath); err == nil {
				fileTypeMap[record.FileType] = fullPath
			}
		}
	}

	if modelPath, ok := fileTypeMap["model"]; ok {
		files["model"] = modelPath
	} else {
		return nil, fmt.Errorf("model file not found for %s -> %s", fromLang, toLang)
	}

	if lexPath, ok := fileTypeMap["lex"]; ok {
		files["lex"] = lexPath
	} else {
		return nil, fmt.Errorf("lex file not found for %s -> %s", fromLang, toLang)
	}

	if vocabPath, ok := fileTypeMap["vocab"]; ok {

		files["vocab_src"] = vocabPath
		files["vocab_trg"] = vocabPath
	} else {

		if srcvocabPath, ok := fileTypeMap["srcvocab"]; ok {
			files["vocab_src"] = srcvocabPath
		} else {
			return nil, fmt.Errorf("source vocab file not found for %s -> %s", fromLang, toLang)
		}

		if trgvocabPath, ok := fileTypeMap["trgvocab"]; ok {
			files["vocab_trg"] = trgvocabPath
		} else {
			return nil, fmt.Errorf("target vocab file not found for %s -> %s", fromLang, toLang)
		}
	}

	return files, nil
}

func IsModelDownloaded(modelDir, fromLang, toLang string) bool {
	_, err := GetModelFiles(modelDir, fromLang, toLang)
	return err == nil
}

func GetSupportedLanguages() ([]string, error) {
	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return nil, err
		}
	}

	langMap := make(map[string]bool)
	for _, record := range GlobalRecords.Data {
		langMap[record.SourceLanguage] = true
		langMap[record.TargetLanguage] = true
	}

	langs := make([]string, 0, len(langMap))
	for lang := range langMap {
		langs = append(langs, lang)
	}
	return langs, nil
}

func ValidateLanguagePair(fromLang, toLang string) error {
	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return fmt.Errorf("failed to init records: %w", err)
		}
	}

	if fromLang == "" {
		return fmt.Errorf("source language cannot be empty")
	}

	if toLang == "" {
		return fmt.Errorf("target language cannot be empty")
	}

	if fromLang == toLang {
		return fmt.Errorf("source and target languages cannot be the same")
	}

	if !GlobalRecords.HasLanguagePair(fromLang, toLang) {
		return fmt.Errorf("language pair %s -> %s is not supported", fromLang, toLang)
	}

	return nil
}
