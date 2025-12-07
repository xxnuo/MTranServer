package manager

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"time"
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
	defer m.mu.Unlock()

	if err := m.worker.Start(); err != nil {
		return fmt.Errorf("failed to start worker: %w", err)
	}

	timeout := time.After(10 * time.Second)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	var connected bool
	for {
		select {
		case <-timeout:
			m.worker.Stop()
			return fmt.Errorf("worker start timeout")
		case <-ticker.C:
			if m.worker.IsRunning() {
				if !connected {
					m.client = NewClient(m.url)
					if err := m.client.Connect(); err != nil {
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
						m.client.Close()
						m.client = nil
						m.worker.Stop()
						return fmt.Errorf("worker exited immediately after connection")
					}
					time.Sleep(50 * time.Millisecond)
				}
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
	m.mu.RLock()
	client := m.client
	m.mu.RUnlock()

	if client == nil {
		return "", fmt.Errorf("client not initialized")
	}

	result, err := client.Compute(ctx, req)
	if err == nil {
		return result, nil
	}

	errMsg := err.Error()
	isConnectionError := !client.IsConnected() || 
		strings.Contains(errMsg, "not connected") || 
		strings.Contains(errMsg, "failed to send message") || 
		strings.Contains(errMsg, "failed to read response") ||
		strings.Contains(errMsg, "module closed") ||
		strings.Contains(errMsg, "exit_code")

	if !isConnectionError {
		return "", err
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if m.client != client {
		return m.client.Compute(ctx, req)
	}

	if !m.worker.IsRunning() || !m.client.IsConnected() {
		if m.client != nil {
			m.client.Close()
			m.client = nil
		}

		if m.worker.IsRunning() {
			m.worker.Stop()
		}

		time.Sleep(500 * time.Millisecond)

		if err := m.worker.Start(); err != nil {
			return "", fmt.Errorf("failed to restart worker: %w", err)
		}

		timeout := time.After(10 * time.Second)
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()

		for {
			select {
			case <-timeout:
				return "", fmt.Errorf("worker restart timeout")
			case <-ticker.C:
				if m.worker.IsRunning() {
					m.client = NewClient(m.url)
					if err := m.client.Connect(); err != nil {
						continue
					}

					time.Sleep(200 * time.Millisecond)

					return m.client.Compute(ctx, req)
				}
			}
		}
	}

	return "", err
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
