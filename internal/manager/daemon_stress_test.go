package manager_test

import (
	"sync"
	"testing"
	"time"

	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestWorker_StressStartStop(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
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

	iterations := 20
	for i := range iterations {
		t.Logf("Iteration %d/%d", i+1, iterations)

		err := worker.Start()
		if err != nil {
			t.Logf("Start error: %v", err)
		}

		time.Sleep(200 * time.Millisecond)

		if worker.IsRunning() {
			err = worker.Stop()
			if err != nil {
				t.Errorf("Stop error at iteration %d: %v", i+1, err)
			}
		}

		time.Sleep(100 * time.Millisecond)
	}
}

func TestWorker_StressRestart(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
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
		t.Fatalf("Failed initial start: %v", err)
	}
	time.Sleep(500 * time.Millisecond)

	iterations := 15
	for i := range iterations {
		t.Logf("Restart %d/%d", i+1, iterations)

		err := worker.Restart()
		if err != nil {
			t.Errorf("Restart error at iteration %d: %v", i+1, err)
		}

		time.Sleep(300 * time.Millisecond)
	}

	if worker.IsRunning() {
		worker.Stop()
	}
}

func TestWorker_StressMultipleWorkers(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
	}

	workerCount := 5
	workers := make([]*manager.Worker, 0, workerCount)

	for i := range workerCount {
		port, err := utils.GetFreePort()
		if err != nil {
			t.Fatalf("Failed to get free port: %v", err)
		}

		args := manager.NewWorkerArgs()
		args.Port = port
		args.Host = "127.0.0.1"
		args.EnableWebSocket = true
		args.LogLevel = "error"

		worker := manager.NewWorker(args)
		workers = append(workers, worker)

		err = worker.Start()
		if err != nil {
			t.Errorf("Failed to start worker %d: %v", i, err)
		}
		t.Logf("Worker %d started on port %d", i, port)
	}

	time.Sleep(1 * time.Second)

	var wg sync.WaitGroup
	for i, w := range workers {
		wg.Add(1)
		go func(idx int, worker *manager.Worker) {
			defer wg.Done()

			for range 5 {
				_ = worker.Status()
				_ = worker.Logs()
				time.Sleep(50 * time.Millisecond)
			}

			if worker.IsRunning() {
				err := worker.Stop()
				if err != nil {
					t.Logf("Worker %d stop error: %v", idx, err)
				}
			}

			err := worker.Cleanup()
			if err != nil {
				t.Logf("Worker %d cleanup error: %v", idx, err)
			}
		}(i, w)
	}

	wg.Wait()
}

func TestManager_StressStartStopCycle(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
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

	iterations := 10
	for i := range iterations {
		t.Logf("Manager cycle %d/%d", i+1, iterations)

		err := mgr.Start()
		if err != nil {
			t.Errorf("Start error at iteration %d: %v", i+1, err)
		}

		time.Sleep(300 * time.Millisecond)

		if mgr.IsRunning() {
			err = mgr.Stop()
			if err != nil {
				t.Errorf("Stop error at iteration %d: %v", i+1, err)
			}
		}

		time.Sleep(200 * time.Millisecond)
	}
}

func TestManager_StressRestart(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
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
		t.Fatalf("Failed initial start: %v", err)
	}
	time.Sleep(500 * time.Millisecond)

	iterations := 10
	for i := range iterations {
		t.Logf("Manager restart %d/%d", i+1, iterations)

		err := mgr.Restart()
		if err != nil {
			t.Errorf("Restart error at iteration %d: %v", i+1, err)
		}

		time.Sleep(400 * time.Millisecond)
	}

	if mgr.IsRunning() {
		mgr.Stop()
	}
}

func TestWorker_StressConcurrentOperations(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
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

	var wg sync.WaitGroup
	done := make(chan struct{})

	for range 5 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-done:
					return
				default:
					_ = worker.Status()
					time.Sleep(10 * time.Millisecond)
				}
			}
		}()
	}

	for range 3 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-done:
					return
				default:
					_ = worker.Logs()
					time.Sleep(20 * time.Millisecond)
				}
			}
		}()
	}

	for range 2 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				select {
				case <-done:
					return
				default:
					_ = worker.GetDetailedStatus()
					time.Sleep(30 * time.Millisecond)
				}
			}
		}()
	}

	time.Sleep(3 * time.Second)
	close(done)
	wg.Wait()

	if worker.IsRunning() {
		worker.Stop()
	}
}

func TestManager_StressMultipleInstances(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping stress test in short mode")
	}

	managerCount := 3
	managers := make([]*manager.Manager, 0, managerCount)

	for i := range managerCount {
		port, err := utils.GetFreePort()
		if err != nil {
			t.Fatalf("Failed to get free port: %v", err)
		}

		args := manager.NewWorkerArgs()
		args.Port = port
		args.Host = "127.0.0.1"
		args.EnableWebSocket = true
		args.LogLevel = "error"

		mgr := manager.NewManager(args)
		managers = append(managers, mgr)

		err = mgr.Start()
		if err != nil {
			t.Errorf("Failed to start manager %d: %v", i, err)
		}
		t.Logf("Manager %d started on port %d", i, port)

		time.Sleep(500 * time.Millisecond)
	}

	time.Sleep(1 * time.Second)

	var wg sync.WaitGroup
	for i, m := range managers {
		wg.Add(1)
		go func(idx int, mgr *manager.Manager) {
			defer wg.Done()

			for range 3 {
				_ = mgr.IsRunning()
				_ = mgr.Status()
				_ = mgr.Logs()
				time.Sleep(100 * time.Millisecond)
			}

			if mgr.IsRunning() {
				err := mgr.Stop()
				if err != nil {
					t.Logf("Manager %d stop error: %v", idx, err)
				}
			}

			err := mgr.Cleanup()
			if err != nil {
				t.Logf("Manager %d cleanup error: %v", idx, err)
			}
		}(i, m)
	}

	wg.Wait()
}
