package models

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"github.com/xxnuo/MTranServer/data"
	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/utils"
	"github.com/xxnuo/MTranServer/internal/utils/downloader"
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

// InitRecords 检测默认配置目录下是否存在 records.json
// 存在则解析本地 records.json
// 不存在则写出默认内嵌的 records.json 到配置目录然后解析
func InitRecords() error {
	cfg := config.GetConfig()
	recordsPath := filepath.Join(cfg.ConfigDir, "records.json")

	// 检查文件是否存在
	if _, err := os.Stat(recordsPath); os.IsNotExist(err) {
		// 不存在，写出内嵌的 records.json
		if err := os.MkdirAll(cfg.ConfigDir, 0755); err != nil {
			return fmt.Errorf("Failed to create config directory: %w", err)
		}
		if err := os.WriteFile(recordsPath, data.RecordsJson, 0644); err != nil {
			return fmt.Errorf("Failed to write records.json: %w", err)
		}
	}

	// 解析本地 records.json
	fileData, err := os.ReadFile(recordsPath)
	if err != nil {
		return fmt.Errorf("Failed to read records.json: %w", err)
	}

	var records RecordsData
	if err := json.Unmarshal(fileData, &records); err != nil {
		return fmt.Errorf("Failed to parse records.json: %w", err)
	}

	GlobalRecords = &records
	return nil
}

// DownloadRecords 更新 records.json，从远程下载 records.json 到配置目录
// 然后解析本地 records.json
func DownloadRecords() error {
	cfg := config.GetConfig()

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
// 不存在则下载到配置目录
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

	// 下载所有需要的文件
	cfg := config.GetConfig()
	d := downloader.New(cfg.ModelDir)

	for _, record := range targetRecords {
		filename := record.Attachment.Filename
		fileUrl := AttachmentsBaseUrl + "/" + record.Attachment.Location
		sha256sum := record.Attachment.Hash

		if err := d.Download(fileUrl, filename, &downloader.DownloadOptions{
			SHA256:    sha256sum,
			Overwrite: false,
		}); err != nil {
			return fmt.Errorf("Failed to download %s: %w", filename, err)
		}
	}

	return nil
}

// GetModelFiles 根据语言对查找模型文件路径
// 返回 map[string]string，key 为文件类型：model, lex, vocab_src, vocab_trg
func GetModelFiles(modelDir, fromLang, toLang string) (map[string]string, error) {
	// 确保 records 已加载
	if GlobalRecords == nil {
		if err := InitRecords(); err != nil {
			return nil, fmt.Errorf("failed to init records: %w", err)
		}
	}

	files := make(map[string]string)
	fileTypeMap := make(map[string]string) // fileType -> fullPath

	// 从 records 中查找匹配的文件
	for _, record := range GlobalRecords.Data {
		if record.FromLang == fromLang && record.ToLang == toLang {
			filename := record.Attachment.Filename
			fullPath := filepath.Join(modelDir, filename)

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

	// vocab_src 可能是 vocab 或 srcvocab
	if vocabPath, ok := fileTypeMap["vocab"]; ok {
		files["vocab_src"] = vocabPath
	} else if srcvocabPath, ok := fileTypeMap["srcvocab"]; ok {
		files["vocab_src"] = srcvocabPath
	} else {
		return nil, fmt.Errorf("vocab file not found for %s -> %s", fromLang, toLang)
	}

	// trgvocab 可能是 trgvocab 或 vocab（反向）
	if trgvocabPath, ok := fileTypeMap["trgvocab"]; ok {
		files["vocab_trg"] = trgvocabPath
	} else {
		// 尝试查找反向的 vocab
		for _, record := range GlobalRecords.Data {
			if record.FromLang == toLang && record.ToLang == fromLang && record.FileType == "vocab" {
				fullPath := filepath.Join(modelDir, record.Attachment.Filename)
				if _, err := os.Stat(fullPath); err == nil {
					files["vocab_trg"] = fullPath
					break
				}
			}
		}
		if _, ok := files["vocab_trg"]; !ok {
			return nil, fmt.Errorf("trgvocab file not found for %s -> %s", fromLang, toLang)
		}
	}

	return files, nil
}
