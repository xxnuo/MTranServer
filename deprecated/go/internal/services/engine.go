package services

import (
	"context"
	"errors"
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
	Managers  []*manager.Manager
	LastUsed  time.Time
	FromLang  string
	ToLang    string
	stopTimer *time.Timer
	mu        sync.Mutex
	nextIdx   int
}

var (
	engines = make(map[string]*EngineInfo)
	engMu   sync.RWMutex
)

const (
	workerMemoryMB     = 2048
	reservedMemoryMB   = 4096
)

var ErrInsufficientMemory = errors.New("insufficient memory to create new worker")

func canCreateNewWorker() bool {
	availableMB := getAvailableMemoryMB()
	if availableMB == 0 {
		logger.Debug("Cannot determine available memory, allowing worker creation")
		return true
	}

	requiredMB := uint64(workerMemoryMB + reservedMemoryMB)
	canCreate := availableMB >= requiredMB

	logger.Debug("Memory check: available=%dMB, required=%dMB, canCreate=%v",
		availableMB, requiredMB, canCreate)

	return canCreate
}

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
			for _, m := range info.Managers {
				if m != nil {
					if err := m.Cleanup(); err != nil {
						logger.Error("Failed to cleanup manager: %v", err)
					}
				}
			}
			delete(engines, key)
			logger.Info("Engine %s stopped due to idle timeout", key)
		}
	})
}

func (ei *EngineInfo) getNextManager() *manager.Manager {
	ei.mu.Lock()
	defer ei.mu.Unlock()

	if len(ei.Managers) == 0 {
		return nil
	}

	// Try to find a running manager first
	startIdx := ei.nextIdx
	for i := 0; i < len(ei.Managers); i++ {
		idx := (startIdx + i) % len(ei.Managers)
		m := ei.Managers[idx]
		if m != nil && m.IsRunning() {
			ei.nextIdx = idx + 1 // Start from next one next time
			return m
		}
	}

	// If no running manager found, just return the next one in round-robin fashion
	// (it will likely fail, but that's expected if all are restarting)
	idx := ei.nextIdx % len(ei.Managers)
	ei.nextIdx++
	return ei.Managers[idx]
}

// Helper to get engine info without creating one if not exists
func getEngineInfo(fromLang, toLang string) *EngineInfo {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)
	engMu.RLock()
	defer engMu.RUnlock()
	return engines[key]
}

func getOrCreateSingleEngine(fromLang, toLang string) (*manager.Manager, error) {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)

	engMu.RLock()
	if info, ok := engines[key]; ok && info != nil {
		m := info.getNextManager()
		if m != nil {
			engMu.RUnlock()
			info.resetIdleTimer()
			return m, nil
		}
	}
	engMu.RUnlock()

	engMu.Lock()
	defer engMu.Unlock()

	if info, ok := engines[key]; ok && info != nil {
		m := info.getNextManager()
		if m != nil {
			info.resetIdleTimer()
			return m, nil
		}
	}

	if !canCreateNewWorker() {
		availableMB := getAvailableMemoryMB()
		return nil, fmt.Errorf("%w: available memory %dMB, need at least %dMB",
			ErrInsufficientMemory, availableMB, workerMemoryMB+reservedMemoryMB)
	}

	logger.Info("Creating new engine pool for %s -> %s", fromLang, toLang)

	cfg := config.GetConfig()
	if cfg.EnableOfflineMode {
		logger.Info("Offline mode enabled, skipping model download")
	} else {
		logger.Info("Downloading model for %s -> %s", fromLang, toLang)
		if err := models.DownloadModel(toLang, fromLang, ""); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	langPairDir := filepath.Join(cfg.ModelDir, fmt.Sprintf("%s_%s", fromLang, toLang))
	if err := os.MkdirAll(langPairDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create work directory: %w", err)
	}

	numWorkers := cfg.WorkersPerLanguage
	if numWorkers <= 0 {
		numWorkers = 1
	}

	managers := make([]*manager.Manager, 0, numWorkers)
	for i := 0; i < numWorkers; i++ {
		port, err := utils.GetFreePort()
		if err != nil {
			for _, m := range managers {
				m.Cleanup()
			}
			return nil, fmt.Errorf("failed to allocate port: %w", err)
		}

		args := manager.NewWorkerArgs()
		args.Port = port
		args.LogLevel = cfg.LogLevel
		args.WorkDir = langPairDir
		args.ModelDir = langPairDir

		m := manager.NewManager(args)

		if err := m.Start(); err != nil {
			for _, m := range managers {
				m.Cleanup()
			}
			return nil, fmt.Errorf("failed to start manager: %w", err)
		}

		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		ready := false
		for j := 0; j < 30; j++ {
			var err error
			ready, err = m.Health(ctx)
			logger.Debug("Worker %d health check %d: ready=%v, err=%v", i+1, j+1, ready, err)
			if err == nil && ready {
				break
			}
			time.Sleep(100 * time.Millisecond)
		}
		cancel()

		if !ready {
			m.Cleanup()
			for _, m := range managers {
				m.Cleanup()
			}
			return nil, fmt.Errorf("worker %d failed to become ready", i+1)
		}

		managers = append(managers, m)
		logger.Info("Worker %d/%d created for %s -> %s on port %d", i+1, numWorkers, fromLang, toLang, port)
	}

	info := &EngineInfo{
		Managers: managers,
		LastUsed: time.Now(),
		FromLang: fromLang,
		ToLang:   toLang,
		nextIdx:  0,
	}
	info.resetIdleTimer()

	engines[key] = info
	logger.Info("Engine pool created successfully for %s -> %s with %d workers", fromLang, toLang, numWorkers)

	return managers[0], nil
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
		return translateSingleLanguageText(ctx, fromLang, toLang, text, isHTML)
	}

	// Pivot Translation

	// Step 1: from -> en
	intermediateText, err := translateSingleLanguageText(ctx, fromLang, "en", text, isHTML)
	if err != nil {
		return "", err
	}

	// Step 2: en -> to
	return translateSingleLanguageText(ctx, "en", toLang, intermediateText, isHTML)
}

func TranslateWithPivot(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	logger.Debug("TranslateWithPivot: %s -> %s, text length: %d, isHTML: %v", fromLang, toLang, len(text), isHTML)

	if fromLang != "auto" && len(text) <= 128 {
		if fromLang == toLang {
			return text, nil
		}
		return translateSegment(ctx, fromLang, toLang, text, isHTML)
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
		return translateSegment(ctx, effectiveFromLang, toLang, text, isHTML)
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
	// 1. Get initial manager (will ensure pool is created)
	m, err := getOrCreateSingleEngine(fromLang, toLang)
	if err != nil {
		logger.Error("translateSingleLanguageText: failed to get engine: %v", err)
		return "", err
	}

	// Get engine info to check concurrency/count
	info := getEngineInfo(fromLang, toLang)
	var maxRetries int = 1
	if info != nil {
		maxRetries = len(info.Managers) * 2 // Try twice per manager on average
		if maxRetries < 3 {
			maxRetries = 3
		}
	}

	var lastErr error

	for i := 0; i < maxRetries; i++ {
		// If retrying, get a potentially new manager (load balanced)
		if i > 0 && info != nil {
			m = info.getNextManager()
			if m == nil {
				// Should not happen if pool is alive
				return "", fmt.Errorf("no managers available")
			}
		}

		logger.Debug("translateSingleLanguageText: attempting translation (try %d/%d)", i+1, maxRetries)
		var result string
		if isHTML {
			result, err = m.TranslateHTML(ctx, text)
		} else {
			result, err = m.Translate(ctx, text)
		}

		if err == nil {
			return result, nil
		}

		// Check if error is retryable (worker failure)
		if isConnectionError(err) {
			logger.Debug("Translation attempt %d failed (connection error): %v. Retrying with next manager...", i+1, err)
			lastErr = err
			// Backoff: 500ms, 1000ms, 2000ms...
			backoff := time.Duration(500*(1<<i)) * time.Millisecond
			if backoff > 3*time.Second {
				backoff = 3 * time.Second
			}
			time.Sleep(backoff)
			continue
		}

		// If it's not a connection error (e.g. invalid request), return immediately
		return "", err
	}

	// If all retries failed, fallback to segmented translation if applicable?
	// The original code did that. Let's preserve it if appropriate.
	if lastErr != nil {
		logger.Warn("All translation attempts failed. Last error: %v. Trying segmented translation.", lastErr)
		segResult, segErr := translateWithSegments(ctx, fromLang, toLang, text, isHTML)
		if segErr != nil {
			return "", lastErr // Return the main error
		}
		return segResult, nil
	}

	return "", lastErr
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

			for _, m := range ei.Managers {
				if m != nil {
					if err := m.Cleanup(); err != nil {
						logger.Error("Failed to cleanup manager: %v", err)
					} else {
						logger.Debug("Manager cleaned up successfully")
					}
				}
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

func isConnectionError(err error) bool {
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
		strings.Contains(errMsg, "invalid table access") ||
		strings.Contains(errMsg, "manager not running") ||
		strings.Contains(errMsg, "worker connection failed")
}

func translateWithSegments(ctx context.Context, fromLang, toLang, text string, isHTML bool) (string, error) {
	segments := DetectMultipleLanguages(text)
	if len(segments) <= 1 {
		return "", fmt.Errorf("segmented translation not applicable")
	}

	logger.Debug("Attempting segmented translation with %d segments", len(segments))
	var result strings.Builder
	lastEnd := 0

	for _, seg := range segments {
		if seg.Start > lastEnd {
			result.WriteString(text[lastEnd:seg.Start])
		}

		if seg.Language == toLang {
			result.WriteString(seg.Text)
		} else {
			segFromLang := seg.Language
			if fromLang != "auto" && fromLang != "" {
				segFromLang = fromLang
			}
			translated, err := translateSegment(ctx, segFromLang, toLang, seg.Text, isHTML)
			if err != nil {
				return "", fmt.Errorf("segmented translation failed: %w", err)
			}
			result.WriteString(translated)
		}
		lastEnd = seg.End
	}

	if lastEnd < len(text) {
		result.WriteString(text[lastEnd:])
	}

	return result.String(), nil
}
