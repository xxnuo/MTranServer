package manager_test

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"

	"github.com/xxnuo/MTranServer/bin"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestBasicUsage(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

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
	args.WorkDir = "."

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	if err := worker.Start(); err != nil {
		t.Logf("Failed to start worker: %v\n", err)
		t.Fatalf("Failed to start worker: %v", err)
	}

	time.Sleep(2 * time.Second)

	status := worker.Status()
	t.Logf("Worker status: %s\n", status)

	detailedStatus := worker.GetDetailedStatus()
	if detailedStatus != nil {
		t.Logf("Worker PID: %d\n", detailedStatus.PID)
		t.Logf("Worker state: %s\n", detailedStatus.State)
	}

	if worker.IsRunning() {
		t.Log("Worker is running")
	}

	logs := worker.Logs()
	t.Logf("Collected %d log lines\n", len(logs))
	for _, log := range logs {
		t.Log(log)
	}

	if err := worker.Restart(); err != nil {
		t.Logf("Failed to restart worker: %v\n", err)
		t.Fatalf("Failed to restart worker: %v", err)
	}

	time.Sleep(2 * time.Second)

	if err := worker.Stop(); err != nil {
		t.Logf("Failed to stop worker: %v\n", err)
		t.Fatalf("Failed to stop worker: %v", err)
	}

	t.Log("Worker stopped successfully")
}

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

	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	worker.Start()
	time.Sleep(1 * time.Second)

	t.Log("Status:", worker.Status())

	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	t.Log("Status after stop:", worker.Status())
}

func TestWorkerHash(t *testing.T) {

	if bin.WorkerHash == "" {
		t.Fatal("WorkerHash should not be empty")
	}

	if len(bin.WorkerHash) != 64 {
		t.Fatalf("WorkerHash should be 64 characters (SHA256), got %d", len(bin.WorkerHash))
	}

	t.Logf("Worker binary hash: %s", bin.WorkerHash)
	t.Logf("Worker binary size: %d bytes", len(bin.WorkerBinary))

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	worker := manager.NewWorker(args)
	defer worker.Cleanup()

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker on first attempt: %v", err)
	}
	time.Sleep(1 * time.Second)
	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	if err := worker.Start(); err != nil {
		t.Fatalf("Failed to start worker on second attempt: %v", err)
	}
	time.Sleep(1 * time.Second)
	worker.Stop()
	time.Sleep(500 * time.Millisecond)

	t.Log("Worker hash verification successful")
}

func TestCustomBinaryPath(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	customPath := filepath.Join(os.TempDir(), "custom-mtran-worker")
	if runtime.GOOS == "windows" {
		customPath += ".exe"
	}
	if err := os.WriteFile(customPath, bin.WorkerBinary, 0755); err != nil {
		t.Fatalf("Failed to create custom binary: %v", err)
	}
	defer os.Remove(customPath)

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	args.Port = port
	args.BinaryPath = customPath
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

func TestMultipleWorkers(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

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

		worker := manager.NewWorker(args)
		workers = append(workers, worker)

		if err := worker.Start(); err != nil {
			t.Fatalf("Failed to start worker %d: %v", i, err)
		}
		t.Logf("Worker %d started on port %d", i, port)
	}

	time.Sleep(2 * time.Second)

	for i, worker := range workers {
		if !worker.IsRunning() {
			t.Errorf("Worker %d should be running", i)
		}
		status := worker.GetDetailedStatus()
		if status != nil {
			t.Logf("Worker %d: PID=%d, State=%s", i, status.PID, status.State)
		}
	}

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
