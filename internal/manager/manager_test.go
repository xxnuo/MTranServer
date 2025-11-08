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

	// 启动 Manager
	err = mgr.Start()
	require.NoError(t, err)

	// 检查状态
	assert.True(t, mgr.IsRunning())
	assert.Equal(t, "running", mgr.Status())

	// 停止 Manager
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

	// 启动
	err = mgr.Start()
	require.NoError(t, err)
	assert.True(t, mgr.IsRunning())

	// 重启
	err = mgr.Restart()
	require.NoError(t, err)
	assert.True(t, mgr.IsRunning())

	// 停止
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
	assert.False(t, ready) // 未加载模型，应该返回 false
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
	args.WorkDir = "../../testdata" // 假设有测试数据目录

	mgr := manager.NewManager(args)
	defer mgr.Cleanup()

	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	// 测试 Poweron（这里会失败，因为没有真实的模型文件）
	_, err = mgr.Poweron(ctx, manager.PoweronRequest{
		Path: "nonexistent",
	})
	assert.Error(t, err) // 预期失败

	// 测试 Poweroff
	_, err = mgr.Poweroff(ctx, manager.PoweroffRequest{
		Time:  0,
		Force: true,
	})
	// Poweroff 可能成功也可能失败，取决于引擎状态
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

	// 测试 Reboot（未加载引擎时会失败）
	_, err = mgr.Reboot(ctx, manager.RebootRequest{
		Time:  0,
		Force: false,
	})
	assert.Error(t, err) // 预期失败
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

	// 测试 Compute（未加载引擎时会失败）
	_, err = mgr.Compute(ctx, manager.ComputeRequest{
		Text: "Hello",
		HTML: false,
	})
	assert.Error(t, err) // 预期失败，因为引擎未加载
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

	// 测试 Translate（未加载引擎时会失败）
	_, err = mgr.Translate(ctx, "Hello")
	assert.Error(t, err) // 预期失败
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

	// 测试 TranslateHTML（未加载引擎时会失败）
	_, err = mgr.TranslateHTML(ctx, "<p>Hello</p>")
	assert.Error(t, err) // 预期失败
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

	// 验证所有 Manager 都在运行
	for i, mgr := range managers {
		assert.True(t, mgr.IsRunning(), "Manager %d should be running", i)
	}

	// 停止所有 Manager
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

	// 未启动时调用方法应该返回错误
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

	// 这是一个完整的工作流测试
	// 需要真实的模型文件才能完全通过
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

	// 1. 启动
	err = mgr.Start()
	require.NoError(t, err)
	defer mgr.Stop()

	time.Sleep(1 * time.Second)

	ctx := context.Background()

	// 2. 检查就绪状态
	ready, err := mgr.Ready(ctx)
	require.NoError(t, err)
	assert.False(t, ready)

	// 3. 加载模型
	resp, err := mgr.Poweron(ctx, manager.PoweronRequest{
		Path: "path/to/model",
	})
	require.NoError(t, err)
	assert.NotNil(t, resp)

	// 4. 等待引擎就绪
	time.Sleep(2 * time.Second)

	ready, err = mgr.Ready(ctx)
	require.NoError(t, err)
	assert.True(t, ready)

	// 5. 翻译文本
	result, err := mgr.Translate(ctx, "Hello, world!")
	require.NoError(t, err)
	assert.NotEmpty(t, result)
	t.Logf("Translation result: %s", result)

	// 6. 翻译 HTML
	htmlResult, err := mgr.TranslateHTML(ctx, "<p>Hello, world!</p>")
	require.NoError(t, err)
	assert.NotEmpty(t, htmlResult)
	t.Logf("HTML translation result: %s", htmlResult)

	// 7. 重启引擎
	rebootResp, err := mgr.Reboot(ctx, manager.RebootRequest{
		Time:  0,
		Force: false,
	})
	require.NoError(t, err)
	assert.NotNil(t, rebootResp)

	// 8. 关闭引擎
	poweroffResp, err := mgr.Poweroff(ctx, manager.PoweroffRequest{
		Time:  0,
		Force: true,
	})
	require.NoError(t, err)
	assert.NotNil(t, poweroffResp)
}
