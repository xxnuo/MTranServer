package services

import (
	"context"
	"fmt"
	"log"
	"path/filepath"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/models"
)

var (
	// 存储已加载的翻译引擎 key: "fromLang-toLang"
	engines = make(map[string]*manager.Manager)
	engMu   sync.RWMutex
)

// GetOrCreateEngine 获取或创建翻译引擎
func GetOrCreateEngine(fromLang, toLang string) (*manager.Manager, error) {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)

	// 检查是否已存在
	engMu.RLock()
	if m, ok := engines[key]; ok {
		if m.IsRunning() {
			engMu.RUnlock()
			return m, nil
		}
	}
	engMu.RUnlock()

	// 创建新引擎
	engMu.Lock()
	defer engMu.Unlock()

	// 再次检查（双重检查锁定）
	if m, ok := engines[key]; ok {
		if m.IsRunning() {
			return m, nil
		}
	}

	log.Printf("Creating new engine for %s -> %s", fromLang, toLang)

	// 下载模型（如果需要）
	cfg := config.GetConfig()
	if cfg.EnableOfflineMode {
		log.Printf("Offline mode enabled, skipping model download")
	} else {
		log.Printf("Downloading model for %s -> %s", fromLang, toLang)
		if err := models.DownloadModel(toLang, fromLang, ""); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	// 查找模型文件
	modelFiles, err := models.GetModelFiles(cfg.ModelDir, fromLang, toLang)
	if err != nil {
		return nil, fmt.Errorf("failed to find model files: %w", err)
	}

	// 创建 Worker
	port := 8988 + len(engines) // 动态分配端口
	args := manager.NewWorkerArgs()
	args.Port = port
	args.WorkDir = cfg.ModelDir

	m := manager.NewManager(args)

	// 启动 Manager
	if err := m.Start(); err != nil {
		return nil, fmt.Errorf("failed to start manager: %w", err)
	}

	// 加载模型
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	// 提取文件名（相对于 WorkDir）
	poweronReq := manager.PoweronRequest{
		ModelPath:            filepath.Base(modelFiles["model"]),
		LexicalShortlistPath: filepath.Base(modelFiles["lex"]),
		VocabularyPaths:      []string{filepath.Base(modelFiles["vocab_src"]), filepath.Base(modelFiles["vocab_trg"])},
	}

	if _, err := m.Poweron(ctx, poweronReq); err != nil {
		m.Cleanup()
		return nil, fmt.Errorf("failed to load model: %w", err)
	}

	// 等待引擎就绪
	for i := 0; i < 30; i++ {
		ready, err := m.Ready(ctx)
		if err == nil && ready {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}

	engines[key] = m
	log.Printf("Engine created successfully for %s -> %s", fromLang, toLang)

	return m, nil
}

// CleanupAllEngines 清理所有翻译引擎
func CleanupAllEngines() {
	engMu.Lock()
	defer engMu.Unlock()

	for key, m := range engines {
		log.Printf("Stopping engine: %s", key)
		if err := m.Cleanup(); err != nil {
			log.Printf("Failed to cleanup engine %s: %v", key, err)
		}
	}
}
