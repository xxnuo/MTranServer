package manager

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"sync"
	"syscall"
	"time"

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
	ModelDir        string
	ModelPath       string
	LexicalPath     string
	VocabPaths      []string
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
	cmd        *exec.Cmd
	id         string
	binaryPath string
	mu         sync.RWMutex
	logMu      sync.RWMutex
	logs       []string
	maxLogs    int
	done       chan struct{}
	wg         sync.WaitGroup
	running    bool
	pid        int
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
		id:         workerID,
		binaryPath: binaryPath,
		logs:       make([]string, 0, maxLogLines),
		maxLogs:    maxLogLines,
		done:       make(chan struct{}),
		running:    false,
		pid:        0,
	}

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

	if w.args.ModelDir != "" {
		args = append(args, "--model-dir", w.args.ModelDir)
	} else if w.args.ModelPath != "" {
		args = append(args, "--model-path", w.args.ModelPath)
		if w.args.LexicalPath != "" {
			args = append(args, "--lexical-shortlist-path", w.args.LexicalPath)
		}
		for _, vocabPath := range w.args.VocabPaths {
			args = append(args, "--vocabulary-path", vocabPath)
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

	if w.running && w.cmd != nil && w.cmd.Process != nil {
		return fmt.Errorf("worker already running")
	}

	if _, err := os.Stat(w.binaryPath); err != nil {
		return fmt.Errorf("worker binary not found at %s: %w", w.binaryPath, err)
	}

	args := w.buildArgs()

	logger.Debug("Starting worker %s on port %d", w.id, w.args.Port)

	cmd := exec.Command(w.binaryPath, args...)
	cmd.Dir = w.args.WorkDir

	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		return fmt.Errorf("failed to create stdout pipe: %w", err)
	}
	stderrPipe, err := cmd.StderrPipe()
	if err != nil {
		return fmt.Errorf("failed to create stderr pipe: %w", err)
	}

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start worker: %w", err)
	}

	w.cmd = cmd
	w.running = true
	w.pid = cmd.Process.Pid

	w.wg.Add(2)
	go w.collectLogs(stdoutPipe, "INFO")
	go w.collectLogs(stderrPipe, "ERROR")

	logger.Debug("Worker %s started with PID %d", w.id, w.pid)
	return nil
}

func (w *Worker) Stop() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.running || w.cmd == nil || w.cmd.Process == nil {
		return fmt.Errorf("worker not running")
	}

	logger.Debug("Stopping worker %s", w.id)

	if err := w.cmd.Process.Signal(syscall.SIGTERM); err != nil {
		logger.Warn("Failed to send SIGTERM to worker: %v", err)
	}

	timeout := time.After(10 * time.Second)
	done := make(chan error, 1)

	go func() {
		done <- w.cmd.Wait()
	}()

	select {
	case <-timeout:
		logger.Warn("Worker %s stop timeout, forcing kill", w.id)
		if err := w.cmd.Process.Kill(); err != nil {
			logger.Warn("Failed to kill worker: %v", err)
		}
		<-done
		w.running = false
		w.pid = 0
		return fmt.Errorf("worker stop timeout, forced kill")
	case err := <-done:
		w.running = false
		w.pid = 0
		logger.Debug("Worker %s stopped", w.id)
		if err != nil && err.Error() != "signal: terminated" && err.Error() != "signal: killed" {
			return err
		}
		return nil
	}
}

func (w *Worker) Restart() error {
	if err := w.Stop(); err != nil {
		logger.Warn("Failed to stop worker during restart: %v", err)
	}

	time.Sleep(500 * time.Millisecond)

	return w.Start()
}

func (w *Worker) Status() string {
	w.mu.RLock()
	defer w.mu.RUnlock()

	if !w.running || w.cmd == nil || w.cmd.Process == nil {
		return "not_started"
	}

	return "running"
}

func (w *Worker) Logs() []string {
	w.logMu.RLock()
	defer w.logMu.RUnlock()

	logsCopy := make([]string, len(w.logs))
	copy(logsCopy, w.logs)
	return logsCopy
}

func (w *Worker) collectLogs(pipe io.ReadCloser, logType string) {
	defer w.wg.Done()
	defer pipe.Close()

	scanner := bufio.NewScanner(pipe)
	for scanner.Scan() {
		select {
		case <-w.done:
			return
		default:
			w.logMu.Lock()
			logLine := fmt.Sprintf("[%s] [%s] %s",
				time.Now().Format("2006-01-02 15:04:05"), logType, scanner.Text())
			w.logs = append(w.logs, logLine)
			if len(w.logs) > w.maxLogs {
				w.logs = w.logs[len(w.logs)-w.maxLogs:]
			}
			w.logMu.Unlock()
		}
	}
}

func (w *Worker) IsRunning() bool {
	return w.Status() == "running"
}

func (w *Worker) Signal(sig syscall.Signal) error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.running || w.cmd == nil || w.cmd.Process == nil {
		return fmt.Errorf("worker not found")
	}

	return w.cmd.Process.Signal(sig)
}

func (w *Worker) Cleanup() error {
	var errs []error

	w.mu.Lock()
	if w.running && w.cmd != nil && w.cmd.Process != nil {
		logger.Debug("Stopping worker %s during cleanup", w.id)
		if err := w.cmd.Process.Signal(syscall.SIGTERM); err != nil {
			logger.Warn("Failed to send SIGTERM during cleanup: %v", err)
		}

		timeout := time.After(5 * time.Second)
		done := make(chan error, 1)

		go func() {
			done <- w.cmd.Wait()
		}()

		select {
		case <-timeout:
			logger.Warn("Worker %s cleanup timeout, forcing kill", w.id)
			if err := w.cmd.Process.Kill(); err != nil {
				logger.Warn("Failed to kill worker during cleanup: %v", err)
				errs = append(errs, fmt.Errorf("failed to kill worker: %w", err))
			}
			<-done
		case err := <-done:
			if err != nil && err.Error() != "signal: terminated" && err.Error() != "signal: killed" {
				errs = append(errs, fmt.Errorf("worker exited with error: %w", err))
			}
		}

		w.running = false
		w.pid = 0
	}

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
