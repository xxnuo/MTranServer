# MTranServer 高级配置说明

[中文](API.md) | [English](docs/API_en.md) | [日本語](docs/API_ja.md) | [Français](docs/API_fr.md) | [Deutsch](docs/API_de.md)

### 环境变量配置

| 环境变量              | 说明                                     | 默认值 | 可选值                      |
| --------------------- | ---------------------------------------- | ------ | --------------------------- |
| MT_LOG_LEVEL          | 日志级别                                 | warn   | debug, info, warn, error    |
| MT_CONFIG_DIR         | 配置目录                                 | ~/.config/mtran/server | 任意路径                    |
| MT_MODEL_DIR          | 模型目录                                 | ~/.config/mtran/models | 任意路径                    |
| MT_HOST               | 服务器监听地址                           | 0.0.0.0| 任意 IP 地址                |
| MT_PORT               | 服务器端口                               | 8989   | 1-65535                     |
| MT_ENABLE_UI          | 启用 Web UI                              | true   | true, false                 |
| MT_OFFLINE | 离线使用已准备好的翻译文件 | false | true, false |
| MT_FAST | 快速模式；更快但可能降低翻译准确性 | false | true, false |
| MT_LEXICAL_SHORTLIST | MT_FAST 的兼容别名 | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | 翻译引擎空闲多久后释放内存（秒） | 60 | 1, 2, 3, ... |
| MT_API_TOKEN          | API 访问令牌                             | 空     | 任意字符串                  |
| MT_CACHE_SIZE | 保存最近多少条翻译结果；0 为关闭 | 1000 | 0, 1, 2, ... |
| MT_MAX_SENTENCE_LENGTH | 长文本分段长度 | 512 | 8, 9, 10, ... |
| MT_FULLWIDTH_ZH_PUNCTUATION | 中文输出逗号全角化 | true | true, false |
| MT_LOG_DIR | 日志目录 | ~/.config/mtran/logs | 任意路径 |
| MT_LOG_TO_FILE | 将日志写入文件 | false | true, false |
| MT_LOG_CONSOLE | 在控制台显示日志 | true | true, false |
| MT_LOG_REQUESTS | 记录请求日志 | false | true, false |
| MT_CHECK_UPDATE | 启动时检查更新 | true | true, false |

有本地 `models` 文件夹时会优先使用它。离线使用前，请先联网准备需要的翻译方向，并保留配置和模型两个文件夹。

示例：

```bash
# 设置日志级别为 debug
export MT_LOG_LEVEL=debug

# 设置端口为 9000
export MT_PORT=9000

# 启动服务
mtranserver
```

### 快速模式

默认 `fast: false`，使用原 K9 引擎。可通过 `mtranserver --fast`、`MT_FAST=true`、配置文件 `~/.config/mtran/server.json` 中的 `"fast": true`，或库配置 `new MTran({ fast: true })` 显式开启。使用 `--no-fast` 或 `false` 关闭。

快速模式复用 lexical shortlist 路径，可能更快，但也可能降低翻译准确性；没有 lex 文件的模型自动回退原 K9。切换模式时会按需重新准备模型，首次准备可能需要转换。

兼容 `--lexical-shortlist` / `--no-lexical-shortlist`、`MT_LEXICAL_SHORTLIST` 和库/文件字段 `lexicalShortlist`。优先级为 CLI > 环境变量 > 文件 > 默认值；同层同时指定时，`fast` 优先于旧别名。库配置及 `setConfig` 同样优先采用显式 `fast`，两个字段始终反映同一请求模式。

`GET /ui/api/settings` 返回 `config.server.fast` 和 `config.server.cacheSize`；`fast` 表示请求模式，无 lex 回退时仍为 `true`。通过 `POST /ui/api/settings/apply` 的 `{"server":{"fast":true}}` 可更新模式，无需新增 UI 控件。


### API 接口说明

#### 系统接口

| 接口 | 方法 | 说明 | 认证 |
| ---- | ---- | ---- | ---- |
| `/version` | GET | 获取服务版本 | 否 |
| `/health` | GET | 健康检查 | 否 |
| `/__heartbeat__` | GET | 心跳检查 | 否 |
| `/__lbheartbeat__` | GET | 负载均衡心跳检查 | 否 |
| `/docs/*` | GET | Swagger API 文档 | 否 |

#### 翻译接口

| 接口 | 方法 | 说明 | 认证 |
| ---- | ---- | ---- | ---- |
| `/languages` | GET | 获取支持的语言列表 | 是 |
| `/detect` | POST | 检测文本语言 | 是 |
| `/translate` | POST | 单文本翻译 | 是 |
| `/translate/batch` | POST | 批量翻译 | 是 |

**单文本翻译请求示例：**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**批量翻译请求示例：**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**认证方式：**

- Header: `Authorization: Bearer <token>`
- Query: `?token=<token>`


详细内容请参考服务器启动后的 API 文档内容。
