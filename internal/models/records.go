package models

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"github.com/xxnuo/MTranServer/data"
	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/downloader"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/utils"
)

const (
	RecordsUrl         = "https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/translations-models/records"
	RecordsFileName    = "records.json"
	AttachmentsBaseUrl = "https://firefox-settings-attachments.cdn.mozilla.net"
)

type RecordsData struct {
	Data []RecordItem `json:"data"`
}

type RecordItem struct {
	Hash       string     `json:"hash,omitempty"`
	Name       string     `json:"name"`
	Schema     int64      `json:"schema"`
	ToLang     string     `json:"toLang"`
	Version    string     `json:"version"`
	FileType   string     `json:"fileType"`
	FromLang   string     `json:"fromLang"`
	Attachment Attachment `json:"attachment"`
	ID         string     `json:"id"`
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
		pair := fmt.Sprintf("%s-%s", record.FromLang, record.ToLang)
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
		if record.FromLang == fromLang && record.ToLang == toLang {
			return true
		}
	}
	return false
}

func (r *RecordsData) GetVersions(fromLang, toLang string) []string {
	versionMap := make(map[string]bool)
	for _, record := range r.Data {
		if record.FromLang == fromLang && record.ToLang == toLang {
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

	if _, err := os.Stat(recordsPath); os.IsNotExist(err) {

		logger.Info("Initializing records.json from embedded data")
		// Ensure parent directories exist
		if err := os.MkdirAll(cfg.ConfigDir, 0755); err != nil {
			return fmt.Errorf("Failed to create config directory: %w", err)
		}
		if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
			return fmt.Errorf("Failed to create model directory: %w", err)
		}
		if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
			return fmt.Errorf("Failed to write records.json: %w", err)
		}
	}

	logger.Debug("Loading records.json from %s", recordsPath)
	fileData, err := os.ReadFile(recordsPath)
	if err != nil {
		return fmt.Errorf("Failed to read records.json: %w", err)
	}

	var records RecordsData
	if err := json.Unmarshal(fileData, &records); err != nil {
		return fmt.Errorf("Failed to parse records.json: %w", err)
	}

	GlobalRecords = &records
	logger.Debug("Loaded %d model records", len(records.Data))
	return nil
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

func DownloadModel(toLang string, fromLang string, version string) error {

	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return err
		}
	}

	var matchedRecords []RecordItem
	for _, record := range GlobalRecords.Data {
		if record.ToLang == toLang && record.FromLang == fromLang {
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

	// Ensure parent model directory exists with proper permissions
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
		sha256sum := record.Attachment.Hash

		logger.Debug("Downloading model file: %s (type: %s)", filename, record.FileType)
		if err := d.Download(fileUrl, filename, &downloader.DownloadOptions{
			SHA256:    sha256sum,
			Overwrite: false,
		}); err != nil {
			return fmt.Errorf("Failed to download %s: %w", filename, err)
		}
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
		if record.FromLang == fromLang && record.ToLang == toLang {
			filename := record.Attachment.Filename
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
		langMap[record.FromLang] = true
		langMap[record.ToLang] = true
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
