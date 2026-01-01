package manager_test

import (
	"context"
	"sync"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestWorker_ConcurrentStartStop(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	var wg sync.WaitGroup
	for range 10 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			worker.Start()
			time.Sleep(50 * time.Millisecond)
		}()
	}

	wg.Wait()
	time.Sleep(500 * time.Millisecond)

	worker.Stop()
}

func TestWorker_ConcurrentStatusCheck(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	err = worker.Start()
	if err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}

	var wg sync.WaitGroup
	for range 20 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for range 10 {
				_ = worker.Status()
				_ = worker.IsRunning()
				_ = worker.GetDetailedStatus()
				_ = worker.Logs()
				time.Sleep(10 * time.Millisecond)
			}
		}()
	}

	wg.Wait()
	worker.Stop()
}

func TestWorker_ConcurrentRestart(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	err = worker.Start()
	if err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}
	time.Sleep(500 * time.Millisecond)

	for range 3 {
		err = worker.Restart()
		if err != nil {
			t.Logf("Restart error (may be expected): %v", err)
		}
		time.Sleep(500 * time.Millisecond)
	}

	if worker.IsRunning() {
		worker.Stop()
	}
}

func TestWorker_StartStopCycle(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	for range 5 {
		err := worker.Start()
		if err != nil {
			t.Logf("Start error (expected on concurrent calls): %v", err)
		}
		time.Sleep(300 * time.Millisecond)

		if worker.IsRunning() {
			err = worker.Stop()
			if err != nil {
				t.Logf("Stop error: %v", err)
			}
		}
		time.Sleep(200 * time.Millisecond)
	}
}

func TestWorker_CleanupWhileRunning(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)

	err = worker.Start()
	if err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}

	time.Sleep(300 * time.Millisecond)

	err = worker.Cleanup()
	assert.NoError(t, err)
}

func TestWorker_MultipleCleanup(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	worker := manager.NewWorker(args)

	err = worker.Start()
	if err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}
	time.Sleep(300 * time.Millisecond)

	var wg sync.WaitGroup
	for range 3 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			worker.Cleanup()
		}()
	}

	wg.Wait()
}

func TestWorker_LogCollectionDuringCleanup(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "debug"

	worker := manager.NewWorker(args)

	err = worker.Start()
	if err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}

	var wg sync.WaitGroup
	done := make(chan struct{})

	wg.Add(1)
	go func() {
		defer wg.Done()
		for {
			select {
			case <-done:
				return
			default:
				_ = worker.Logs()
				time.Sleep(10 * time.Millisecond)
			}
		}
	}()

	time.Sleep(500 * time.Millisecond)

	err = worker.Cleanup()
	assert.NoError(t, err)

	close(done)
	wg.Wait()
}

func TestManager_ConcurrentCompute(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	if err != nil {
		t.Fatalf("Failed to start manager: %v", err)
	}

	var wg sync.WaitGroup
	for range 10 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			ctx := context.Background()
			_, _ = mgr.Translate(ctx, "test")
		}()
	}

	wg.Wait()
	mgr.Stop()
}

func TestManager_ConcurrentStartStop(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	var wg sync.WaitGroup
	for range 5 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			mgr.Start()
			time.Sleep(100 * time.Millisecond)
		}()
	}

	wg.Wait()
	time.Sleep(500 * time.Millisecond)

	mgr.Stop()
}

func TestManager_StatusChecksDuringRestart(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping race condition test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "error"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	if err != nil {
		t.Fatalf("Failed to start manager: %v", err)
	}

	var wg sync.WaitGroup
	done := make(chan struct{})

	wg.Add(1)
	go func() {
		defer wg.Done()
		for {
			select {
			case <-done:
				return
			default:
				_ = mgr.IsRunning()
				_ = mgr.Status()
				_ = mgr.Logs()
				time.Sleep(10 * time.Millisecond)
			}
		}
	}()

	time.Sleep(200 * time.Millisecond)
	mgr.Restart()
	time.Sleep(200 * time.Millisecond)

	close(done)
	wg.Wait()

	mgr.Stop()
}
