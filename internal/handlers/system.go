package handlers

import (
	"net/http"

	"github.com/gin-gonic/gin"
	"github.com/xxnuo/MTranServer/internal/version"
)

// handleVersion 获取服务版本
// @Summary      获取服务版本
// @Description  返回当前服务的版本号
// @Tags         系统
// @Produce      json
// @Success      200  {object}  map[string]string
// @Router       /version [get]
func HandleVersion(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{
		"server": version.GetVersion(),
		"worker": version.GetWorkerVersion(),
	})
}

// handleHealth 健康检查
// @Summary      健康检查
// @Description  检查服务是否正常运行
// @Tags         系统
// @Produce      json
// @Success      200  {object}  map[string]string
// @Router       /health [get]
func HandleHealth(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{
		"status": "ok",
	})
}

// handleHeartbeat 心跳检查
// @Summary      心跳检查
// @Description  返回服务状态
// @Tags         系统
// @Produce      plain
// @Success      200  {string}  string  "Ready"
// @Router       /__heartbeat__ [get]
func HandleHeartbeat(c *gin.Context) {
	c.String(http.StatusOK, "Ready")
}

// handleLBHeartbeat 负载均衡心跳检查
// @Summary      负载均衡心跳检查
// @Description  返回负载均衡器心跳状态
// @Tags         系统
// @Produce      plain
// @Success      200  {string}  string  "Ready"
// @Router       /__lbheartbeat__ [get]
func HandleLBHeartbeat(c *gin.Context) {
	c.String(http.StatusOK, "Ready")
}
