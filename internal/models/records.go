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

// RecordsData records.json 的结构
type RecordsData struct {
	Data []RecordItem `json:"data"`
}

// RecordItem 单个记录项
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

// Attachment 附件信息
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

// GetLanguagePairs 获取所有可用的语言对
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

// HasLanguagePair 检查是否支持指定的语言对
func (r *RecordsData) HasLanguagePair(fromLang, toLang string) bool {
	for _, record := range r.Data {
		if record.FromLang == fromLang && record.ToLang == toLang {
			return true
		}
	}
	return false
}

// GetVersions 获取指定语言对的所有可用版本
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

// InitRecords 检测默认配置目录下是否存在 records.json
// 存在则解析本地 records.json
// 不存在则写出默认内嵌的 records.json 到配置目录然后解析
func InitRecords() error {
	cfg := config.GetConfig()
	recordsPath := filepath.Join(cfg.ConfigDir, "records.json")

	// 检查文件是否存在
	if _, err := os.Stat(recordsPath); os.IsNotExist(err) {
		// 不存在，写出内嵌的 records.json
		logger.Info("Initializing records.json from embedded data")
		if err := os.MkdirAll(cfg.ConfigDir, 0755); err != nil {
			return fmt.Errorf("Failed to create config directory: %w", err)
		}
		if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
			return fmt.Errorf("Failed to write records.json: %w", err)
		}
	}

	// 解析本地 records.json
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

// DownloadRecords 更新 records.json，从远程下载 records.json 到配置目录
// 然后解析本地 records.json
func DownloadRecords() error {
	cfg := config.GetConfig()

	logger.Info("Updating records.json from remote")
	// 下载 records.json
	d := downloader.New(cfg.ConfigDir)
	if err := d.Download(RecordsUrl, RecordsFileName, &downloader.DownloadOptions{
		Overwrite: true,
	}); err != nil {
		return fmt.Errorf("Failed to download records.json: %w", err)
	}

	// 解析本地 records.json
	return InitRecords()
}

// DownloadModel 解析 records.json，找到对应的模型属性
// 检查配置目录下是否存在对应的模型文件，可通过 sha256 校验
// 不存在则下载到语言对子目录
// 参数：toLang 目标语言，fromLang 源语言，version 模型版本
// records 里同一个 fromLang 和 toLang 会存在多个 version 的模型
// 需要根据 version 下载对应的模型，未指定 version 则下载最新版本
func DownloadModel(toLang string, fromLang string, version string) error {
	// 确保 records 已加载
	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return err
		}
	}

	// 找到匹配的模型记录
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

	// 如果未指定版本，找最新版本
	targetRecords := matchedRecords
	if version == "" {
		// 按 fileType 分组，每组找最新版本
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

	// 构建语言对子目录
	cfg := config.GetConfig()
	langPairDir := filepath.Join(cfg.ModelDir, fmt.Sprintf("%s_%s", fromLang, toLang))

	// 创建语言对子目录
	if err := os.MkdirAll(langPairDir, 0755); err != nil {
		return fmt.Errorf("Failed to create language pair directory: %w", err)
	}

	logger.Info("Downloading model files for %s -> %s", fromLang, toLang)
	// 下载所有需要的文件到语言对子目录
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

// GetModelFiles 根据语言对查找模型文件路径
// 返回 map[string]string，key 为文件类型：model, lex, vocab_src, vocab_trg
// 支持单个词表文件同时用于源语言和目标语言
func GetModelFiles(modelDir, fromLang, toLang string) (map[string]string, error) {
	// 确保 records 已加载
	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return nil, fmt.Errorf("failed to init records: %w", err)
		}
	}

	// 构建语言对子目录
	langPairDir := filepath.Join(modelDir, fmt.Sprintf("%s_%s", fromLang, toLang))

	files := make(map[string]string)
	fileTypeMap := make(map[string]string) // fileType -> fullPath

	// 从 records 中查找匹配的文件
	for _, record := range GlobalRecords.Data {
		if record.FromLang == fromLang && record.ToLang == toLang {
			filename := record.Attachment.Filename
			fullPath := filepath.Join(langPairDir, filename)

			// 检查文件是否存在
			if _, err := os.Stat(fullPath); err == nil {
				fileTypeMap[record.FileType] = fullPath
			}
		}
	}

	// 映射 fileType 到所需的 key
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

	// 处理词表文件：可能是单个 vocab 文件或分开的 srcvocab/trgvocab
	// 优先查找 vocab（单个词表文件）
	if vocabPath, ok := fileTypeMap["vocab"]; ok {
		// 单个词表文件同时用于源语言和目标语言
		files["vocab_src"] = vocabPath
		files["vocab_trg"] = vocabPath
	} else {
		// 查找分开的词表文件
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

// IsModelDownloaded 检查指定语言对的模型是否已下载
func IsModelDownloaded(modelDir, fromLang, toLang string) bool {
	_, err := GetModelFiles(modelDir, fromLang, toLang)
	return err == nil
}

// GetSupportedLanguages 获取所有支持的语言列表
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

// ValidateLanguagePair 验证语言对是否有效
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
