package manager

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/ShinyTrinkets/overseer"
	"github.com/xxnuo/MTranServer/bin"
	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/logger"
)

const (
	maxLogLines = 1000
)

var (
	workerBinaryInitialized bool
	workerBinaryMu          sync.Mutex
)

type WorkerArgs struct {
	Host            string
	Port            int
	WorkDir         string
	EnableGRPC      bool
	EnableHTTP      bool
	EnableWebSocket bool
	GRPCUnixSocket  string
	LogLevel        string
	BinaryPath      string
}

func NewWorkerArgs() *WorkerArgs {
	return &WorkerArgs{
		Host:            "127.0.0.1",
		Port:            8988,
		WorkDir:         ".",
		EnableGRPC:      false,
		EnableHTTP:      false,
		EnableWebSocket: true,
		GRPCUnixSocket:  "",
		LogLevel:        "warning",
	}
}

type Worker struct {
	args       *WorkerArgs
	overseer   *overseer.Overseer
	id         string
	binaryPath string
	mu         sync.RWMutex
	logChan    chan *overseer.LogMsg
	stateChan  chan *overseer.ProcessJSON
	logs       []string
	maxLogs    int
	done       chan struct{}
	wg         sync.WaitGroup
}

func NewWorker(args *WorkerArgs) *Worker {
	binaryPath := args.BinaryPath
	if binaryPath == "" {
		cfg := config.GetConfig()
		binaryName := "mtrancore"
		if runtime.GOOS == "windows" {
			binaryName += ".exe"
		}
		binaryPath = filepath.Join(cfg.ConfigDir, "bin", binaryName)
	}

	workerID := fmt.Sprintf("mtran-worker-%d", args.Port)

	w := &Worker{
		args:       args,
		overseer:   overseer.NewOverseer(),
		id:         workerID,
		binaryPath: binaryPath,
		logChan:    make(chan *overseer.LogMsg, 100),
		stateChan:  make(chan *overseer.ProcessJSON, 10),
		logs:       make([]string, 0, maxLogLines),
		maxLogs:    maxLogLines,
		done:       make(chan struct{}),
	}

	w.overseer.WatchLogs(w.logChan)
	w.overseer.WatchState(w.stateChan)

	w.wg.Add(1)
	go w.collectLogs()

	return w
}

func EnsureWorkerBinary(cfg *config.Config) error {
	workerBinaryMu.Lock()
	defer workerBinaryMu.Unlock()

	if workerBinaryInitialized {
		return nil
	}

	binaryName := "mtrancore"
	if runtime.GOOS == "windows" {
		binaryName += ".exe"
	}
	binaryPath := filepath.Join(cfg.ConfigDir, "bin", binaryName)

	if data, err := os.ReadFile(binaryPath); err == nil {

		existingHash := fmt.Sprintf("%x", bin.ComputeHash(data))
		if existingHash == bin.WorkerHash {

			logger.Debug("Worker binary already exists and is up to date")
			workerBinaryInitialized = true
			return nil
		}
		logger.Info("Worker binary hash mismatch, updating...")
	}

	if err := os.MkdirAll(filepath.Dir(binaryPath), 0755); err != nil {
		return fmt.Errorf("failed to create directory for worker binary: %w", err)
	}

	logger.Info("Extracting worker binary to %s", binaryPath)

	if err := os.WriteFile(binaryPath, bin.WorkerBinary, 0755); err != nil {
		return fmt.Errorf("failed to write worker binary: %w", err)
	}

	logger.Info("Worker binary extracted successfully")
	workerBinaryInitialized = true
	return nil
}

func (w *Worker) buildArgs() []string {
	args := []string{
		"--host", w.args.Host,
		"--port", strconv.Itoa(w.args.Port),
		"--log-level", w.args.LogLevel,
	}

	if w.args.WorkDir != "" {
		absWorkDir, err := filepath.Abs(w.args.WorkDir)
		if err == nil {
			args = append(args, "--work-dir", absWorkDir)
		} else {
			args = append(args, "--work-dir", w.args.WorkDir)
		}
	}
	if w.args.EnableGRPC {
		args = append(args, "--enable-grpc", "true")
	} else {
		args = append(args, "--enable-grpc", "false")
	}

	if w.args.EnableHTTP {
		args = append(args, "--enable-http", "true")
	} else {
		args = append(args, "--enable-http", "false")
	}

	if w.args.EnableWebSocket {
		args = append(args, "--enable-websocket", "true")
	} else {
		args = append(args, "--enable-websocket", "false")
	}

	if w.args.GRPCUnixSocket != "" {
		args = append(args, "--grpc-unix-socket", w.args.GRPCUnixSocket)
	}

	return args
}

func (w *Worker) Start() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.overseer.HasProc(w.id) {
		status := w.overseer.Status(w.id)
		if status != nil && status.State == "running" {
			return fmt.Errorf("worker already running")
		}

		w.overseer.Remove(w.id)
	}

	if _, err := os.Stat(w.binaryPath); err != nil {
		return fmt.Errorf("worker binary not found at %s: %w", w.binaryPath, err)
	}

	args := w.buildArgs()

	logger.Debug("Starting worker %s on port %d", w.id, w.args.Port)
	cmd := w.overseer.Add(w.id, w.binaryPath, args)
	if cmd == nil {
		return fmt.Errorf("failed to add worker to overseer")
	}

	cmd.Dir = w.args.WorkDir
	cmd.DelayStart = 0
	cmd.RetryTimes = 0

	go w.overseer.Supervise(w.id)

	time.Sleep(100 * time.Millisecond)

	logger.Debug("Worker %s started", w.id)
	return nil
}

func (w *Worker) Stop() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	defer func() {
		if r := recover(); r != nil {
			logger.Error("Panic during worker stop: %v", r)
		}
	}()

	if !w.overseer.HasProc(w.id) {
		return fmt.Errorf("worker not found")
	}

	status := w.overseer.Status(w.id)
	if status == nil || status.State != "running" {
		return fmt.Errorf("worker not running")
	}

	logger.Debug("Stopping worker %s", w.id)

	func() {
		defer func() {
			if r := recover(); r != nil {
				logger.Warn("Panic while stopping worker (process may have already exited): %v", r)
			}
		}()

		if !w.overseer.HasProc(w.id) {
			return
		}
		currentStatus := w.overseer.Status(w.id)
		if currentStatus == nil || currentStatus.State != "running" {
			return
		}

		if err := w.overseer.Stop(w.id); err != nil {
			logger.Warn("Failed to stop worker: %v", err)
		}
	}()

	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			func() {
				defer func() {
					if r := recover(); r != nil {
						logger.Warn("Panic while killing worker (process may have already exited): %v", r)
					}
				}()

				if !w.overseer.HasProc(w.id) {
					return
				}
				killStatus := w.overseer.Status(w.id)
				if killStatus == nil || killStatus.State != "running" {
					return
				}

				logger.Warn("Worker %s stop timeout, forcing kill", w.id)
				if err := w.overseer.Signal(w.id, syscall.SIGKILL); err != nil {
					logger.Warn("Failed to kill worker: %v", err)
				}
			}()
			time.Sleep(500 * time.Millisecond)
			for i := 0; i < 10; i++ {
				if !w.overseer.HasProc(w.id) {
					break
				}
				status := w.overseer.Status(w.id)
				if status == nil || status.State != "running" {
					break
				}
				time.Sleep(100 * time.Millisecond)
			}
			return fmt.Errorf("worker stop timeout, forced kill")
		case <-ticker.C:
			if !w.overseer.HasProc(w.id) {
				logger.Debug("Worker %s stopped", w.id)
				return nil
			}
			status := w.overseer.Status(w.id)
			if status == nil || status.State != "running" {
				logger.Debug("Worker %s stopped", w.id)
				return nil
			}
		}
	}
}

func (w *Worker) Restart() error {

	if w.overseer.HasProc(w.id) {
		status := w.overseer.Status(w.id)
		if status != nil && status.State == "running" {
			if err := w.Stop(); err != nil {
				return fmt.Errorf("failed to stop worker: %w", err)
			}
		}

		w.overseer.Remove(w.id)
	}

	time.Sleep(500 * time.Millisecond)

	return w.Start()
}

func (w *Worker) Status() string {
	w.mu.RLock()
	defer w.mu.RUnlock()

	if !w.overseer.HasProc(w.id) {
		return "not_started"
	}

	status := w.overseer.Status(w.id)
	if status == nil {
		return "unknown"
	}

	return status.State
}

func (w *Worker) GetDetailedStatus() *overseer.ProcessJSON {
	w.mu.RLock()
	defer w.mu.RUnlock()

	if !w.overseer.HasProc(w.id) {
		return nil
	}

	return w.overseer.Status(w.id)
}

func (w *Worker) Logs() []string {
	w.mu.RLock()
	defer w.mu.RUnlock()

	logsCopy := make([]string, len(w.logs))
	copy(logsCopy, w.logs)
	return logsCopy
}

func (w *Worker) collectLogs() {
	defer w.wg.Done()

	for {
		select {
		case <-w.done:

			for {
				select {
				case msg, ok := <-w.logChan:
					if !ok {
						return
					}
					w.mu.Lock()
					logType := "INFO"
					if msg.Type == 1 {
						logType = "ERROR"
					}
					logLine := fmt.Sprintf("[%s] [%s] %s",
						time.Now().Format("2006-01-02 15:04:05"), logType, msg.Text)
					w.logs = append(w.logs, logLine)
					if len(w.logs) > w.maxLogs {
						w.logs = w.logs[len(w.logs)-w.maxLogs:]
					}
					w.mu.Unlock()
				case state, ok := <-w.stateChan:
					if !ok {
						return
					}
					w.mu.Lock()
					stateLog := fmt.Sprintf("[%s] Worker state: %s (PID: %d)",
						time.Now().Format("2006-01-02 15:04:05"), state.State, state.PID)
					w.logs = append(w.logs, stateLog)
					if len(w.logs) > w.maxLogs {
						w.logs = w.logs[len(w.logs)-w.maxLogs:]
					}
					w.mu.Unlock()
				default:
					return
				}
			}

		case msg, ok := <-w.logChan:
			if !ok {
				return
			}
			w.mu.Lock()

			logType := "INFO"
			if msg.Type == 1 {
				logType = "ERROR"
			}
			logLine := fmt.Sprintf("[%s] [%s] %s",
				time.Now().Format("2006-01-02 15:04:05"), logType, msg.Text)
			w.logs = append(w.logs, logLine)

			if len(w.logs) > w.maxLogs {
				w.logs = w.logs[len(w.logs)-w.maxLogs:]
			}
			w.mu.Unlock()

		case state, ok := <-w.stateChan:
			if !ok {
				return
			}
			w.mu.Lock()

			stateLog := fmt.Sprintf("[%s] Worker state: %s (PID: %d)",
				time.Now().Format("2006-01-02 15:04:05"), state.State, state.PID)
			w.logs = append(w.logs, stateLog)

			if len(w.logs) > w.maxLogs {
				w.logs = w.logs[len(w.logs)-w.maxLogs:]
			}
			w.mu.Unlock()
		}
	}
}

func (w *Worker) IsRunning() bool {
	return w.Status() == "running"
}

func (w *Worker) Signal(sig syscall.Signal) error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.overseer.HasProc(w.id) {
		return fmt.Errorf("worker not found")
	}

	return w.overseer.Signal(w.id, sig)
}

func (w *Worker) Cleanup() error {
	w.mu.Lock()

	var errs []error

	defer func() {
		if r := recover(); r != nil {
			logger.Error("Panic during worker cleanup: %v", r)
			errs = append(errs, fmt.Errorf("panic during cleanup: %v", r))
		}
	}()

	if w.overseer.HasProc(w.id) {
		status := w.overseer.Status(w.id)
		if status != nil && status.State == "running" {
			func() {
				defer func() {
					if r := recover(); r != nil {
						logger.Warn("Panic while stopping worker (process may have already exited): %v", r)
						errs = append(errs, fmt.Errorf("panic while stopping worker: %v", r))
					}
				}()

				if !w.overseer.HasProc(w.id) {
					return
				}
				currentStatus := w.overseer.Status(w.id)
				if currentStatus == nil || currentStatus.State != "running" {
					return
				}

				if err := w.overseer.Stop(w.id); err != nil {
					errs = append(errs, fmt.Errorf("failed to stop worker gracefully: %w", err))
				}

				timeout := time.After(5 * time.Second)
				ticker := time.NewTicker(100 * time.Millisecond)
				defer ticker.Stop()

			waitLoop:
				for {
					select {
					case <-timeout:
						func() {
							defer func() {
								if r := recover(); r != nil {
									logger.Warn("Panic while killing worker (process may have already exited): %v", r)
									errs = append(errs, fmt.Errorf("panic while killing worker: %v", r))
								}
							}()

							if !w.overseer.HasProc(w.id) {
								return
							}
							killStatus := w.overseer.Status(w.id)
							if killStatus == nil || killStatus.State != "running" {
								return
							}

							if err := w.overseer.Signal(w.id, syscall.SIGKILL); err != nil {
								errs = append(errs, fmt.Errorf("failed to kill worker: %w", err))
							}
						}()
						time.Sleep(500 * time.Millisecond)
						break waitLoop
					case <-ticker.C:
						if !w.overseer.HasProc(w.id) {
							break waitLoop
						}
						status := w.overseer.Status(w.id)
						if status == nil || status.State != "running" {
							break waitLoop
						}
					}
				}
			}()
		}

		for i := 0; i < 20; i++ {
			if !w.overseer.HasProc(w.id) {
				break
			}
			status := w.overseer.Status(w.id)
			if status == nil || status.State != "running" {
				break
			}
			time.Sleep(100 * time.Millisecond)
		}

		if w.overseer.HasProc(w.id) {
			w.overseer.Remove(w.id)
		}
	}

	w.overseer.UnWatchLogs(w.logChan)
	w.overseer.UnWatchState(w.stateChan)

	select {
	case <-w.done:
	default:
		close(w.done)
	}

	w.mu.Unlock()

	w.wg.Wait()

	if len(errs) > 0 {
		return fmt.Errorf("cleanup errors: %v", errs)
	}

	return nil
}
