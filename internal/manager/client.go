package manager

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

// WSMessage WebSocket 消息
type WSMessage struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data"`
}

// WSResponse WebSocket 响应
type WSResponse struct {
	Type string          `json:"type"`
	Code int             `json:"code"`
	Msg  string          `json:"msg"`
	Data json.RawMessage `json:"data,omitempty"`
}

// PoweronRequest poweron 请求参数
type PoweronRequest struct {
	Path                 string   `json:"path,omitempty"`
	ModelPath            string   `json:"model_path,omitempty"`
	LexicalShortlistPath string   `json:"lexical_shortlist_path,omitempty"`
	VocabularyPath       string   `json:"vocabulary_path,omitempty"`
	VocabularyPaths      []string `json:"vocabulary_paths,omitempty"`
}

// PoweroffRequest poweroff 请求参数
type PoweroffRequest struct {
	Time  int  `json:"time"`
	Force bool `json:"force"`
}

// RebootRequest reboot 请求参数
type RebootRequest struct {
	Time  int  `json:"time"`
	Force bool `json:"force"`
}

// ComputeRequest compute 请求参数
type ComputeRequest struct {
	Text string `json:"text"`
	HTML bool   `json:"html"`
}

// ReadyResponse ready 响应数据
type ReadyResponse struct {
	Ready bool `json:"ready"`
}

// ComputeResponse compute 响应数据
type ComputeResponse struct {
	TranslatedText string `json:"translated_text"`
}

// PoweronResponse poweron 响应数据
type PoweronResponse struct {
	Message string `json:"message"`
}

// PoweroffResponse poweroff 响应数据
type PoweroffResponse struct {
	Message string `json:"message"`
}

// RebootResponse reboot 响应数据
type RebootResponse struct {
	Message string `json:"message"`
}

// Client WebSocket 客户端
type Client struct {
	url       string
	conn      *websocket.Conn
	mu        sync.RWMutex
	timeout   time.Duration
	connected bool
	reconnect bool
	closeChan chan struct{}
	closeOnce sync.Once
}

// ClientOption 客户端配置选项
type ClientOption func(*Client)

// WithTimeout 设置请求超时时间
func WithTimeout(timeout time.Duration) ClientOption {
	return func(c *Client) {
		c.timeout = timeout
	}
}

// WithReconnect 设置是否自动重连
func WithReconnect(reconnect bool) ClientOption {
	return func(c *Client) {
		c.reconnect = reconnect
	}
}

// NewClient 创建新的 WebSocket 客户端
func NewClient(url string, opts ...ClientOption) *Client {
	c := &Client{
		url:       url,
		timeout:   30 * time.Second,
		reconnect: false,
		closeChan: make(chan struct{}),
	}

	for _, opt := range opts {
		opt(c)
	}

	return c
}

// Connect 连接到 WebSocket 服务器
func (c *Client) Connect() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	if c.connected {
		return nil
	}

	dialer := websocket.Dialer{
		HandshakeTimeout: c.timeout,
	}

	conn, _, err := dialer.Dial(c.url, nil)
	if err != nil {
		return fmt.Errorf("failed to connect: %w", err)
	}

	c.conn = conn
	c.connected = true

	return nil
}

// Close 关闭连接
func (c *Client) Close() error {
	var err error
	c.closeOnce.Do(func() {
		close(c.closeChan)
		c.mu.Lock()
		defer c.mu.Unlock()

		if c.conn != nil {
			err = c.conn.Close()
			c.connected = false
		}
	})
	return err
}

// IsConnected 检查是否已连接
func (c *Client) IsConnected() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.connected
}

// sendRequest 发送请求并接收响应
func (c *Client) sendRequest(ctx context.Context, msgType string, data interface{}) (*WSResponse, error) {
	c.mu.Lock()
	if !c.connected {
		c.mu.Unlock()
		return nil, fmt.Errorf("not connected")
	}
	c.mu.Unlock()

	// 序列化数据
	dataBytes, err := json.Marshal(data)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal data: %w", err)
	}

	msg := WSMessage{
		Type: msgType,
		Data: dataBytes,
	}

	// 创建带超时的 context
	reqCtx, cancel := context.WithTimeout(ctx, c.timeout)
	defer cancel()

	// 发送消息
	c.mu.Lock()
	if err := c.conn.WriteJSON(msg); err != nil {
		c.mu.Unlock()
		c.connected = false
		return nil, fmt.Errorf("failed to send message: %w", err)
	}
	c.mu.Unlock()

	// 接收响应
	responseChan := make(chan *WSResponse, 1)
	errChan := make(chan error, 1)

	go func() {
		c.mu.Lock()
		defer c.mu.Unlock()

		var resp WSResponse
		if err := c.conn.ReadJSON(&resp); err != nil {
			errChan <- fmt.Errorf("failed to read response: %w", err)
			c.connected = false
			return
		}
		responseChan <- &resp
	}()

	select {
	case <-reqCtx.Done():
		return nil, fmt.Errorf("request timeout")
	case err := <-errChan:
		return nil, err
	case resp := <-responseChan:
		return resp, nil
	}
}

// Poweron 加载翻译引擎
func (c *Client) Poweron(ctx context.Context, req PoweronRequest) (*PoweronResponse, error) {
	resp, err := c.sendRequest(ctx, "poweron", req)
	if err != nil {
		return nil, err
	}

	if resp.Code != 200 {
		return nil, fmt.Errorf("poweron failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result PoweronResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	return &result, nil
}

// Poweroff 关闭服务器
func (c *Client) Poweroff(ctx context.Context, req PoweroffRequest) (*PoweroffResponse, error) {
	resp, err := c.sendRequest(ctx, "poweroff", req)
	if err != nil {
		return nil, err
	}

	// poweroff 可能返回 1101 (等待任务完成)，这也是成功的
	if resp.Code != 200 && resp.Code != 1101 {
		return nil, fmt.Errorf("poweroff failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result PoweroffResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	} else {
		result.Message = resp.Msg
	}

	return &result, nil
}

// Reboot 重启引擎
func (c *Client) Reboot(ctx context.Context, req RebootRequest) (*RebootResponse, error) {
	resp, err := c.sendRequest(ctx, "reboot", req)
	if err != nil {
		return nil, err
	}

	if resp.Code != 200 {
		return nil, fmt.Errorf("reboot failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result RebootResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	return &result, nil
}

// Ready 检查引擎是否就绪
func (c *Client) Ready(ctx context.Context) (bool, error) {
	resp, err := c.sendRequest(ctx, "ready", struct{}{})
	if err != nil {
		return false, err
	}

	if resp.Code != 200 {
		return false, fmt.Errorf("ready check failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result ReadyResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return false, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	return result.Ready, nil
}

// Compute 翻译文本
func (c *Client) Compute(ctx context.Context, req ComputeRequest) (string, error) {
	resp, err := c.sendRequest(ctx, "compute", req)
	if err != nil {
		return "", err
	}

	if resp.Code != 200 {
		return "", fmt.Errorf("compute failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result ComputeResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return "", fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	return result.TranslatedText, nil
}
