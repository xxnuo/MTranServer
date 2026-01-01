package manager

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/logger"
)

type Manager struct {
	worker *Worker
	client *Client
	mu     sync.RWMutex
	url    string
}

type ManagerOption func(*Manager)

func NewManager(args *WorkerArgs, opts ...ManagerOption) *Manager {

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

func (m *Manager) Poweron(ctx context.Context, req PoweronRequest) (*PoweronResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Poweron(ctx, req)
}

func (m *Manager) Poweroff(ctx context.Context, req PoweroffRequest) (*PoweroffResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Poweroff(ctx, req)
}

func (m *Manager) Reboot(ctx context.Context, req RebootRequest) (*RebootResponse, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return nil, fmt.Errorf("client not initialized")
	}

	return m.client.Reboot(ctx, req)
}

func (m *Manager) Ready(ctx context.Context) (bool, error) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	if m.client == nil {
		return false, fmt.Errorf("client not initialized")
	}

	return m.client.Ready(ctx)
}

func (m *Manager) Compute(ctx context.Context, req ComputeRequest) (string, error) {
	logger.Debug("Manager.Compute: text length: %d, isHTML: %v", len(req.Text), req.HTML)
	m.mu.RLock()
	client := m.client
	worker := m.worker
	m.mu.RUnlock()

	if client == nil || worker == nil {
		logger.Error("Manager.Compute: client or worker not initialized")
		return "", fmt.Errorf("client not initialized")
	}

	logger.Debug("Manager.Compute: calling client.Compute")
	result, err := client.Compute(ctx, req)
	if err == nil {
		logger.Debug("Manager.Compute: success, result length: %d", len(result))
		return result, nil
	}
	logger.Debug("Manager.Compute: client.Compute error: %v", err)

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

	if m.client != client {
		m.mu.Unlock()
		if m.client != nil {
			logger.Debug("Manager.Compute: client changed during reconnection, retrying with new client")
			return m.client.Compute(ctx, req)
		}
		return "", fmt.Errorf("client changed to nil during reconnection")
	}

	logger.Debug("Manager.Compute: attempting reconnection")

	if m.client != nil {
		m.client.Close()
		m.client = nil
	}

	if m.worker != nil && m.worker.IsRunning() {
		m.worker.Stop()
	}

	time.Sleep(500 * time.Millisecond)

	if err := m.worker.Start(); err != nil {
		m.mu.Unlock()
		return "", fmt.Errorf("failed to restart worker: %w", err)
	}
	m.mu.Unlock()

	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-timeout:
			return "", fmt.Errorf("worker restart timeout")
		case <-ticker.C:
			if m.worker.IsRunning() {
				newClient := NewClient(m.url)
				if err := newClient.Connect(); err != nil {
					continue
				}

				time.Sleep(200 * time.Millisecond)

				m.mu.Lock()
				m.client = newClient
				m.mu.Unlock()

				return newClient.Compute(ctx, req)
			}
		}
	}
}

func (m *Manager) Translate(ctx context.Context, text string) (string, error) {
	return m.Compute(ctx, ComputeRequest{
		Text: text,
		HTML: false,
	})
}

func (m *Manager) TranslateHTML(ctx context.Context, html string) (string, error) {
	return m.Compute(ctx, ComputeRequest{
		Text: html,
		HTML: true,
	})
}
