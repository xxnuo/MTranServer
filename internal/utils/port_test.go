package utils_test

import (
	"testing"

	"github.com/xxnuo/MTranServer/internal/utils"
)

func TestGetFreePort(t *testing.T) {
	port, err := utils.GetFreePort()
	if err != nil {
		t.Fatalf("Failed to get free port: %v", err)
	}
	t.Logf("Free port: %d", port)
}
