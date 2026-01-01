package manager

import (
	"context"
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/xxnuo/MTranServer/internal/logger"
)

type WSMessage struct {
	Type string          `json:"type"`
	Data json.RawMessage `json:"data"`
}

type WSResponse struct {
	Type string          `json:"type"`
	Code int             `json:"code"`
	Msg  string          `json:"msg"`
	Data json.RawMessage `json:"data,omitempty"`
}

type TransRequest struct {
	Text string `json:"text"`
	HTML bool   `json:"html"`
}

type ExitRequest struct {
	Time  int  `json:"time"`
	Force bool `json:"force"`
}

type HealthResponse struct {
	Ready bool `json:"ready"`
}

type TransResponse struct {
	TranslatedText string `json:"translated_text"`
}

type ExitResponse struct {
	Message string `json:"message"`
}

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

type ClientOption func(*Client)

func WithTimeout(timeout time.Duration) ClientOption {
	return func(c *Client) {
		c.timeout = timeout
	}
}

func WithReconnect(reconnect bool) ClientOption {
	return func(c *Client) {
		c.reconnect = reconnect
	}
}

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

func (c *Client) IsConnected() bool {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.connected
}

func (c *Client) sendRequest(ctx context.Context, msgType string, data interface{}) (*WSResponse, error) {
	dataBytes, err := json.Marshal(data)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal data: %w", err)
	}

	msg := WSMessage{
		Type: msgType,
		Data: dataBytes,
	}

	reqCtx, cancel := context.WithTimeout(ctx, c.timeout)
	defer cancel()

	c.mu.Lock()
	defer c.mu.Unlock()

	if !c.connected {
		return nil, fmt.Errorf("not connected")
	}

	if err := c.conn.WriteJSON(msg); err != nil {
		c.connected = false
		return nil, fmt.Errorf("failed to send message: %w", err)
	}

	responseChan := make(chan *WSResponse, 1)
	errChan := make(chan error, 1)

	go func() {
		var resp WSResponse
		if err := c.conn.ReadJSON(&resp); err != nil {
			errChan <- fmt.Errorf("failed to read response: %w", err)
			return
		}
		responseChan <- &resp
	}()

	select {
	case <-reqCtx.Done():
		c.connected = false
		return nil, fmt.Errorf("request timeout")
	case err := <-errChan:
		c.connected = false
		return nil, err
	case resp := <-responseChan:
		return resp, nil
	}
}

func (c *Client) Health(ctx context.Context) (bool, error) {
	resp, err := c.sendRequest(ctx, "health", struct{}{})
	if err != nil {
		return false, err
	}

	if resp.Code != 200 {
		return false, fmt.Errorf("health check failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result HealthResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return false, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	return result.Ready, nil
}

func (c *Client) Trans(ctx context.Context, req TransRequest) (string, error) {
	logger.Debug("Client.Trans: sending request, text length: %d, isHTML: %v, text: %q", len(req.Text), req.HTML, req.Text)
	resp, err := c.sendRequest(ctx, "trans", req)
	if err != nil {
		logger.Debug("Client.Trans: sendRequest error: %v", err)
		return "", err
	}

	if resp.Code != 200 {
		logger.Debug("Client.Trans: response code %d: %s", resp.Code, resp.Msg)
		return "", fmt.Errorf("trans failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result TransResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			logger.Debug("Client.Trans: unmarshal error: %v", err)
			return "", fmt.Errorf("failed to unmarshal response: %w", err)
		}
	}

	logger.Debug("Client.Trans: success, result length: %d", len(result.TranslatedText))
	return result.TranslatedText, nil
}

func (c *Client) Exit(ctx context.Context, req ExitRequest) (*ExitResponse, error) {
	resp, err := c.sendRequest(ctx, "exit", req)
	if err != nil {
		return nil, err
	}

	if resp.Code != 200 {
		return nil, fmt.Errorf("exit failed (code %d): %s", resp.Code, resp.Msg)
	}

	var result ExitResponse
	if resp.Data != nil {
		if err := json.Unmarshal(resp.Data, &result); err != nil {
			return nil, fmt.Errorf("failed to unmarshal response: %w", err)
		}
	} else {
		result.Message = resp.Msg
	}

	return &result, nil
}
