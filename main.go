package main

import (
	"embed"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path/filepath"
	"syscall"
)

//go:embed bin/mts
var mtsBin embed.FS

func extractMts() (string, error) {
	// 创建.bin目录
	binDir := ".bin"
	if err := os.MkdirAll(binDir, 0755); err != nil {
		return "", fmt.Errorf("创建%s目录失败: %v", binDir, err)
	}

	mtsPath := filepath.Join(binDir, "mts")

	// 检查文件是否已存在,不存在则创建
	if _, err := os.Stat(mtsPath); err != nil {
		// 读取嵌入的mts二进制文件
		mtsData, err := mtsBin.ReadFile("bin/mts")
		if err != nil {
			return "", fmt.Errorf("读取嵌入的 mts 文件失败: %v", err)
		}

		// 写入到.bin目录
		if err := os.WriteFile(mtsPath, mtsData, 0755); err != nil {
			return "", fmt.Errorf("写入 mts 文件失败: %v", err)
		}
	}

	return mtsPath, nil
}

func main() {
	mtsPath, err := extractMts()
	if err != nil {
		fmt.Printf("提取 mts 文件失败: %v\n", err)
		return
	}

	// 创建用于控制子进程的channel
	stopChan := make(chan struct{})
	doneChan := make(chan struct{})

	// 在goroutine中启动mts程序
	go func() {
		cmd := exec.Command(mtsPath)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr

		fmt.Println("正在启动 mts 程序...")
		if err := cmd.Start(); err != nil {
			fmt.Printf("启动 mts 程序失败: %v\n", err)
			close(doneChan)
			return
		}

		fmt.Printf("mts 程序已在后台启动，进程ID: %d\n", cmd.Process.Pid)

		// 等待停止信号
		<-stopChan

		// 终止子进程
		if cmd.Process != nil {
			cmd.Process.Kill()
			fmt.Printf("已终止 mts 进程(PID: %d)\n", cmd.Process.Pid)
		}
		close(doneChan)
	}()

	// 主程序可以在这里执行其他任务
	fmt.Println("主程序继续执行其他任务...")

	// 等待中断信号
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	fmt.Println("\n收到终止信号，正在关闭程序...")
	close(stopChan) // 通知goroutine终止子进程
	<-doneChan      // 等待goroutine完成清理工作
}
