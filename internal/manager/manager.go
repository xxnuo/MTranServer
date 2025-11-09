package manager

import (
	"context"
	"fmt"
	"sync"
	"time"
)

// Manager 管理 Worker 和 Client，提供统一的翻译服务接口
type Manager struct {
	worker *Worker
	client *Client
	mu     sync.RWMutex
	url    string
}

// ManagerOption 管理器配置选项
type ManagerOption func(*Manager)

// NewManager 创建新的 Manager 实例
func NewManager(args *WorkerArgs, opts ...ManagerOption) *Manager {
	// 构建 WebSocket URL
	url := fmt.Sprintf("ws://%s:%d/ws", args.Host, args.Port)

	m := &Manager{
		worker: NewWorker(args),
		url:    url,
	}

	for _, opt := range opts {
		opt(m)
	}

	return m
}

// Start 启动 Worker 并连接 Client
func (m *Manager) Start() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	// 启动 Worker
	if err := m.worker.Start(); err != nil {
		return fmt.Errorf("failed to start worker: %w", err)
	}

	// 等待 Worker 启动
	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			m.worker.Stop()
			return fmt.Errorf("worker start timeout")
		case <-ticker.C:
			if m.worker.IsRunning() {
				// Worker 已运行，创建并连接 Client
				m.client = NewClient(m.url)
				if err := m.client.Connect(); err != nil {
					m.worker.Stop()
					return fmt.Errorf("failed to connect to worker: %w", err)
				}
				return nil
			}
		}
	}
}

// Stop 停止 Manager
func (m *Manager) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var errs []error

	// 关闭 Client
	if m.client != nil {
		if err := m.client.Close(); err != nil {
			errs = append(errs, fmt.Errorf("failed to close client: %w", err))
		}
		m.client = nil
	}

	// 停止 Worker
	if m.worker != nil {
		if err := m.worker.Stop(); err != nil {
			errs = append(errs, fmt.Errorf("failed to stop worker: %w", err))
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("stop errors: %v", errs)
	}

	return nil
}

// Restart 重启 Manager
func (m *Manager) Restart() error {
	if err := m.Stop(); err != nil {
		return fmt.Errorf("failed to stop: %w", err)
	}

	time.Sleep(500 * time.Millisecond)

	return m.Start()
}

// Cleanup 清理资源
func (m *Manager) Cleanup() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	var errs []error

	// 关闭 Client（忽略错误，继续清理）
	if m.client != nil {
		if err := m.client.Close(); err != nil {
			errs = append(errs, fmt.Errorf("failed to close client: %w", err))
		}
		m.client = nil
	}

	// 清理 Worker（即使 client 关闭失败也要清理）
	if m.worker != nil {
		if err := m.worker.Cleanup(); err != nil {
			errs = append(errs, fmt.Errorf("failed to cleanup worker: %w", err))
		}
		// 不设置为 nil，因为 worker 结构体可能还需要保留
	}

	if len(errs) > 0 {
		return fmt.Errorf("cleanup errors: %v", errs)
	}

	return nil
}

// IsRunning 检查 Manager 是否正在运行
func (m *Manager) IsRunning() bool {
	m.mu.RLock()
	defer m.mu.RUnlock()

	return m.worker != nil && m.worker.IsRunning() && m.client != nil && m.client.IsConnected()
}

// Status 返回 Worker 状态
func (m *Manager) Status() string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.worker == nil {
		return "not_initialized"
	}

	return m.worker.Status()
}

// Logs 返回 Worker 日志
func (m *Manager) Logs() []string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.worker == nil {
		return nil
	}

	return m.worker.Logs()
}

// Poweron 加载翻译引擎
func (m *Manager) Poweron(ctx context.Context, req PoweronRequest) (*PoweronResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Poweron(ctx, req)
}

// Poweroff 关闭翻译引擎
func (m *Manager) Poweroff(ctx context.Context, req PoweroffRequest) (*PoweroffResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Poweroff(ctx, req)
}

// Reboot 重启翻译引擎
func (m *Manager) Reboot(ctx context.Context, req RebootRequest) (*RebootResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Reboot(ctx, req)
}

// Ready 检查翻译引擎是否就绪
func (m *Manager) Ready(ctx context.Context) (bool, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return false, fmt.Errorf("client not initialized")
	}

	return m.client.Ready(ctx)
}

// Compute 翻译文本
func (m *Manager) Compute(ctx context.Context, req ComputeRequest) (string, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return "", fmt.Errorf("client not initialized")
	}

	return m.client.Compute(ctx, req)
}

// Translate 翻译文本（简化接口）
func (m *Manager) Translate(ctx context.Context, text string) (string, error) {
	return m.Compute(ctx, ComputeRequest{
		Text: text,
		HTML: false,
	})
}

// TranslateHTML 翻译 HTML 文本
func (m *Manager) TranslateHTML(ctx context.Context, html string) (string, error) {
	return m.Compute(ctx, ComputeRequest{
		Text: html,
		HTML: true,
	})
}
