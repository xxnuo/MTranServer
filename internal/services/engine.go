package services

import (
	"context"
	"fmt"
	"path/filepath"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/utils"
)

// EngineInfo 引擎信息
type EngineInfo struct {
	Manager   *manager.Manager
	LastUsed  time.Time
	FromLang  string
	ToLang    string
	stopTimer *time.Timer
	mu        sync.Mutex
}

var (
	// 存储已加载的翻译引擎 key: "fromLang-toLang"
	engines = make(map[string]*EngineInfo)
	engMu   sync.RWMutex
)

// resetIdleTimer 重置空闲计时器
func (ei *EngineInfo) resetIdleTimer() {
	ei.mu.Lock()
	defer ei.mu.Unlock()

	ei.LastUsed = time.Now()

	// 停止旧的计时器
	if ei.stopTimer != nil {
		ei.stopTimer.Stop()
	}

	// 从配置获取超时时间
	cfg := config.GetConfig()
	timeout := time.Duration(cfg.WorkerIdleTimeout) * time.Second

	// 创建新的计时器
	ei.stopTimer = time.AfterFunc(timeout, func() {
		key := fmt.Sprintf("%s-%s", ei.FromLang, ei.ToLang)
		logger.Info("Engine %s idle timeout, stopping...", key)

		engMu.Lock()
		defer engMu.Unlock()

		if info, ok := engines[key]; ok {
			if err := info.Manager.Cleanup(); err != nil {
				logger.Error("Failed to cleanup engine %s: %v", key, err)
			}
			delete(engines, key)
			logger.Info("Engine %s stopped due to idle timeout", key)
		}
	})
}

// getOrCreateSingleEngine 获取或创建单个翻译引擎（内部函数）
// 一个 worker 只对应一个语言方向的翻译（fromLang -> toLang）
func getOrCreateSingleEngine(fromLang, toLang string) (*manager.Manager, error) {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)

	// 检查是否已存在
	engMu.RLock()
	if info, ok := engines[key]; ok {
		if info.Manager.IsRunning() {
			engMu.RUnlock()
			// 更新最后使用时间并重置空闲计时器
			info.resetIdleTimer()
			return info.Manager, nil
		}
	}
	engMu.RUnlock()

	// 创建新引擎
	engMu.Lock()
	defer engMu.Unlock()

	// 再次检查（双重检查锁定）
	if info, ok := engines[key]; ok {
		if info.Manager.IsRunning() {
			info.resetIdleTimer()
			return info.Manager, nil
		}
	}

	logger.Info("Creating new engine for %s -> %s", fromLang, toLang)

	// 下载模型（如果需要）
	cfg := config.GetConfig()
	if cfg.EnableOfflineMode {
		logger.Info("Offline mode enabled, skipping model download")
	} else {
		logger.Info("Downloading model for %s -> %s", fromLang, toLang)
		if err := models.DownloadModel(toLang, fromLang, ""); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	// 查找模型文件
	modelFiles, err := models.GetModelFiles(cfg.ModelDir, fromLang, toLang)
	if err != nil {
		return nil, fmt.Errorf("failed to find model files: %w", err)
	}

	// 创建 Worker，分配独立端口
	port, err := utils.GetFreePort()
	if err != nil {
		return nil, fmt.Errorf("failed to allocate port: %w", err)
	}
	args := manager.NewWorkerArgs()
	args.Port = port

	// WorkDir 设置为语言对子目录
	langPairDir := filepath.Join(cfg.ModelDir, fmt.Sprintf("%s_%s", fromLang, toLang))
	args.WorkDir = langPairDir

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

	// 创建引擎信息并设置空闲计时器
	info := &EngineInfo{
		Manager:  m,
		LastUsed: time.Now(),
		FromLang: fromLang,
		ToLang:   toLang,
	}
	info.resetIdleTimer()

	engines[key] = info
	logger.Info("Engine created successfully for %s -> %s on port %d", fromLang, toLang, port)

	return m, nil
}

// needsPivotTranslation 检查是否需要通过英语中转
func needsPivotTranslation(fromLang, toLang string) bool {
	// 如果源语言或目标语言是英语，不需要中转
	if fromLang == "en" || toLang == "en" {
		return false
	}

	// 检查是否存在直接的语言对
	if models.GlobalRecords != nil && models.GlobalRecords.HasLanguagePair(fromLang, toLang) {
		return false
	}

	// 需要通过英语中转
	return true
}

// GetOrCreateEngine 获取或创建翻译引擎
// 如果需要跨英语翻译（如 zh-Hans -> ja），返回第一步的引擎（zh-Hans -> en）
// 调用者需要使用 TranslateWithPivot 来完成完整的翻译
func GetOrCreateEngine(fromLang, toLang string) (*manager.Manager, error) {
	// 如果不需要中转，直接创建单个引擎
	if !needsPivotTranslation(fromLang, toLang) {
		return getOrCreateSingleEngine(fromLang, toLang)
	}

	// 需要中转，返回第一步的引擎
	logger.Debug("Translation %s -> %s requires pivot through English", fromLang, toLang)
	return getOrCreateSingleEngine(fromLang, "en")
}

// TranslateWithPivot 处理可能需要中转的翻译
// 如果需要中转（如 zh-Hans -> ja），会自动创建两个引擎并执行两步翻译
// 支持 auto 模式：自动检测源语言
func TranslateWithPivot(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	// 处理 auto 模式：自动检测源语言
	if fromLang == "auto" {
		detected := DetectLanguage(text)
		if detected == "" {
			return "", fmt.Errorf("failed to detect source language")
		}
		logger.Debug("Auto-detected source language: %s", detected)
		fromLang = detected
	}

	// 如果源语言和目标语言相同，直接返回原文
	if fromLang == toLang {
		logger.Debug("Source and target languages are the same (%s), returning original text", fromLang)
		return text, nil
	}

	// 如果不需要中转，直接翻译
	if !needsPivotTranslation(fromLang, toLang) {
		m, err := getOrCreateSingleEngine(fromLang, toLang)
		if err != nil {
			return "", err
		}
		if isHTML {
			return m.TranslateHTML(ctx, text)
		}
		return m.Translate(ctx, text)
	}

	// 需要中转：第一步 fromLang -> en
	logger.Debug("Step 1: Translating %s -> en", fromLang)
	m1, err := getOrCreateSingleEngine(fromLang, "en")
	if err != nil {
		return "", fmt.Errorf("failed to create first engine (%s -> en): %w", fromLang, err)
	}

	var intermediateText string
	if isHTML {
		intermediateText, err = m1.TranslateHTML(ctx, text)
	} else {
		intermediateText, err = m1.Translate(ctx, text)
	}
	if err != nil {
		return "", fmt.Errorf("failed in first step (%s -> en): %w", fromLang, err)
	}

	// 第二步 en -> toLang
	logger.Debug("Step 2: Translating en -> %s", toLang)
	m2, err := getOrCreateSingleEngine("en", toLang)
	if err != nil {
		return "", fmt.Errorf("failed to create second engine (en -> %s): %w", toLang, err)
	}

	var finalText string
	if isHTML {
		finalText, err = m2.TranslateHTML(ctx, intermediateText)
	} else {
		finalText, err = m2.Translate(ctx, intermediateText)
	}
	if err != nil {
		return "", fmt.Errorf("failed in second step (en -> %s): %w", toLang, err)
	}

	return finalText, nil
}

// CleanupAllEngines 清理所有翻译引擎
func CleanupAllEngines() {
	engMu.Lock()
	defer engMu.Unlock()

	if len(engines) == 0 {
		logger.Debug("No engines to cleanup")
		return
	}

	logger.Info("Cleaning up %d engine(s)...", len(engines))

	// 使用 WaitGroup 并发清理所有引擎以加快关闭速度
	var wg sync.WaitGroup
	for key, info := range engines {
		wg.Add(1)
		go func(k string, ei *EngineInfo) {
			defer wg.Done()
			defer func() {
				if r := recover(); r != nil {
					logger.Error("Panic while cleaning up engine %s: %v", k, r)
				}
			}()

			logger.Debug("Stopping engine: %s", k)

			// 停止空闲计时器
			ei.mu.Lock()
			if ei.stopTimer != nil {
				ei.stopTimer.Stop()
			}
			ei.mu.Unlock()

			// 清理 Manager
			if err := ei.Manager.Cleanup(); err != nil {
				logger.Error("Failed to cleanup engine %s: %v", k, err)
			} else {
				logger.Debug("Engine %s cleaned up successfully", k)
			}
		}(key, info)
	}

	// 等待所有清理完成，最多等待 15 秒
	done := make(chan struct{})
	go func() {
		wg.Wait()
		close(done)
	}()

	select {
	case <-done:
		logger.Info("All engines cleaned up successfully")
	case <-time.After(15 * time.Second):
		logger.Warn("Engine cleanup timeout after 15 seconds")
	}

	// 清空 engines map
	engines = make(map[string]*EngineInfo)
}
