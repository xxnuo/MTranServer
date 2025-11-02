package manager_test

import (
	"testing"
	"time"

	"github.com/xxnuo/MTranServer/bin"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

// Example demonstrates basic usage of the Worker
func TestBasicUsage(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	// Create worker arguments with custom configuration
	args := manager.NewWorkerArgs()
	args.Host = "127.0.0.1"
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.EnableWebSocket = true
	args.EnableHTTP = true
	args.LogLevel = "debug"
	args.WorkDir = "/tmp/mtran"
	// Binary will be written to /tmp by default (BinaryPath is empty)

	// Create a new worker
	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	// Start the worker
	if err := worker.Start(); err != nil {
		t.Logf("Failed to start worker: %v\n", err)
		t.Fatalf("Failed to start worker: %v", err)
	}

	// Wait for worker to be running
	time.Sleep(2 * time.Second)

	// Check status
	status := worker.Status()
	t.Logf("Worker status: %s\n", status)

	// Get detailed status
	detailedStatus := worker.GetDetailedStatus()
	if detailedStatus != nil {
		t.Logf("Worker PID: %d\n", detailedStatus.PID)
		t.Logf("Worker state: %s\n", detailedStatus.State)
	}

	// Check if running
	if worker.IsRunning() {
		t.Log("Worker is running")
	}

	// Get logs
	logs := worker.Logs()
	t.Logf("Collected %d log lines\n", len(logs))
	for _, log := range logs {
		t.Log(log)
	}

	// Restart the worker
	if err := worker.Restart(); err != nil {
		t.Logf("Failed to restart worker: %v\n", err)
		t.Fatalf("Failed to restart worker: %v", err)
	}

	time.Sleep(2 * time.Second)

	// Stop the worker
	if err := worker.Stop(); err != nil {
		t.Logf("Failed to stop worker: %v\n", err)
		t.Fatalf("Failed to stop worker: %v", err)
	}

	t.Log("Worker stopped successfully")
}

// Example demonstrates worker lifecycle
func TestLifecycle(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	// Binary will be written to /tmp by default
	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	// Start
	worker.Start()
	time.Sleep(1 * time.Second)

	// Check status
	t.Log("Status:", worker.Status())

	// Stop
	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	// Check status again
	t.Log("Status after stop:", worker.Status())
}

// TestWorkerHash verifies that WorkerHash is properly computed
func TestWorkerHash(t *testing.T) {
	// Check that WorkerHash is not empty
	if bin.WorkerHash == "" {
		t.Fatal("WorkerHash should not be empty")
	}

	// Check that hash is a valid hex string (64 chars for SHA256)
	if len(bin.WorkerHash) != 64 {
		t.Fatalf("WorkerHash should be 64 characters (SHA256), got %d", len(bin.WorkerHash))
	}

	t.Logf("Worker binary hash: %s", bin.WorkerHash)
	t.Logf("Worker binary size: %d bytes", len(bin.WorkerBinary))

	// Verify the worker starts successfully with the hash check
	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	// First start - should write binary and hash file
	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker on first attempt: %v", err)
	}
	time.Sleep(1 * time.Second)
	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	// Second start - should detect existing hash and skip writing
	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker on second attempt: %v", err)
	}
	time.Sleep(1 * time.Second)
	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	t.Log("Worker hash verification successful")
}

// TestCustomBinaryPath demonstrates using a custom binary path
func TestCustomBinaryPath(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	// Test with custom binary path
	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.BinaryPath = "/tmp/custom-mtran-worker" // Custom path
	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker with custom binary path: %v", err)
	}
	time.Sleep(1 * time.Second)

	if !worker.IsRunning() {
		t.Fatal("Worker should be running")
	}

	worker.Stop()
	time.Sleep(500 * time.Millisecond)
	t.Log("Worker with custom binary path stopped successfully")
}

// TestMultipleWorkers demonstrates running multiple workers concurrently
func TestMultipleWorkers(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	// Create multiple workers with different ports
	workers := make([]*manager.Worker, 0, 3)

	for i := 0; i < 3; i++ {
		port, err := utils.GetFreePort()
		if err != nil {
			t.Fatalf("Failed to get free port: %v", err)
		}

		args := manager.NewWorkerArgs()
		args.Port = port
		args.Host = "127.0.0.1"
		args.EnableWebSocket = true
		// Each worker writes to /tmp by default but has unique ID based on port

		worker := manager.NewWorker(args)
		workers = append(workers, worker)

		if err := worker.Start(); err != nil {
			t.Fatalf("Failed to start worker %d: %v", i, err)
		}
		t.Logf("Worker %d started on port %d", i, port)
	}

	// Let all workers run for a bit
	time.Sleep(2 * time.Second)

	// Verify all workers are running
	for i, worker := range workers {
		if !worker.IsRunning() {
			t.Errorf("Worker %d should be running", i)
		}
		status := worker.GetDetailedStatus()
		if status != nil {
			t.Logf("Worker %d: PID=%d, State=%s", i, status.PID, status.State)
		}
	}

	// Stop all workers
	for i, worker := range workers {
		if err := worker.Stop(); err != nil {
			t.Errorf("Failed to stop worker %d: %v", i, err)
		}
		worker.Cleanup()
		t.Logf("Worker %d stopped", i)
	}

	time.Sleep(500 * time.Millisecond)
	t.Log("All workers stopped successfully")
}
