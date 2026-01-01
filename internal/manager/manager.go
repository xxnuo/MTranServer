package manager

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/logger"
	"github.com/xxnuo/MTranServer/internal/utils"
)

type Manager struct {
	worker       *Worker
	client       *Client
	mu           sync.RWMutex
	url          string
	reconnecting bool
	reconnectCh  chan struct{}
	reconnectMu  sync.Mutex
}

type ManagerOption func(*Manager)

func NewManager(args *WorkerArgs, opts ...ManagerOption) *Manager {

	url := fmt.Sprintf("ws://%s:%d/ws", args.Host, args.Port)

	m := &Manager{
		worker:       NewWorker(args),
		url:          url,
		reconnecting: false,
		reconnectCh:  nil,
	}

	for _, opt := range opts {
		opt(m)
	}

	return m
}

func (m *Manager) Start() error {
	m.mu.Lock()
	if err := m.worker.Start(); err != nil {
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
						m.worker.Stop()
						return fmt.Errorf("failed to connect to worker: %w", err)
					}
					connected = true
					continue
				}

				stableStart := time.Now()
				stableDuration := 500 * time.Millisecond
				for time.Since(stableStart) < stableDuration {
					if !m.worker.IsRunning() {
						client.Close()
						m.worker.Stop()
						return fmt.Errorf("worker exited immediately after connection")
					}
					time.Sleep(50 * time.Millisecond)
				}

				m.mu.Lock()
				m.client = client
				m.mu.Unlock()
				return nil
			}
		}
	}
}

func (m *Manager) Stop() error {
	m.mu.Lock()
	defer m.mu.Unlock()

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

	return m.worker != nil && m.worker.IsRunning() && m.client != nil && m.client.IsConnected()
}

func (m *Manager) Status() string {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.worker == nil {
		return "not_initialized"
	}

	return m.worker.Status()
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
	logger.Debug("Manager.Trans: text length: %d, isHTML: %v", len(req.Text), req.HTML)
	m.mu.RLock()
	client := m.client
	worker := m.worker
	m.mu.RUnlock()

	if client == nil || worker == nil {
		logger.Error("Manager.Trans: client or worker not initialized")
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
		strings.Contains(errMsg, "invalid table access")

	if !isConnectionError {
		return "", err
	}

	m.mu.Lock()
	if m.reconnecting {
		ch := m.reconnectCh
		m.mu.Unlock()
		if ch != nil {
			logger.Debug("Manager.Trans: another request is reconnecting, waiting...")
			select {
			case <-ch:
				logger.Debug("Manager.Trans: reconnection completed, retrying")
				m.mu.RLock()
				newClient := m.client
				m.mu.RUnlock()
				if newClient != nil {
					return newClient.Trans(ctx, req)
				}
				return "", fmt.Errorf("reconnection failed")
			case <-ctx.Done():
				return "", ctx.Err()
			}
		}
	}

	if m.client != client {
		m.mu.Unlock()
		if m.client != nil {
			logger.Debug("Manager.Trans: client changed during reconnection, retrying with new client")
			return m.client.Trans(ctx, req)
		}
		return "", fmt.Errorf("client changed to nil during reconnection")
	}

	m.reconnecting = true
	m.reconnectCh = make(chan struct{})
	ch := m.reconnectCh
	m.mu.Unlock()

	defer func() {
		m.mu.Lock()
		m.reconnecting = false
		m.reconnectCh = nil
		m.mu.Unlock()
		close(ch)
	}()

	logger.Debug("Manager.Trans: attempting reconnection")

	m.mu.Lock()
	oldClient := m.client
	oldWorker := m.worker

	if oldClient != nil {
		m.client = nil
	}

	newPort, err := utils.GetFreePort()
	if err != nil {
		m.mu.Unlock()
		return "", fmt.Errorf("failed to allocate new port: %w", err)
	}

	newArgs := *m.worker.args
	newArgs.Port = newPort
	newWorker := NewWorker(&newArgs)
	newURL := fmt.Sprintf("ws://%s:%d/ws", newArgs.Host, newArgs.Port)

	m.worker = newWorker
	m.url = newURL
	m.mu.Unlock()

	go func() {
		defer func() {
			if r := recover(); r != nil {
				logger.Error("Panic during old worker cleanup: %v", r)
			}
		}()

		// Give a small grace period for any pending operations
		time.Sleep(100 * time.Millisecond)

		if oldClient != nil {
			exitCtx, exitCancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
			oldClient.Exit(exitCtx, ExitRequest{Time: 0, Force: true})
			exitCancel()
			oldClient.Close()
		}

		if oldWorker != nil {
			// Try graceful stop first
			if err := oldWorker.Stop(); err != nil {
				logger.Warn("Failed to stop old worker gracefully: %v", err)
			}
		}
		logger.Debug("Old worker cleanup completed")
	}()

	if startErr := newWorker.Start(); startErr != nil {
		return "", fmt.Errorf("failed to start new worker: %w", startErr)
	}

	maxRetries := 50
	for i := 0; i < maxRetries; i++ {
		if !newWorker.IsRunning() {
			return "", fmt.Errorf("new worker exited unexpectedly")
		}

		newClient := NewClient(newURL)
		if connErr := newClient.Connect(); connErr == nil {
			m.mu.Lock()
			m.client = newClient
			m.mu.Unlock()

			return newClient.Trans(ctx, req)
		}

		time.Sleep(100 * time.Millisecond)
	}

	return "", fmt.Errorf("failed to connect to new worker after %d retries", maxRetries)
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
