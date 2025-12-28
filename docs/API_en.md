# MTranServer Advanced Configuration Guide

[中文](../API.md) | [English](API_en.md) | [日本語](API_ja.md) | [Français](API_fr.md) | [Deutsch](API_de.md)

### Environment Variables

| Environment Variable  | Description                              | Default | Options                     |
| --------------------- | ---------------------------------------- | ------- | --------------------------- |
| MT_LOG_LEVEL          | Log level                                | warn    | debug, info, warn, error    |
| MT_CONFIG_DIR         | Configuration directory                  | ~/.config/mtran/server | Any path                    |
| MT_MODEL_DIR          | Model directory                          | ~/.config/mtran/models | Any path                    |
| MT_HOST               | Server host address                      | 0.0.0.0 | Any IP address              |
| MT_PORT               | Server port                              | 8989    | 1-65535                     |
| MT_ENABLE_UI          | Enable Web UI                            | true    | true, false                 |
| MT_OFFLINE            | Offline mode, disable automatic download of new language models, only use downloaded models | false   | true, false                 |
| MT_WORKER_IDLE_TIMEOUT| Worker idle timeout (seconds)            | 300     | Any positive integer        |
| MT_API_TOKEN          | API access token                         | empty   | Any string                  |

Example:

```bash
# Set log level to debug
export MT_LOG_LEVEL=debug

# Set port to 9000
export MT_PORT=9000

# Start the server
./mtranserver
```

### API Documentation

#### System Endpoints

| Endpoint | Method | Description | Auth Required |
| -------- | ------ | ----------- | ------------- |
| `/version` | GET | Get service version | No |
| `/health` | GET | Health check | No |
| `/__heartbeat__` | GET | Heartbeat check | No |
| `/__lbheartbeat__` | GET | Load balancer heartbeat check | No |
| `/docs/*` | GET | Swagger API documentation | No |

#### Translation Endpoints

| Endpoint | Method | Description | Auth Required |
| -------- | ------ | ----------- | ------------- |
| `/languages` | GET | Get supported language list | Yes |
| `/translate` | POST | Single text translation | Yes |
| `/translate/batch` | POST | Batch translation | Yes |

**Single Text Translation Request Example:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**Batch Translation Request Example:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**Authentication Methods:**

- Header: `Authorization: Bearer <token>`
- Query: `?token=<token>`


For details, please refer to the API documentation after the server starts.
