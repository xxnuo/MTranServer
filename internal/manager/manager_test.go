package manager_test

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestManager_StartStop(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.LogLevel = "debug"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)

	assert.True(t, mgr.IsRunning())
	assert.Equal(t, "running", mgr.Status())

	err = mgr.Stop()
	require.NoError(t, err)

	assert.False(t, mgr.IsRunning())
}

func TestManager_Restart(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	assert.True(t, mgr.IsRunning())

	err = mgr.Restart()
	require.NoError(t, err)
	assert.True(t, mgr.IsRunning())

	err = mgr.Stop()
	require.NoError(t, err)
}

func TestManager_Logs(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	logs := mgr.Logs()
	assert.NotEmpty(t, logs)
	t.Logf("Collected %d log lines", len(logs))
}

func TestManager_Ready(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()
	ready, err := mgr.Ready(ctx)
	require.NoError(t, err)
	assert.False(t, ready)
}

func TestManager_PoweronPoweroff(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.WorkDir = "../../testdata"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	_, err = mgr.Poweron(ctx, manager.PoweronRequest{
		Path: "nonexistent",
	})
	assert.Error(t, err)

	_, err = mgr.Poweroff(ctx, manager.PoweroffRequest{
		Time:  0,
		Force: true,
	})

	t.Logf("Poweroff result: %v", err)
}

func TestManager_Reboot(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	_, err = mgr.Reboot(ctx, manager.RebootRequest{
		Time:  0,
		Force: false,
	})
	assert.Error(t, err)
}

func TestManager_Compute(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	_, err = mgr.Compute(ctx, manager.ComputeRequest{
		Text: "Hello",
		HTML: false,
	})
	assert.Error(t, err)
}

func TestManager_Translate(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	_, err = mgr.Translate(ctx, "Hello")
	assert.Error(t, err)
}

func TestManager_TranslateHTML(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	_, err = mgr.TranslateHTML(ctx, "<p>Hello</p>")
	assert.Error(t, err)
}

func TestManager_MultipleManagers(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	managers := make([]*manager.Manager, 0, 3)

	for i := 0; i < 3; i++ {
		port, err := utils.GetFreePort()
		require.NoError(t, err)

		args := manager.NewWorkerArgs()
		args.Port = port
		args.Host = "127.0.0.1"
		args.EnableWebSocket = true

		mgr := manager.NewManager(args)
		managers = append(managers, mgr)

		err = mgr.Start()
		require.NoError(t, err)
		t.Logf("Manager %d started on port %d", i, port)
	}

	time.Sleep(2 * time.Second)

	for i, mgr := range managers {
		assert.True(t, mgr.IsRunning(), "Manager %d should be running", i)
	}

	for i, mgr := range managers {
		err := mgr.Stop()
		assert.NoError(t, err)
		mgr.Cleanup()
		t.Logf("Manager %d stopped", i)
	}
}

func TestManager_NotStarted(t *testing.T) {
	args := manager.NewWorkerArgs()
	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	ctx := context.Background()

	_, err := mgr.Ready(ctx)
	assert.Error(t, err)

	_, err = mgr.Translate(ctx, "Hello")
	assert.Error(t, err)

	assert.False(t, mgr.IsRunning())
}

func TestManager_FullWorkflow(t *testing.T) {
	if testing.Short() {
		t.Skip("Skipping integration test in short mode")
	}

	t.Skip("Requires real model files")

	args := manager.NewWorkerArgs()
	port, err := utils.GetFreePort()
	require.NoError(t, err)
	args.Port = port
	args.Host = "127.0.0.1"
	args.EnableWebSocket = true
	args.WorkDir = "../../testdata"

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	ready, err := mgr.Ready(ctx)
	require.NoError(t, err)
	assert.False(t, ready)

	resp, err := mgr.Poweron(ctx, manager.PoweronRequest{
		Path: "path/to/model",
	})
	require.NoError(t, err)
	assert.NotNil(t, resp)

	time.Sleep(2 * time.Second)

	ready, err = mgr.Ready(ctx)
	require.NoError(t, err)
	assert.True(t, ready)

	result, err := mgr.Translate(ctx, "Hello, world!")
	require.NoError(t, err)
	assert.NotEmpty(t, result)
	t.Logf("Translation result: %s", result)

	htmlResult, err := mgr.TranslateHTML(ctx, "<p>Hello, world!</p>")
	require.NoError(t, err)
	assert.NotEmpty(t, htmlResult)
	t.Logf("HTML translation result: %s", htmlResult)

	rebootResp, err := mgr.Reboot(ctx, manager.RebootRequest{
		Time:  0,
		Force: false,
	})
	require.NoError(t, err)
	assert.NotNil(t, rebootResp)

	poweroffResp, err := mgr.Poweroff(ctx, manager.PoweroffRequest{
		Time:  0,
		Force: true,
	})
	require.NoError(t, err)
	assert.NotNil(t, poweroffResp)
}
