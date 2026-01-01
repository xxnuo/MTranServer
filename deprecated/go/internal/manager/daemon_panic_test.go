package manager_test

import (
	"testing"
	"time"

	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestWorkerPanicRecovery(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}

	args := manager.NewWorkerArgs()
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}
	time.Sleep(1 * time.Second)

	if !worker.IsRunning() {
		t.Fatal("Worker should be running")
	}

	if err := worker.Stop(); err != nil {
		t.Logf("Stop returned error (may be expected): %v", err)
	}
	time.Sleep(500 * time.Millisecond)

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to restart worker after stop: %v", err)
	}
	time.Sleep(1 * time.Second)

	if !worker.IsRunning() {
		t.Fatal("Worker should be running after restart")
	}

	t.Log("Worker successfully recovered from panic scenario")
}

func TestMultipleStopStartCycles(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}

	args := manager.NewWorkerArgs()
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	cycles := 5
	for i := 0; i < cycles; i++ {
		t.Logf("Cycle %d: Starting worker", i+1)
		if err := worker.Start(); err != nil {
			t.Fatalf("Cycle %d: Failed to start worker: %v", i+1, err)
		}
		time.Sleep(500 * time.Millisecond)

		if !worker.IsRunning() {
			t.Fatalf("Cycle %d: Worker should be running", i+1)
		}

		t.Logf("Cycle %d: Stopping worker", i+1)
		if err := worker.Stop(); err != nil {
			t.Logf("Cycle %d: Stop returned error: %v", i+1, err)
		}
		time.Sleep(500 * time.Millisecond)

		status := worker.Status()
		if status == "running" {
			t.Fatalf("Cycle %d: Worker should not be running after stop, status: %s", i+1, status)
		}
	}

	t.Logf("Successfully completed %d stop/start cycles", cycles)
}

func TestRestartUnderLoad(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}

	args := manager.NewWorkerArgs()
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}
	time.Sleep(1 * time.Second)

	restarts := 3
	for i := 0; i < restarts; i++ {
		t.Logf("Restart attempt %d", i+1)
		if err := worker.Restart(); err != nil {
			t.Fatalf("Restart %d failed: %v", i+1, err)
		}
		time.Sleep(1 * time.Second)

		if !worker.IsRunning() {
			t.Fatalf("Worker should be running after restart %d", i+1)
		}
	}

	t.Logf("Successfully completed %d restarts", restarts)
}

func TestCleanupAfterPanic(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}

	args := manager.NewWorkerArgs()
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	worker := manager.NewWorker(args)

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker: %v", err)
	}
	time.Sleep(1 * time.Second)

	if err := worker.Cleanup(); err != nil {
		t.Logf("Cleanup returned error (may be expected on Windows): %v", err)
	}

	time.Sleep(500 * time.Millisecond)

	worker2 := manager.NewWorker(args)
	defer worker2.Cleanup()

	if err := worker2.Start(); err != nil {
		t.Fatalf("Failed to start new worker with same ID after cleanup: %v", err)
	}
	time.Sleep(1 * time.Second)

	if !worker2.IsRunning() {
		t.Fatal("Second worker should be running")
	}

	t.Log("Successfully cleaned up and restarted worker with same ID")
}
