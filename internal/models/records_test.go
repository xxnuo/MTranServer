package models_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/xxnuo/MTranServer/data"
	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/models"
)

func TestInitRecords(t *testing.T) {

	oldConfig := config.GlobalConfig
	defer func() { config.GlobalConfig = oldConfig }()

	tmpDir := t.TempDir()

	config.GlobalConfig = &config.Config{
		ConfigDir: tmpDir,
		ModelDir:  filepath.Join(tmpDir, "models"),
	}

	models.GlobalRecords = nil

	err := models.InitRecords()
	if err != nil {
		t.Fatalf("initRecords() error = %v", err)
	}

	recordsPath := filepath.Join(tmpDir, "records.json")
	if _, err := os.Stat(recordsPath); os.IsNotExist(err) {
		t.Fatal("records.json was not created")
	}

	if models.GlobalRecords == nil {
		t.Fatal("GlobalRecords was not set")
	}

	if len(models.GlobalRecords.Data) == 0 {
		t.Fatal("GlobalRecords.Data is empty")
	}

	models.GlobalRecords = nil
	err = models.InitRecords()
	if err != nil {
		t.Fatalf("initRecords() second call error = %v", err)
	}

	if models.GlobalRecords == nil || len(models.GlobalRecords.Data) == 0 {
		t.Fatal("Failed to load from existing records.json")
	}
}

func TestRecordsDataStructure(t *testing.T) {

	var records models.RecordsData
	err := json.Unmarshal(data.RecordsJson, &records)
	if err != nil {
		t.Fatalf("Failed to unmarshal embedded records.json: %v", err)
	}

	if len(records.Data) == 0 {
		t.Fatal("No records found in embedded data")
	}

	firstRecord := records.Data[0]
	if firstRecord.Name == "" {
		t.Error("Record name is empty")
	}
	if firstRecord.ToLang == "" {
		t.Error("Record toLang is empty")
	}
	if firstRecord.FromLang == "" {
		t.Error("Record fromLang is empty")
	}
	if firstRecord.Version == "" {
		t.Error("Record version is empty")
	}
	if firstRecord.FileType == "" {
		t.Error("Record fileType is empty")
	}
	if firstRecord.Attachment.Filename == "" {
		t.Error("Attachment filename is empty")
	}
	if firstRecord.Attachment.Location == "" {
		t.Error("Attachment location is empty")
	}
	if firstRecord.Attachment.Hash == "" {
		t.Error("Attachment hash is empty")
	}
}

func TestFindModelRecords(t *testing.T) {

	var records models.RecordsData
	err := json.Unmarshal(data.RecordsJson, &records)
	if err != nil {
		t.Fatalf("Failed to unmarshal records: %v", err)
	}

	tests := []struct {
		name     string
		toLang   string
		fromLang string
		version  string
		wantErr  bool
		minCount int
	}{
		{
			name:     "find en to pl models",
			toLang:   "pl",
			fromLang: "en",
			version:  "",
			wantErr:  false,
			minCount: 1,
		},
		{
			name:     "find en to de models",
			toLang:   "de",
			fromLang: "en",
			version:  "",
			wantErr:  false,
			minCount: 1,
		},
		{
			name:     "find specific version",
			toLang:   "pl",
			fromLang: "en",
			version:  "2.1",
			wantErr:  false,
			minCount: 1,
		},
		{
			name:     "non-existent language pair",
			toLang:   "zz",
			fromLang: "en",
			version:  "",
			wantErr:  true,
			minCount: 0,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			var matchedRecords []models.RecordItem
			for _, record := range records.Data {
				if record.ToLang == tt.toLang && record.FromLang == tt.fromLang {
					if tt.version == "" || record.Version == tt.version {
						matchedRecords = append(matchedRecords, record)
					}
				}
			}

			hasError := len(matchedRecords) == 0
			if hasError != tt.wantErr {
				t.Errorf("Expected error: %v, got: %v (found %d records)", tt.wantErr, hasError, len(matchedRecords))
			}

			if !tt.wantErr && len(matchedRecords) < tt.minCount {
				t.Errorf("Expected at least %d records, got %d", tt.minCount, len(matchedRecords))
			}
		})
	}
}

func TestVersionGrouping(t *testing.T) {

	var records models.RecordsData
	err := json.Unmarshal(data.RecordsJson, &records)
	if err != nil {
		t.Fatalf("Failed to unmarshal records: %v", err)
	}

	var matchedRecords []models.RecordItem
	for _, record := range records.Data {
		if record.ToLang == "pl" && record.FromLang == "en" {
			matchedRecords = append(matchedRecords, record)
		}
	}

	if len(matchedRecords) == 0 {
		t.Skip("No en->pl records found for testing")
	}

	fileTypeMap := make(map[string][]models.RecordItem)
	for _, record := range matchedRecords {
		fileTypeMap[record.FileType] = append(fileTypeMap[record.FileType], record)
	}

	expectedTypes := []string{"model", "vocab", "lex"}
	for _, fileType := range expectedTypes {
		if records, exists := fileTypeMap[fileType]; !exists || len(records) == 0 {
			t.Logf("Warning: No %s files found for en->pl", fileType)
		}
	}

	for fileType, fileRecords := range fileTypeMap {
		if len(fileRecords) > 1 {
			t.Logf("FileType %s has %d versions", fileType, len(fileRecords))
		}
	}
}

func TestDownloadRecords(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping download test in short mode")
	}

	oldConfig := config.GlobalConfig
	defer func() { config.GlobalConfig = oldConfig }()

	tmpDir := t.TempDir()

	config.GlobalConfig = &config.Config{
		ConfigDir: tmpDir,
		ModelDir:  filepath.Join(tmpDir, "models"),
	}

	models.GlobalRecords = nil

	err := models.DownloadRecords()
	if err != nil {
		t.Fatalf("downloadRecords() error = %v", err)
	}

	recordsPath := filepath.Join(tmpDir, "records.json")
	if _, err := os.Stat(recordsPath); os.IsNotExist(err) {
		t.Fatal("records.json was not downloaded")
	}

	if models.GlobalRecords == nil {
		t.Fatal("GlobalRecords was not set after download")
	}

	if len(models.GlobalRecords.Data) == 0 {
		t.Fatal("GlobalRecords.Data is empty after download")
	}
}

func TestRealDownloadModel(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping model download test in short mode")
	}

	t.Log("This test is real world test.")

	err := models.InitRecords()
	if err != nil {
		t.Fatalf("initRecords() error = %v", err)
	}

	err = models.DownloadModel("ja", "en", "")
	if err != nil {
		t.Fatalf("downloadModel() error = %v", err)
	}

	modelDir := config.GetConfig().ModelDir
	files, err := os.ReadDir(modelDir)
	if err != nil {
		t.Fatalf("Failed to read model directory: %v", err)
	}

	if len(files) == 0 {
		t.Fatal("No model files were downloaded")
	}

	downloadedFiles := make(map[string]bool)
	for _, file := range files {
		downloadedFiles[file.Name()] = true
	}

	expectedFileTypes := []string{"model", "vocab", "lex", "trgvocab", "srcvocab"}
	for _, record := range models.GlobalRecords.Data {
		if record.ToLang == "ja" && record.FromLang == "en" {
			if !downloadedFiles[record.Attachment.Filename] {
				t.Errorf("Expected file %s was not downloaded", record.Attachment.Filename)
			}

			found := false
			for _, ft := range expectedFileTypes {
				if record.FileType == ft {
					found = true
					break
				}
			}
			if !found {
				t.Logf("Unexpected fileType: %s", record.FileType)
			}
		}
	}
}

func TestDownloadModelLatestVersion(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping model download test in short mode")
	}

	oldConfig := config.GlobalConfig
	defer func() { config.GlobalConfig = oldConfig }()

	tmpDir := t.TempDir()

	config.GlobalConfig = &config.Config{
		ConfigDir: tmpDir,
		ModelDir:  filepath.Join(tmpDir, "models"),
	}

	models.GlobalRecords = nil

	err := models.InitRecords()
	if err != nil {
		t.Fatalf("initRecords() error = %v", err)
	}

	err = models.DownloadModel("de", "en", "")
	if err != nil {
		t.Fatalf("downloadModel() error = %v", err)
	}

	modelDir := filepath.Join(tmpDir, "models")
	files, err := os.ReadDir(modelDir)
	if err != nil {
		t.Fatalf("Failed to read model directory: %v", err)
	}

	if len(files) == 0 {
		t.Fatal("No model files were downloaded")
	}
}

func TestDownloadModelNonExistent(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping test in short mode")
	}

	oldConfig := config.GlobalConfig
	defer func() { config.GlobalConfig = oldConfig }()

	tmpDir := t.TempDir()

	config.GlobalConfig = &config.Config{
		ConfigDir: tmpDir,
		ModelDir:  filepath.Join(tmpDir, "models"),
	}

	models.GlobalRecords = nil

	err := models.InitRecords()
	if err != nil {
		t.Fatalf("initRecords() error = %v", err)
	}

	err = models.DownloadModel("zz", "yy", "")
	if err == nil {
		t.Fatal("Expected error for non-existent language pair, got nil")
	}
}
