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
| MT_OFFLINE | Use previously prepared translation files offline | false | true, false |
| MT_FAST | Fast mode; faster but may reduce translation accuracy | false | true, false |
| MT_LEXICAL_SHORTLIST | Compatibility alias for MT_FAST | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | Release an idle translation engine after this many seconds | 60 | 1, 2, 3, ... |
| MT_API_TOKEN          | API access token                         | empty   | Any string                  |
| MT_CACHE_SIZE | Recent translation results to keep; 0 disables caching | 1000 | 0, 1, 2, ... |
| MT_MAX_SENTENCE_LENGTH | Long-text splitting limit | 512 | 8, 9, 10, ... |
| MT_FULLWIDTH_ZH_PUNCTUATION | Use fullwidth commas in Chinese output | true | true, false |
| MT_LOG_DIR | Log directory | ~/.config/mtran/logs | Any path |
| MT_LOG_TO_FILE | Write logs to files | false | true, false |
| MT_LOG_CONSOLE | Show console logs | true | true, false |
| MT_LOG_REQUESTS | Log requests | false | true, false |
| MT_CHECK_UPDATE | Check for updates at startup | true | true, false |

An existing local `models` folder is used by default. Prepare the required translation directions online before going offline, and keep both the configuration and model folders.

Example:

```bash
# Set log level to debug
export MT_LOG_LEVEL=debug

# Set port to 9000
export MT_PORT=9000

# Start the server
mtranserver
```

### Fast mode

The default `fast: false` uses the original K9 engine. Opt in with `mtranserver --fast`, `MT_FAST=true`, `"fast": true` in `~/.config/mtran/server.json`, or library configuration `new MTran({ fast: true })`. Use `--no-fast` or `false` to disable it.

Fast mode reuses the lexical shortlist path. It can be faster but may reduce translation accuracy. Models without a lex file fall back to the original K9 engine. Switching modes prepares the model again when needed; initial preparation may require conversion.

Compatibility names remain supported: `--lexical-shortlist` / `--no-lexical-shortlist`, `MT_LEXICAL_SHORTLIST`, and the library/file field `lexicalShortlist`. Precedence is CLI > environment > file > default. Within each layer, `fast` wins over its legacy alias. Library configuration and `setConfig` also prefer explicit `fast`; both fields reflect the same requested mode.

`GET /ui/api/settings` exposes `config.server.fast` and `config.server.cacheSize`. `fast` reports the requested mode, so it remains `true` when a model without lex falls back. Update it with `POST /ui/api/settings/apply` and `{"server":{"fast":true}}`; no additional UI control is required.


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
| `/detect` | POST | Detect text language | Yes |
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
