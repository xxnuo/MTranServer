package services

import (
	"github.com/shirou/gopsutil/v4/mem"
)

func getAvailableMemoryMB() uint64 {
	v, err := mem.VirtualMemory()
	if err != nil {
		return 0
	}
	return v.Available / 1024 / 1024
}
