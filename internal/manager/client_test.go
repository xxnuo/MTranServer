package manager_test

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	"github.com/xxnuo/MTranServer/internal/manager"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func mockWSServer(t *testing.T, handler func(*websocket.Conn)) *httptest.Server {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		conn, err := upgrader.Upgrade(w, r, nil)
		require.NoError(t, err)
		defer conn.Close()
		handler(conn)
	}))
	return server
}

func handleEcho(conn *websocket.Conn) {
	for {
		var msg manager.WSMessage
		if err := conn.ReadJSON(&msg); err != nil {
			break
		}

		resp := manager.WSResponse{
			Type: msg.Type,
			Code: 200,
			Msg:  "success",
			Data: msg.Data,
		}

		if err := conn.WriteJSON(resp); err != nil {
			break
		}
	}
}

func handleHealth(conn *websocket.Conn, ready bool) {
	var msg manager.WSMessage
	if err := conn.ReadJSON(&msg); err != nil {
		return
	}

	resp := manager.WSResponse{
		Type: "health",
		Code: 200,
		Msg:  "success",
	}

	data := map[string]bool{"ready": ready}
	resp.Data, _ = json.Marshal(data)

	conn.WriteJSON(resp)
}

func handleTrans(conn *websocket.Conn) {
	var msg manager.WSMessage
	if err := conn.ReadJSON(&msg); err != nil {
		return
	}

	var req manager.TransRequest
	json.Unmarshal(msg.Data, &req)

	resp := manager.WSResponse{
		Type: "trans",
		Code: 200,
		Msg:  "success",
	}

	if req.Text == "" {
		resp.Code = 400
		resp.Msg = "text is required"
	} else {
		data := map[string]string{"translated_text": "translated: " + req.Text}
		resp.Data, _ = json.Marshal(data)
	}

	conn.WriteJSON(resp)
}

func TestClient_Connect(t *testing.T) {
	server := mockWSServer(t, handleEcho)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	assert.NoError(t, err)
	assert.True(t, client.IsConnected())
}

func TestClient_ConnectTwice(t *testing.T) {
	server := mockWSServer(t, handleEcho)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	err = client.Connect()
	assert.NoError(t, err)
	assert.True(t, client.IsConnected())
}

func TestClient_Poweron(t *testing.T) {
	server := mockWSServer(t, handlePoweron)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	resp, err := client.Poweron(ctx, manager.PoweronRequest{
		Path: "/path/to/model",
	})

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, "Engine loaded successfully", resp.Message)
}

func TestClient_Poweron_InvalidParams(t *testing.T) {
	server := mockWSServer(t, handlePoweron)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	_, err = client.Poweron(ctx, manager.PoweronRequest{})

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "path is required")
}

func TestClient_Ready(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {
		handleReady(conn, true)
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	ready, err := client.Ready(ctx)

	assert.NoError(t, err)
	assert.True(t, ready)
}

func TestClient_Ready_NotReady(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {
		handleReady(conn, false)
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	ready, err := client.Ready(ctx)

	assert.NoError(t, err)
	assert.False(t, ready)
}

func TestClient_Compute(t *testing.T) {
	server := mockWSServer(t, handleCompute)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	result, err := client.Compute(ctx, manager.ComputeRequest{
		Text: "Hello",
		HTML: false,
	})

	assert.NoError(t, err)
	assert.Equal(t, "翻译结果: Hello", result)
}

func TestClient_Compute_EmptyText(t *testing.T) {
	server := mockWSServer(t, handleCompute)
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	_, err = client.Compute(ctx, manager.ComputeRequest{
		Text: "",
	})

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "text is required")
}

func TestClient_Timeout(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {

		time.Sleep(5 * time.Second)
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL, manager.WithTimeout(100*time.Millisecond))
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	_, err = client.Ready(ctx)

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "timeout")
}

func TestClient_NotConnected(t *testing.T) {
	client := manager.NewClient("ws://localhost:9999")
	defer client.Close()

	ctx := context.Background()
	_, err := client.Ready(ctx)

	assert.Error(t, err)
	assert.Contains(t, err.Error(), "not connected")
}

func TestClient_Poweroff(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {
		var msg manager.WSMessage
		if err := conn.ReadJSON(&msg); err != nil {
			return
		}

		data := map[string]string{"message": "Server is shutting down"}
		dataBytes, _ := json.Marshal(data)

		resp := manager.WSResponse{
			Type: "poweroff",
			Code: 200,
			Msg:  "success",
			Data: dataBytes,
		}

		conn.WriteJSON(resp)
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	resp, err := client.Poweroff(ctx, manager.PoweroffRequest{
		Time:  0,
		Force: true,
	})

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, "Server is shutting down", resp.Message)
}

func TestClient_Reboot(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {
		var msg manager.WSMessage
		if err := conn.ReadJSON(&msg); err != nil {
			return
		}

		data := map[string]string{"message": "Engine rebooted successfully"}
		dataBytes, _ := json.Marshal(data)

		resp := manager.WSResponse{
			Type: "reboot",
			Code: 200,
			Msg:  "success",
			Data: dataBytes,
		}

		conn.WriteJSON(resp)
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()
	resp, err := client.Reboot(ctx, manager.RebootRequest{
		Time:  0,
		Force: false,
	})

	assert.NoError(t, err)
	assert.NotNil(t, resp)
	assert.Equal(t, "Engine rebooted successfully", resp.Message)
}

func TestClient_MultipleRequests(t *testing.T) {
	server := mockWSServer(t, func(conn *websocket.Conn) {

		for i := 0; i < 3; i++ {
			var msg manager.WSMessage
			if err := conn.ReadJSON(&msg); err != nil {
				break
			}

			var req manager.ComputeRequest
			json.Unmarshal(msg.Data, &req)

			data := map[string]string{"translated_text": "翻译结果: " + req.Text}
			dataBytes, _ := json.Marshal(data)

			resp := manager.WSResponse{
				Type: "compute",
				Code: 200,
				Msg:  "success",
				Data: dataBytes,
			}

			if err := conn.WriteJSON(resp); err != nil {
				break
			}
		}
	})
	defer server.Close()

	wsURL := "ws" + server.URL[4:]

	client := manager.NewClient(wsURL)
	defer client.Close()

	err := client.Connect()
	require.NoError(t, err)

	ctx := context.Background()

	for i := 1; i <= 3; i++ {
		result, err := client.Compute(ctx, manager.ComputeRequest{
			Text: "Test " + string(rune('0'+i)),
		})
		assert.NoError(t, err)
		assert.Contains(t, result, "翻译结果:")
	}
}
