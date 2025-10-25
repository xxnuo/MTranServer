package process

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/ShinyTrinkets/overseer"
	"github.com/xxnuo/MTranServer/bin"
)

const (
	maxLogLines = 1000
)

// WorkerArgs 包含工作进程的配置
type WorkerArgs struct {
	Host            string
	Port            int
	WorkDir         string
	EnableGRPC      bool
	EnableHTTP      bool
	EnableWebSocket bool
	GRPCUnixSocket  string
	LogLevel        string
	BinaryPath      string // 写入工作程序二进制文件的路径，如果为空则使用 /tmp
}

// NewWorkerArgs 创建一个新的 WorkerArgs 实例，使用默认值
func NewWorkerArgs() *WorkerArgs {
	return &WorkerArgs{
		Host:            "0.0.0.0",
		Port:            8988,
		WorkDir:         ".",
		EnableGRPC:      false,
		EnableHTTP:      false,
		EnableWebSocket: true,
		GRPCUnixSocket:  "",
		LogLevel:        "info",
	}
}

// Worker 管理使用 overseer 的工作进程
type Worker struct {
	args       *WorkerArgs
	overseer   *overseer.Overseer
	id         string
	binaryPath string // 实际写入二进制文件的路径
	mu         sync.RWMutex
	logChan    chan *overseer.LogMsg
	stateChan  chan *overseer.ProcessJSON
	logs       []string
	maxLogs    int
}

// NewWorker 创建一个新的 Worker 实例
func NewWorker(args *WorkerArgs) *Worker {
	// 确定二进制文件路径
	binaryPath := args.BinaryPath
	if binaryPath == "" {
		binaryPath = "/tmp/mtran-worker"
	}

	// 根据二进制文件路径和端口生成唯一的 worker ID
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
	}

	// 订阅日志和状态变化
	w.overseer.WatchLogs(w.logChan)
	w.overseer.WatchState(w.stateChan)

	// 启动日志收集器
	go w.collectLogs()

	return w
}

// ensureWorkerBinary 提取嵌入的工作程序二进制文件到指定路径
func (w *Worker) ensureWorkerBinary() error {
	// 检查二进制文件是否已存在并且匹配哈希
	if data, err := os.ReadFile(w.binaryPath); err == nil {
		// 二进制文件存在，计算其哈希并比较
		existingHash := fmt.Sprintf("%x", bin.ComputeHash(data))
		if existingHash == bin.WorkerHash {
			// 哈希匹配，二进制文件是最新的
			return nil
		}
	}

	// 确保父目录存在
	if err := os.MkdirAll(filepath.Dir(w.binaryPath), 0755); err != nil {
		return fmt.Errorf("failed to create directory for worker binary: %w", err)
	}

	// 写入嵌入的二进制文件
	if err := os.WriteFile(w.binaryPath, bin.WorkerBinary, 0755); err != nil {
		return fmt.Errorf("failed to write worker binary: %w", err)
	}

	return nil
}

// buildArgs 构建工作程序的命令行参数
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

// Start 启动工作进程
func (w *Worker) Start() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	// 检查是否已经运行
	if w.overseer.HasProc(w.id) {
		status := w.overseer.Status(w.id)
		if status != nil && status.State == "running" {
			return fmt.Errorf("worker already running")
		}
		// 如果存在但未运行，则移除旧进程
		w.overseer.Remove(w.id)
	}

	// 确保工作程序二进制文件可用
	if err := w.ensureWorkerBinary(); err != nil {
		return err
	}

	// 构建命令行参数
	args := w.buildArgs()

	// 将工作进程添加到 overseer
	// 注意: overseer.Add 接受 []string 作为单个参数，而不是可变参数字符串
	cmd := w.overseer.Add(w.id, w.binaryPath, args)
	if cmd == nil {
		return fmt.Errorf("failed to add worker to overseer")
	}

	// 配置进程
	cmd.Dir = w.args.WorkDir
	cmd.DelayStart = 0
	cmd.RetryTimes = 0 // 默认不自动重启

	// 在 goroutine 中启动监督
	go w.overseer.Supervise(w.id)

	// 等待一段时间让进程启动
	time.Sleep(100 * time.Millisecond)

	return nil
}

// Stop 优雅地停止工作进程
func (w *Worker) Stop() error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.overseer.HasProc(w.id) {
		return fmt.Errorf("worker not found")
	}

	status := w.overseer.Status(w.id)
	if status == nil || status.State != "running" {
		return fmt.Errorf("worker not running")
	}

	// 优雅地停止进程
	if err := w.overseer.Stop(w.id); err != nil {
		return fmt.Errorf("failed to stop worker: %w", err)
	}

	// 等待进程停止
	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			// 如果优雅停止失败，则强制杀死进程
			w.overseer.Signal(w.id, syscall.SIGKILL)
			return fmt.Errorf("worker stop timeout, forced kill")
		case <-ticker.C:
			status := w.overseer.Status(w.id)
			if status != nil && status.State != "running" {
				return nil
			}
		}
	}
}

// Restart 重启工作进程
func (w *Worker) Restart() error {
	// 如果正在运行，则停止
	if w.overseer.HasProc(w.id) {
		status := w.overseer.Status(w.id)
		if status != nil && status.State == "running" {
			if err := w.Stop(); err != nil {
				return fmt.Errorf("failed to stop worker: %w", err)
			}
		}
		// 移除旧进程
		w.overseer.Remove(w.id)
	}

	// 等待一段时间再重启
	time.Sleep(500 * time.Millisecond)

	// 再次启动
	return w.Start()
}

// Status 返回工作进程的当前状态
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

// GetDetailedStatus 返回详细的状态信息
func (w *Worker) GetDetailedStatus() *overseer.ProcessJSON {
	w.mu.RLock()
	defer w.mu.RUnlock()

	if !w.overseer.HasProc(w.id) {
		return nil
	}

	return w.overseer.Status(w.id)
}

// Logs 返回最近的工作日志行
func (w *Worker) Logs() []string {
	w.mu.RLock()
	defer w.mu.RUnlock()

	// 返回一个副本以避免竞争条件
	logsCopy := make([]string, len(w.logs))
	copy(logsCopy, w.logs)
	return logsCopy
}

// collectLogs 收集工作进程的日志
func (w *Worker) collectLogs() {
	for {
		select {
		case msg, ok := <-w.logChan:
			if !ok {
				return
			}
			w.mu.Lock()
			// 格式化日志消息
			logType := "INFO"
			if msg.Type == 1 {
				logType = "ERROR"
			}
			logLine := fmt.Sprintf("[%s] [%s] %s",
				time.Now().Format("2006-01-02 15:04:05"), logType, msg.Text)
			w.logs = append(w.logs, logLine)

			// 只保留最近的日志
			if len(w.logs) > w.maxLogs {
				w.logs = w.logs[len(w.logs)-w.maxLogs:]
			}
			w.mu.Unlock()

		case state, ok := <-w.stateChan:
			if !ok {
				return
			}
			w.mu.Lock()
			// 记录状态变化
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

// IsRunning 检查工作进程是否正在运行
func (w *Worker) IsRunning() bool {
	return w.Status() == "running"
}

// Signal 发送信号到工作进程
func (w *Worker) Signal(sig syscall.Signal) error {
	w.mu.Lock()
	defer w.mu.Unlock()

	if !w.overseer.HasProc(w.id) {
		return fmt.Errorf("worker not found")
	}

	return w.overseer.Signal(w.id, sig)
}

// Cleanup 清理资源
func (w *Worker) Cleanup() error {
	// 如果正在运行，则停止
	if w.IsRunning() {
		if err := w.Stop(); err != nil {
			return err
		}
	}

	// 取消订阅通道
	w.overseer.UnWatchLogs(w.logChan)
	w.overseer.UnWatchState(w.stateChan)

	// 关闭通道
	close(w.logChan)
	close(w.stateChan)

	// 注意: 我们不在这里删除工作程序二进制文件，因为它可能被共享
	// 或者用户有意放置在特定位置

	return nil
}
