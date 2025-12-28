package services

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/utils"
)

type EngineInfo struct {
	Manager   *manager.Manager
	LastUsed  time.Time
	FromLang  string
	ToLang    string
	stopTimer *time.Timer
	mu        sync.Mutex
}

var (
	engines = make(map[string]*EngineInfo)
	engMu   sync.RWMutex
)

func (ei *EngineInfo) resetIdleTimer() {
	ei.mu.Lock()
	defer ei.mu.Unlock()

	ei.LastUsed = time.Now()

	if ei.stopTimer != nil {
		ei.stopTimer.Stop()
	}

	cfg := config.GetConfig()
	timeout := time.Duration(cfg.WorkerIdleTimeout) * time.Second

	ei.stopTimer = time.AfterFunc(timeout, func() {
		defer func() {
			if r := recover(); r != nil {
				logger.Error("Panic during engine cleanup: %v", r)
			}
		}()

		key := fmt.Sprintf("%s-%s", ei.FromLang, ei.ToLang)
		logger.Info("Engine %s idle timeout, stopping...", key)

		engMu.Lock()
		defer engMu.Unlock()

		if info, ok := engines[key]; ok {
			if info.Manager != nil {
				if err := info.Manager.Cleanup(); err != nil {
					logger.Error("Failed to cleanup engine %s: %v", key, err)
				}
			}
			delete(engines, key)
			logger.Info("Engine %s stopped due to idle timeout", key)
		}
	})
}

func getOrCreateSingleEngine(fromLang, toLang string) (*manager.Manager, error) {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)

	engMu.RLock()
	if info, ok := engines[key]; ok && info != nil && info.Manager != nil {
		if info.Manager.IsRunning() {
			engMu.RUnlock()

			info.resetIdleTimer()
			return info.Manager, nil
		}
	}
	engMu.RUnlock()

	engMu.Lock()
	defer engMu.Unlock()

	if info, ok := engines[key]; ok && info != nil && info.Manager != nil {
		if info.Manager.IsRunning() {
			info.resetIdleTimer()
			return info.Manager, nil
		}
	}

	logger.Info("Creating new engine for %s -> %s", fromLang, toLang)

	cfg := config.GetConfig()
	if cfg.EnableOfflineMode {
		logger.Info("Offline mode enabled, skipping model download")
	} else {
		logger.Info("Downloading model for %s -> %s", fromLang, toLang)
		if err := models.DownloadModel(toLang, fromLang, ""); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	modelFiles, err := models.GetModelFiles(cfg.ModelDir, fromLang, toLang)
	if err != nil {
		return nil, fmt.Errorf("failed to find model files: %w", err)
	}

	port, err := utils.GetFreePort()
	if err != nil {
		return nil, fmt.Errorf("failed to allocate port: %w", err)
	}
	args := manager.NewWorkerArgs()
	args.Port = port
	args.LogLevel = cfg.LogLevel

	langPairDir := filepath.Join(cfg.ModelDir, fmt.Sprintf("%s_%s", fromLang, toLang))
	if err := os.MkdirAll(langPairDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create work directory: %w", err)
	}
	args.WorkDir = langPairDir

	m := manager.NewManager(args)

	if err := m.Start(); err != nil {
		return nil, fmt.Errorf("failed to start manager: %w", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	poweronReq := manager.PoweronRequest{
		ModelPath:            filepath.Base(modelFiles["model"]),
		LexicalShortlistPath: filepath.Base(modelFiles["lex"]),
		VocabularyPaths:      []string{filepath.Base(modelFiles["vocab_src"]), filepath.Base(modelFiles["vocab_trg"])},
	}

	logger.Debug("Poweron request: %+v", poweronReq)
	resp, err := m.Poweron(ctx, poweronReq)
	if err != nil {
		m.Cleanup()
		return nil, fmt.Errorf("failed to load model: %w", err)
	}
	logger.Debug("Poweron response: %+v", resp)

	ready := false
	for i := 0; i < 30; i++ {
		var err error
		ready, err = m.Ready(ctx)
		logger.Debug("Ready check %d: ready=%v, err=%v", i+1, ready, err)
		if err == nil && ready {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}

	if !ready {
		m.Cleanup()
		return nil, fmt.Errorf("engine failed to become ready after poweron")
	}

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

func needsPivotTranslation(fromLang, toLang string) bool {

	if fromLang == "en" || toLang == "en" {
		return false
	}

	if models.GlobalRecords != nil && models.GlobalRecords.HasLanguagePair(fromLang, toLang) {
		return false
	}

	return true
}

func GetOrCreateEngine(fromLang, toLang string) (*manager.Manager, error) {

	if !needsPivotTranslation(fromLang, toLang) {
		return getOrCreateSingleEngine(fromLang, toLang)
	}

	logger.Debug("Translation %s -> %s requires pivot through English", fromLang, toLang)
	return getOrCreateSingleEngine(fromLang, "en")
}

func translateSegment(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	if fromLang == toLang {
		return text, nil
	}

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

	m1, err := getOrCreateSingleEngine(fromLang, "en")
	if err != nil {
		return "", err
	}
	var intermediateText string
	if isHTML {
		intermediateText, err = m1.TranslateHTML(ctx, text)
	} else {
		intermediateText, err = m1.Translate(ctx, text)
	}
	if err != nil {
		return "", err
	}

	m2, err := getOrCreateSingleEngine("en", toLang)
	if err != nil {
		return "", err
	}
	if isHTML {
		return m2.TranslateHTML(ctx, intermediateText)
	}
	return m2.Translate(ctx, intermediateText)
}

func TranslateWithPivot(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	logger.Debug("TranslateWithPivot: %s -> %s, text length: %d, isHTML: %v", fromLang, toLang, len(text), isHTML)

	if fromLang != "auto" && len(text) <= 128 {
		if fromLang == toLang {
			return text, nil
		}
		return translateSingleLanguageText(ctx, fromLang, toLang, text, isHTML)
	}

	segments := DetectMultipleLanguages(text)
	if len(segments) <= 1 {
		var effectiveFromLang string
		if len(segments) == 1 {
			effectiveFromLang = segments[0].Language
		} else if fromLang == "auto" {
			detected := DetectLanguage(text)
			if detected == "" {
				return "", fmt.Errorf("failed to detect source language")
			}
			effectiveFromLang = detected
		} else {
			effectiveFromLang = fromLang
		}
		if effectiveFromLang == toLang {
			return text, nil
		}
		return translateSingleLanguageText(ctx, effectiveFromLang, toLang, text, isHTML)
	}

	logger.Debug("Detected %d language segments", len(segments))
	var result strings.Builder
	lastEnd := 0

	for _, seg := range segments {
		if seg.Start > lastEnd {
			result.WriteString(text[lastEnd:seg.Start])
		}

		if seg.Language == toLang {
			result.WriteString(seg.Text)
		} else {
			translated, err := translateSegment(ctx, seg.Language, toLang, seg.Text, isHTML)
			if err != nil {
				logger.Error("Failed to translate segment: %v", err)
				result.WriteString(seg.Text)
			} else {
				result.WriteString(translated)
			}
		}
		lastEnd = seg.End
	}

	if lastEnd < len(text) {
		result.WriteString(text[lastEnd:])
	}

	return result.String(), nil
}

func translateSingleLanguageText(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	if !needsPivotTranslation(fromLang, toLang) {
		logger.Debug("translateSingleLanguageText: direct translation path")
		m, err := getOrCreateSingleEngine(fromLang, toLang)
		if err != nil {
			logger.Error("translateSingleLanguageText: failed to get engine: %v", err)
			return "", err
		}
		logger.Debug("translateSingleLanguageText: got engine, calling translate")
		var result string
		if isHTML {
			result, err = m.TranslateHTML(ctx, text)
		} else {
			result, err = m.Translate(ctx, text)
		}
		logger.Debug("translateSingleLanguageText: translate returned, err: %v", err)
		if err != nil && isFatalError(err) {
			key := fmt.Sprintf("%s-%s", fromLang, toLang)
			logger.Warn("Fatal error detected for engine %s, recreating...", key)
			engMu.Lock()
			info, ok := engines[key]
			if ok && info != nil && info.Manager == m {
				info.mu.Lock()
				if info.stopTimer != nil {
					info.stopTimer.Stop()
				}
				info.mu.Unlock()
				if info.Manager != nil {
					info.Manager.Cleanup()
				}
				delete(engines, key)
			}
			engMu.Unlock()
			m, err = getOrCreateSingleEngine(fromLang, toLang)
			if err != nil {
				return "", err
			}
			if isHTML {
				return m.TranslateHTML(ctx, text)
			}
			return m.Translate(ctx, text)
		}
		return result, err
	}

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
		if isFatalError(err) {
			key := fmt.Sprintf("%s-en", fromLang)
			logger.Warn("Fatal error detected for engine %s, recreating...", key)
			engMu.Lock()
			info, ok := engines[key]
			if ok && info != nil && info.Manager == m1 {
				info.mu.Lock()
				if info.stopTimer != nil {
					info.stopTimer.Stop()
				}
				info.mu.Unlock()
				if info.Manager != nil {
					info.Manager.Cleanup()
				}
				delete(engines, key)
			}
			engMu.Unlock()
			m1, err = getOrCreateSingleEngine(fromLang, "en")
			if err != nil {
				return "", fmt.Errorf("failed to recreate first engine (%s -> en): %w", fromLang, err)
			}
			if isHTML {
				intermediateText, err = m1.TranslateHTML(ctx, text)
			} else {
				intermediateText, err = m1.Translate(ctx, text)
			}
		}
		if err != nil {
			return "", fmt.Errorf("failed in first step (%s -> en): %w", fromLang, err)
		}
	}

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
		if isFatalError(err) {
			key := fmt.Sprintf("en-%s", toLang)
			logger.Warn("Fatal error detected for engine %s, recreating...", key)
			engMu.Lock()
			info, ok := engines[key]
			if ok && info != nil && info.Manager == m2 {
				info.mu.Lock()
				if info.stopTimer != nil {
					info.stopTimer.Stop()
				}
				info.mu.Unlock()
				if info.Manager != nil {
					info.Manager.Cleanup()
				}
				delete(engines, key)
			}
			engMu.Unlock()
			m2, err = getOrCreateSingleEngine("en", toLang)
			if err != nil {
				return "", fmt.Errorf("failed to recreate second engine (en -> %s): %w", toLang, err)
			}
			if isHTML {
				finalText, err = m2.TranslateHTML(ctx, intermediateText)
			} else {
				finalText, err = m2.Translate(ctx, intermediateText)
			}
		}
		if err != nil {
			return "", fmt.Errorf("failed in second step (en -> %s): %w", toLang, err)
		}
	}

	return finalText, nil
}

func CleanupAllEngines() {
	engMu.Lock()
	defer engMu.Unlock()

	if len(engines) == 0 {
		logger.Debug("No engines to cleanup")
		return
	}

	logger.Info("Cleaning up %d engine(s)...", len(engines))

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

			ei.mu.Lock()
			if ei.stopTimer != nil {
				ei.stopTimer.Stop()
			}
			ei.mu.Unlock()

			if err := ei.Manager.Cleanup(); err != nil {
				logger.Error("Failed to cleanup engine %s: %v", k, err)
			} else {
				logger.Debug("Engine %s cleaned up successfully", k)
			}
		}(key, info)
	}

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

	engines = make(map[string]*EngineInfo)
}

func isFatalError(err error) bool {
	if err == nil {
		return false
	}
	errMsg := err.Error()
	return strings.Contains(errMsg, "module closed") || 
		strings.Contains(errMsg, "exit_code") ||
		strings.Contains(errMsg, "not connected") ||
		strings.Contains(errMsg, "failed to send message") ||
		strings.Contains(errMsg, "failed to read response") ||
		strings.Contains(errMsg, "wasm error") ||
		strings.Contains(errMsg, "invalid table access")
}
