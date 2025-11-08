package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"

	swaggerFiles "github.com/swaggo/files"
	ginSwagger "github.com/swaggo/gin-swagger"

	"github.com/xxnuo/MTranServer/internal/config"
	"github.com/xxnuo/MTranServer/internal/docs"
	"github.com/xxnuo/MTranServer/internal/manager"
	"github.com/xxnuo/MTranServer/internal/models"
	"github.com/xxnuo/MTranServer/internal/utils"
)

const Version = "v3.0.0"

// @title           MTranServer API
// @version         3.0.0
// @description     超低资源消耗超快的离线翻译服务器 API
// @termsOfService  https://github.com/xxnuo/MTranServer

// @contact.name   API Support
// @contact.url    https://github.com/xxnuo/MTranServer/issues
// @contact.email  support@example.com

// @license.name  MIT
// @license.url   https://github.com/xxnuo/MTranServer/blob/main/LICENSE

// @host      localhost:8989
// @BasePath  /

// @securityDefinitions.apikey ApiKeyAuth
// @in header
// @name Authorization

// @securityDefinitions.apikey ApiKeyQuery
// @in query
// @name token

var (
	apiToken string
	mgr      *manager.Manager
	mgrMu    sync.RWMutex
	// 存储已加载的翻译引擎 key: "fromLang-toLang"
	engines = make(map[string]*manager.Manager)
	engMu   sync.RWMutex
)

func main() {
	// 加载配置
	cfg := config.GetConfig()

	// 初始化 records
	if err := models.InitRecords(); err != nil {
		log.Fatalf("Failed to initialize records: %v", err)
	}

	// 创建必要的目录
	if err := os.MkdirAll(cfg.ModelDir, 0755); err != nil {
		log.Fatalf("Failed to create model directory: %v", err)
	}

	// 获取 API Token
	apiToken = utils.GetEnv("API_TOKEN", utils.GetEnv("CORE_API_TOKEN", ""))

	// 设置 Gin 模式
	if cfg.LogLevel == "debug" {
		gin.SetMode(gin.DebugMode)
	} else {
		gin.SetMode(gin.ReleaseMode)
	}

	// 创建 Gin 路由
	r := gin.Default()

	// 添加 CORS 中间件
	r.Use(corsMiddleware())

	// 配置 Swagger
	docs.SwaggerInfo.BasePath = "/"
	r.GET("/docs/*any", ginSwagger.WrapHandler(swaggerFiles.Handler))

	// 注册路由
	registerRoutes(r)

	// 启动服务器
	addr := fmt.Sprintf("%s:%s", cfg.Host, cfg.Port)
	srv := &http.Server{
		Addr:    addr,
		Handler: r,
	}

	// 优雅关闭
	go func() {
		sigChan := make(chan os.Signal, 1)
		signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
		<-sigChan

		log.Println("Shutting down server...")

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		// 关闭所有翻译引擎
		engMu.Lock()
		for key, m := range engines {
			log.Printf("Stopping engine: %s", key)
			if err := m.Cleanup(); err != nil {
				log.Printf("Failed to cleanup engine %s: %v", key, err)
			}
		}
		engMu.Unlock()

		if err := srv.Shutdown(ctx); err != nil {
			log.Fatalf("Server forced to shutdown: %v", err)
		}
	}()

	log.Printf("HTTP Service URL: http://%s", addr)
	log.Printf("Swagger UI: http://%s/docs/index.html", addr)

	if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatalf("Failed to start server: %v", err)
	}
}

func registerRoutes(r *gin.Engine) {
	// 无需认证的路由
	r.GET("/version", handleVersion)
	r.GET("/health", handleHealth)
	r.GET("/__heartbeat__", handleHeartbeat)
	r.GET("/__lbheartbeat__", handleLBHeartbeat)

	// 需要认证的路由
	auth := r.Group("/")
	if apiToken != "" {
		auth.Use(authMiddleware())
	}

	auth.GET("/languages", handleLanguages)
	auth.POST("/translate", handleTranslate)
	auth.POST("/translate/batch", handleTranslateBatch)
	auth.POST("/language/translate/v2", handleGoogleCompatTranslate)

	// 插件兼容接口
	r.POST("/imme", handleImmeTranslate)
	r.POST("/kiss", handleKissTranslate)
}

// CORS 中间件
func corsMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		c.Writer.Header().Set("Access-Control-Allow-Origin", "*")
		c.Writer.Header().Set("Access-Control-Allow-Credentials", "true")
		c.Writer.Header().Set("Access-Control-Allow-Headers", "Content-Type, Content-Length, Accept-Encoding, X-CSRF-Token, Authorization, accept, origin, Cache-Control, X-Requested-With, KEY")
		c.Writer.Header().Set("Access-Control-Allow-Methods", "POST, OPTIONS, GET, PUT, DELETE")

		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(http.StatusNoContent)
			return
		}

		c.Next()
	}
}

// 认证中间件
func authMiddleware() gin.HandlerFunc {
	return func(c *gin.Context) {
		token := c.GetHeader("Authorization")
		if token == "" {
			token = c.Query("token")
		}

		if token != apiToken {
			c.JSON(http.StatusUnauthorized, gin.H{
				"error": "Unauthorized",
			})
			c.Abort()
			return
		}

		c.Next()
	}
}

// handleVersion 获取服务版本
// @Summary      获取服务版本
// @Description  返回当前服务的版本号
// @Tags         系统
// @Produce      json
// @Success      200  {object}  map[string]string
// @Router       /version [get]
func handleVersion(c *gin.Context) {
	c.JSON(http.StatusOK, gin.H{
		"version": Version,
	})
}

// handleHealth 健康检查
// @Summary      健康检查
// @Description  检查服务是否正常运行
// @Tags         系统
// @Produce      json
// @Success      200  {object}  map[string]string
// @Router       /health [get]
func handleHealth(c *gin.Context) {
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
func handleHeartbeat(c *gin.Context) {
	c.String(http.StatusOK, "Ready")
}

// handleLBHeartbeat 负载均衡心跳检查
// @Summary      负载均衡心跳检查
// @Description  返回负载均衡器心跳状态
// @Tags         系统
// @Produce      plain
// @Success      200  {string}  string  "Ready"
// @Router       /__lbheartbeat__ [get]
func handleLBHeartbeat(c *gin.Context) {
	c.String(http.StatusOK, "Ready")
}

// handleLanguages 获取支持的语言列表
// @Summary      获取支持的语言列表
// @Description  返回所有支持的翻译语言代码
// @Tags         翻译
// @Produce      json
// @Success      200  {object}  map[string][]string
// @Failure      500  {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /languages [get]
func handleLanguages(c *gin.Context) {
	if models.GlobalRecords == nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Records not initialized",
		})
		return
	}

	// 从 records 中提取所有支持的语言
	langMap := make(map[string]bool)
	for _, record := range models.GlobalRecords.Data {
		langMap[record.FromLang] = true
		langMap[record.ToLang] = true
	}

	languages := make([]string, 0, len(langMap))
	for lang := range langMap {
		languages = append(languages, lang)
	}

	c.JSON(http.StatusOK, gin.H{
		"languages": languages,
	})
}

// TranslateRequest 翻译请求
type TranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-Hans"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
	HTML bool   `json:"html" example:"false"`
}

// TranslateResponse 翻译响应
type TranslateResponse struct {
	Result string `json:"result" example:"你好，世界！"`
}

// handleTranslate 单文本翻译
// @Summary      单文本翻译
// @Description  翻译单个文本
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      TranslateRequest  true  "翻译请求"
// @Success      200      {object}  TranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /translate [post]
func handleTranslate(c *gin.Context) {
	var req TranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := getOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()

	var result string
	if req.HTML {
		result, err = m.TranslateHTML(ctx, req.Text)
	} else {
		result, err = m.Translate(ctx, req.Text)
	}

	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"result": result,
	})
}

// TranslateBatchRequest 批量翻译请求
type TranslateBatchRequest struct {
	From  string   `json:"from" binding:"required" example:"en"`
	To    string   `json:"to" binding:"required" example:"zh-Hans"`
	Texts []string `json:"texts" binding:"required" example:"Hello, world!,Good morning!"`
	HTML  bool     `json:"html" example:"false"`
}

// TranslateBatchResponse 批量翻译响应
type TranslateBatchResponse struct {
	Results []string `json:"results" example:"你好，世界！,早上好！"`
}

// handleTranslateBatch 批量翻译
// @Summary      批量翻译
// @Description  批量翻译多个文本
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      TranslateBatchRequest  true  "批量翻译请求"
// @Success      200      {object}  TranslateBatchResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /translate/batch [post]
func handleTranslateBatch(c *gin.Context) {
	var req TranslateBatchRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := getOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 批量翻译
	results := make([]string, len(req.Texts))
	ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
	defer cancel()

	for i, text := range req.Texts {
		var result string
		if req.HTML {
			result, err = m.TranslateHTML(ctx, text)
		} else {
			result, err = m.Translate(ctx, text)
		}

		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
			})
			return
		}
		results[i] = result
	}

	c.JSON(http.StatusOK, gin.H{
		"results": results,
	})
}

// GoogleTranslateRequest Google 翻译兼容请求
type GoogleTranslateRequest struct {
	Q      string `json:"q" binding:"required" example:"The Great Pyramid of Giza"`
	Source string `json:"source" binding:"required" example:"en"`
	Target string `json:"target" binding:"required" example:"zh-Hans"`
	Format string `json:"format" example:"text"`
}

// GoogleTranslateResponse Google 翻译兼容响应
type GoogleTranslateResponse struct {
	Data struct {
		Translations []struct {
			TranslatedText string `json:"translatedText" example:"吉萨大金字塔"`
		} `json:"translations"`
	} `json:"data"`
}

// handleGoogleCompatTranslate Google 翻译兼容接口
// @Summary      Google 翻译兼容接口
// @Description  兼容 Google Translate API v2 的翻译接口
// @Tags         翻译
// @Accept       json
// @Produce      json
// @Param        request  body      GoogleTranslateRequest  true  "Google 翻译请求"
// @Success      200      {object}  GoogleTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Security     ApiKeyAuth
// @Security     ApiKeyQuery
// @Router       /language/translate/v2 [post]
func handleGoogleCompatTranslate(c *gin.Context) {
	var req GoogleTranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := getOrCreateEngine(req.Source, req.Target)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()

	isHTML := req.Format == "html"
	var result string
	if isHTML {
		result, err = m.TranslateHTML(ctx, req.Q)
	} else {
		result, err = m.Translate(ctx, req.Q)
	}

	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"data": gin.H{
			"translations": []gin.H{
				{
					"translatedText": result,
				},
			},
		},
	})
}

// ImmeTranslateRequest 沉浸式翻译请求
type ImmeTranslateRequest struct {
	From  string   `json:"from" binding:"required" example:"en"`
	To    string   `json:"to" binding:"required" example:"zh-Hans"`
	Trans []string `json:"trans" binding:"required" example:"Hello, world!,Good morning!"`
}

// ImmeTranslateResponse 沉浸式翻译响应
type ImmeTranslateResponse struct {
	Trans []string `json:"trans" example:"你好，世界！,早上好！"`
}

// handleImmeTranslate 沉浸式翻译插件接口
// @Summary      沉浸式翻译插件接口
// @Description  为沉浸式翻译插件提供的翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        token    query     string                  false  "API Token"
// @Param        request  body      ImmeTranslateRequest    true   "沉浸式翻译请求"
// @Success      200      {object}  ImmeTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /imme [post]
func handleImmeTranslate(c *gin.Context) {
	// 检查 token
	if apiToken != "" {
		token := c.Query("token")
		if token != apiToken {
			c.JSON(http.StatusUnauthorized, gin.H{
				"error": "Unauthorized",
			})
			return
		}
	}

	var req ImmeTranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := getOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 批量翻译
	results := make([]string, len(req.Trans))
	ctx, cancel := context.WithTimeout(c.Request.Context(), 60*time.Second)
	defer cancel()

	for i, text := range req.Trans {
		result, err := m.Translate(ctx, text)
		if err != nil {
			c.JSON(http.StatusInternalServerError, gin.H{
				"error": fmt.Sprintf("Translation failed at index %d: %v", i, err),
			})
			return
		}
		results[i] = result
	}

	c.JSON(http.StatusOK, gin.H{
		"trans": results,
	})
}

// KissTranslateRequest 简约翻译请求
type KissTranslateRequest struct {
	From string `json:"from" binding:"required" example:"en"`
	To   string `json:"to" binding:"required" example:"zh-Hans"`
	Text string `json:"text" binding:"required" example:"Hello, world!"`
}

// KissTranslateResponse 简约翻译响应
type KissTranslateResponse struct {
	Text string `json:"text" example:"你好，世界！"`
}

// handleKissTranslate 简约翻译插件接口
// @Summary      简约翻译插件接口
// @Description  为简约翻译插件提供的翻译接口
// @Tags         插件
// @Accept       json
// @Produce      json
// @Param        KEY      header    string                false  "API Token"
// @Param        request  body      KissTranslateRequest  true   "简约翻译请求"
// @Success      200      {object}  KissTranslateResponse
// @Failure      400      {object}  map[string]string
// @Failure      401      {object}  map[string]string
// @Failure      500      {object}  map[string]string
// @Router       /kiss [post]
func handleKissTranslate(c *gin.Context) {
	// 检查 token
	if apiToken != "" {
		token := c.GetHeader("KEY")
		if token != apiToken {
			c.JSON(http.StatusUnauthorized, gin.H{
				"error": "Unauthorized",
			})
			return
		}
	}

	var req KissTranslateRequest

	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"error": err.Error(),
		})
		return
	}

	// 获取或创建翻译引擎
	m, err := getOrCreateEngine(req.From, req.To)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Failed to get engine: %v", err),
		})
		return
	}

	// 翻译
	ctx, cancel := context.WithTimeout(c.Request.Context(), 30*time.Second)
	defer cancel()

	result, err := m.Translate(ctx, req.Text)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": fmt.Sprintf("Translation failed: %v", err),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"text": result,
	})
}

// getOrCreateEngine 获取或创建翻译引擎
func getOrCreateEngine(fromLang, toLang string) (*manager.Manager, error) {
	key := fmt.Sprintf("%s-%s", fromLang, toLang)

	// 检查是否已存在
	engMu.RLock()
	if m, ok := engines[key]; ok {
		if m.IsRunning() {
			engMu.RUnlock()
			return m, nil
		}
	}
	engMu.RUnlock()

	// 创建新引擎
	engMu.Lock()
	defer engMu.Unlock()

	// 再次检查（双重检查锁定）
	if m, ok := engines[key]; ok {
		if m.IsRunning() {
			return m, nil
		}
	}

	log.Printf("Creating new engine for %s -> %s", fromLang, toLang)

	// 下载模型（如果需要）
	cfg := config.GetConfig()
	if cfg.EnableOfflineMode {
		log.Printf("Offline mode enabled, skipping model download")
	} else {
		log.Printf("Downloading model for %s -> %s", fromLang, toLang)
		if err := models.DownloadModel(toLang, fromLang, ""); err != nil {
			return nil, fmt.Errorf("failed to download model: %w", err)
		}
	}

	// 查找模型文件
	modelFiles, err := models.GetModelFiles(cfg.ModelDir, fromLang, toLang)
	if err != nil {
		return nil, fmt.Errorf("failed to find model files: %w", err)
	}

	// 创建 Worker
	port := 8988 + len(engines) // 动态分配端口
	args := manager.NewWorkerArgs()
	args.Port = port
	args.WorkDir = cfg.ModelDir

	m := manager.NewManager(args)

	// 启动 Manager
	if err := m.Start(); err != nil {
		return nil, fmt.Errorf("failed to start manager: %w", err)
	}

	// 加载模型
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	// 提取文件名（相对于 WorkDir）
	poweronReq := manager.PoweronRequest{
		ModelPath:            filepath.Base(modelFiles["model"]),
		LexicalShortlistPath: filepath.Base(modelFiles["lex"]),
		VocabularyPaths:      []string{filepath.Base(modelFiles["vocab_src"]), filepath.Base(modelFiles["vocab_trg"])},
	}

	if _, err := m.Poweron(ctx, poweronReq); err != nil {
		m.Cleanup()
		return nil, fmt.Errorf("failed to load model: %w", err)
	}

	// 等待引擎就绪
	for i := 0; i < 30; i++ {
		ready, err := m.Ready(ctx)
		if err == nil && ready {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}

	engines[key] = m
	log.Printf("Engine created successfully for %s -> %s", fromLang, toLang)

	return m, nil
}
