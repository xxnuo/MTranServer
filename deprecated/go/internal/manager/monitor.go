package manager

import (
	"context"
	"sync"
	"time"

	"github.com/xxnuo/MTranServer/internal/logger"
)

type WorkerMonitor struct {
	worker      *Worker
	client      *Client
	url         string
	stopChan    chan struct{}
	restartChan chan struct{}
	wg          sync.WaitGroup
	mu          sync.Mutex
	running     bool
}

func NewWorkerMonitor(worker *Worker, client *Client, url string) *WorkerMonitor {
	return &WorkerMonitor{
		worker:      worker,
		client:      client,
		url:         url,
		stopChan:    make(chan struct{}),
		restartChan: make(chan struct{}, 1),
		running:     false,
	}
}

func (wm *WorkerMonitor) Start() {
	wm.mu.Lock()
	if wm.running {
		wm.mu.Unlock()
		return
	}
	wm.running = true
	wm.mu.Unlock()

	wm.wg.Add(1)
	go wm.monitor()
}

func (wm *WorkerMonitor) Stop() {
	wm.mu.Lock()
	if !wm.running {
		wm.mu.Unlock()
		return
	}
	wm.running = false
	wm.mu.Unlock()

	close(wm.stopChan)
	wm.wg.Wait()
}

func (wm *WorkerMonitor) monitor() {
	defer wm.wg.Done()

	ticker := time.NewTicker(5 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-wm.stopChan:
			return

		case <-wm.restartChan:
			wm.handleRestart()

		case <-ticker.C:
			wm.checkWorkerHealth()
		}
	}
}

func (wm *WorkerMonitor) checkWorkerHealth() {
	if !wm.worker.IsRunning() {
		logger.Warn("Worker process exited unexpectedly, attempting restart...")
		select {
		case wm.restartChan <- struct{}{}:
		default:
		}
		return
	}

	if wm.client != nil && wm.client.IsConnected() {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()

		_, err := wm.client.Health(ctx)
		if err != nil {
			logger.Warn("Worker health check failed: %v, attempting restart...", err)
			select {
			case wm.restartChan <- struct{}{}:
			default:
			}
		}
	}
}

func (wm *WorkerMonitor) handleRestart() {
	logger.Info("Restarting worker...")

	if wm.client != nil {
		wm.client.Close()
	}

	if err := wm.worker.Stop(); err != nil {
		logger.Warn("Failed to stop worker cleanly: %v", err)
	}

	time.Sleep(1 * time.Second)

	if err := wm.worker.Start(); err != nil {
		logger.Error("Failed to restart worker: %v", err)
		return
	}

	newClient := NewClient(wm.url)
	if err := newClient.Connect(); err != nil {
		logger.Error("Failed to reconnect client after restart: %v", err)
		return
	}

	wm.client = newClient
	logger.Info("Worker restarted successfully")
}

func (wm *WorkerMonitor) TriggerRestart() {
	select {
	case wm.restartChan <- struct{}{}:
	default:
	}
}
