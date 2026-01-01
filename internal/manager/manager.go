package manager

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/logger"
)

const (
	StateStopped    = 0
	StateStarting   = 1
	StateRunning    = 2
	StateRestarting = 3
)

type Manager struct {
	worker    *Worker
	client    *Client
	mu        sync.RWMutex
	url       string
	taskQueue chan struct{} // Token bucket for serializing tasks
	closed    bool
	state     int
}

type ManagerOption func(*Manager)

func NewManager(args *WorkerArgs, opts ...ManagerOption) *Manager {

	url := fmt.Sprintf("ws://%s:%d/ws", args.Host, args.Port)

	m := &Manager{
		worker:    NewWorker(args),
		url:       url,
		taskQueue: make(chan struct{}, 1),
		state:     StateStopped,
	}

	for _, opt := range opts {
		opt(m)
	}

	return m
}

func (m *Manager) Start() error {
	m.mu.Lock()
	if m.state != StateStopped {
		m.mu.Unlock()
		return fmt.Errorf("manager is not in stopped state")
	}
	m.state = StateStarting

	if err := m.worker.Start(); err != nil {
		m.state = StateStopped
		m.mu.Unlock()
		return fmt.Errorf("failed to start worker: %w", err)
	}
	m.mu.Unlock()

	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	var connected bool
	var client *Client

	for {
		select {
		case <-timeout:
			m.Stop()
			return fmt.Errorf("worker start timeout")
		case <-ticker.C:
			if m.worker.IsRunning() {
				if !connected {
					client = NewClient(m.url)
					if err := client.Connect(); err != nil {
						// Keep retrying connection
						continue
					}
					connected = true
				}

				// Wait for worker to be stable and really ready
				healthCtx, healthCancel := context.WithTimeout(context.Background(), 1*time.Second)
				isHealthy, _ := client.Health(healthCtx)
				healthCancel()

				if !isHealthy {
					continue
				}

				m.mu.Lock()
				m.client = client
				m.state = StateRunning
				m.mu.Unlock()
				return nil
			}
		}
	}
}

func (m *Manager) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()

	m.state = StateStopped
	var errs []error

	if m.client != nil {
		if err := m.client.Close(); err != nil {
			errs = append(errs, fmt.Errorf("failed to close client: %w", err))
		}
		m.client = nil
	}

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

func (m *Manager) Restart() error {
	if err := m.Stop(); err != nil {
		return fmt.Errorf("failed to stop: %w", err)
	}

	time.Sleep(500 * time.Millisecond)

	return m.Start()
}

func (m *Manager) Cleanup() error {
	m.mu.Lock()
	m.state = StateStopped
	defer m.mu.Unlock()

	var errs []error

	if m.client != nil {
		if err := m.client.Close(); err != nil {
			errs = append(errs, fmt.Errorf("failed to close client: %w", err))
		}
		m.client = nil
	}

	if m.worker != nil {
		if err := m.worker.Cleanup(); err != nil {
			errs = append(errs, fmt.Errorf("failed to cleanup worker: %w", err))
		}

	}

	if len(errs) > 0 {
		return fmt.Errorf("cleanup errors: %v", errs)
	}

	return nil
}

func (m *Manager) IsRunning() bool {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.state == StateRunning && m.worker != nil && m.worker.IsRunning() && m.client != nil && m.client.IsConnected()
}

func (m *Manager) IsHealthy(ctx context.Context) bool {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.state != StateRunning || m.client == nil {
		return false
	}

	healthy, err := m.client.Health(ctx)
	return err == nil && healthy
}

func (m *Manager) Status() string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	switch m.state {
	case StateStopped:
		return "stopped"
	case StateStarting:
		return "starting"
	case StateRunning:
		return "running"
	case StateRestarting:
		return "restarting"
	default:
		return "unknown"
	}
}

func (m *Manager) Logs() []string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.worker == nil {
		return nil
	}

	return m.worker.Logs()
}

func (m *Manager) Health(ctx context.Context) (bool, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return false, fmt.Errorf("client not initialized")
	}

	return m.client.Health(ctx)
}

func (m *Manager) Trans(ctx context.Context, req TransRequest) (string, error) {
	// 1. Check state immediately
	m.mu.RLock()
	if m.state != StateRunning {
		state := m.state
		m.mu.RUnlock()
		return "", fmt.Errorf("manager not running (state: %d)", state)
	}
	m.mu.RUnlock()

	// 2. Wait for task slot (concurrency control)
	select {
	case m.taskQueue <- struct{}{}:
		defer func() { <-m.taskQueue }()
	case <-ctx.Done():
		return "", ctx.Err()
	}

	logger.Debug("Manager.Trans: text length: %d, isHTML: %v", len(req.Text), req.HTML)
	m.mu.RLock()
	if m.closed {
		m.mu.RUnlock()
		return "", fmt.Errorf("manager is closed")
	}

	// Double check state after acquiring lock
	if m.state != StateRunning {
		m.mu.RUnlock()
		return "", fmt.Errorf("manager not running (state: %d)", m.state)
	}

	client := m.client
	// worker := m.worker
	m.mu.RUnlock()

	if client == nil {
		logger.Error("Manager.Trans: client not initialized")
		m.TriggerRestartAsync()
		return "", fmt.Errorf("client not initialized")
	}

	logger.Debug("Manager.Trans: calling client.Trans")
	result, err := client.Trans(ctx, req)
	if err == nil {
		logger.Debug("Manager.Trans: success, result length: %d", len(result))
		return result, nil
	}
	logger.Debug("Manager.Trans: client.Trans error: %v", err)

	errMsg := err.Error()
	isConnectionError := !client.IsConnected() ||
		strings.Contains(errMsg, "not connected") ||
		strings.Contains(errMsg, "failed to send message") ||
		strings.Contains(errMsg, "failed to read response") ||
		strings.Contains(errMsg, "module closed") ||
		strings.Contains(errMsg, "exit_code") ||
		strings.Contains(errMsg, "wasm error") ||
		strings.Contains(errMsg, "invalid table access") ||
		strings.Contains(errMsg, "Translation engine not ready") ||
		strings.Contains(errMsg, "code 503")

	if !isConnectionError {
		return "", err
	}

	// Trigger async restart and fail this request
	m.TriggerRestartAsync()
	return "", fmt.Errorf("worker connection failed, restarting: %w", err)
}

func (m *Manager) TriggerRestartAsync() {
	m.mu.Lock()
	if m.state == StateRestarting || m.state == StateStopped {
		m.mu.Unlock()
		return
	}
	m.state = StateRestarting
	m.mu.Unlock()

	go func() {
		logger.Info("Async restart triggered for worker on port %d", m.worker.args.Port)
		if err := m.RestartWorker(); err != nil {
			logger.Error("Async restart failed: %v", err)
			// Ensure we mark as stopped so it can be picked up or retried later if needed?
			// or maybe we should try again? For now, leave it as stopped/failed.
			m.mu.Lock()
			m.state = StateStopped
			m.mu.Unlock()
		} else {
			logger.Info("Async restart completed successfully")
		}
	}()
}

// RestartWorker performs the kill-and-restart logic on the SAME port
func (m *Manager) RestartWorker() error {
	// 1. Kill old worker and cleanup resources
	m.mu.Lock()
	oldWorker := m.worker
	oldClient := m.client

	if oldClient != nil {
		m.client = nil
		go oldClient.Close() // Close async
	}
	m.mu.Unlock()

	logger.Info("Stopping old worker...")
	// Force kill if necessary, make sure port is freed
	if oldWorker != nil {
		if err := oldWorker.Cleanup(); err != nil {
			logger.Warn("Failed to cleanup old worker: %v", err)
		}
	}

	// Wait a bit to ensure OS releases port
	time.Sleep(1 * time.Second)

	// 2. Start new worker on the SAME port (args are reused)
	// We need to create a NEW worker instance because the old one holds the old cmd/process
	m.mu.Lock()
	newWorker := NewWorker(m.worker.args)
	m.worker = newWorker
	m.mu.Unlock()

	logger.Info("Starting new worker on port %d...", newWorker.args.Port)
	if err := newWorker.Start(); err != nil {
		return fmt.Errorf("failed to start new worker: %w", err)
	}

	// 3. Wait for readiness
	timeout := time.After(30 * time.Second)
	ticker := time.NewTicker(200 * time.Millisecond)
	defer ticker.Stop()

	var client *Client
	var connected bool

	for {
		select {
		case <-timeout:
			newWorker.Stop()
			return fmt.Errorf("restart timeout waiting for worker readiness")
		case <-ticker.C:
			if newWorker.IsRunning() {
				if !connected {
					client = NewClient(m.url) // URL is unchanged
					if err := client.Connect(); err != nil {
						continue
					}
					connected = true
				}

				healthCtx, healthCancel := context.WithTimeout(context.Background(), 1*time.Second)
				isHealthy, _ := client.Health(healthCtx)
				healthCancel()

				if isHealthy {
					m.mu.Lock()
					m.client = client
					m.state = StateRunning
					m.mu.Unlock()
					return nil
				}
			}
		}
	}
}

func (m *Manager) Exit(ctx context.Context, req ExitRequest) (*ExitResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Exit(ctx, req)
}

func (m *Manager) Translate(ctx context.Context, text string) (string, error) {
	return m.Trans(ctx, TransRequest{
		Text: text,
		HTML: false,
	})
}

func (m *Manager) TranslateHTML(ctx context.Context, html string) (string, error) {
	return m.Trans(ctx, TransRequest{
		Text: html,
		HTML: true,
	})
}
