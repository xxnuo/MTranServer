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
| MT_OFFLINE            | 离线模式，不自动下载新语言的模型，仅使用已下载的模型 | false  | true, false                 |
| MT_WORKER_IDLE_TIMEOUT| Worker 空闲超时时间（秒）                | 300    | 任意正整数                  |
| MT_API_TOKEN          | API 访问令牌                             | 空     | 任意字符串                  |
| MT_CACHE_SIZE         | 缓存大小（缓存最近的多少次翻译）              | 0      | 任意正整数                  |

示例：

```bash
# 设置日志级别为 debug
export MT_LOG_LEVEL=debug

# 设置端口为 9000
export MT_PORT=9000

# 启动服务
./mtranserver
```

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
| `/translate` | POST | 单文本翻译 | 是 |
| `/translate/batch` | POST | 批量翻译 | 是 |

#### 模型管理接口

模型下载采用异步任务。前端先调用下载接口获取任务 ID，再轮询任务状态；下载中的 `.part` 文件和活动任务会写入配置目录中的 `downloads.json`，服务重启后会自动从断点继续。

| 接口 | 方法 | 说明 | 认证 |
| ---- | ---- | ---- | ---- |
| `/models` | GET | 分页获取模型目录、版本、架构和安装状态 | 是 |
| `/models/ocr` | GET | 从下载站获取 OCR 模型清单、文件下载地址、大小、SHA-256 和可用状态 | 是 |
| `/models/ocr/download` | POST | 创建 OCR 模型下载任务，支持断点续传和 SHA-256 校验 | 是 |
| `/models/pair/{from}/{to}` | GET | 获取指定语言对的模型详情 | 是 |
| `/models/download` | POST | 创建模型下载任务 | 是 |
| `/models/downloads` | GET | 获取下载任务列表；传 `active=true` 时只返回进行中的任务 | 是 |
| `/models/downloads/{id}` | GET | 获取单个下载任务进度 | 是 |
| `/models/downloads/{id}` | DELETE | 取消下载任务并保留临时文件 | 是 |
| `/models/refresh` | POST | 从远程刷新 Mozilla 模型记录 | 是 |
| `/models/settings` | GET/POST | 获取或保存下载站、官方 CDN 和下载代理设置 | 是 |
| `/models/speedtest` | GET/POST | 测试下载站和官方 CDN 的下载速度 | 是 |
| `/models/latency` | GET/POST | 测试下载站和官方 CDN 的网络延迟 | 是 |
| `/models/{from}/{to}` | DELETE | 删除指定语言对的本地模型 | 是 |

`/models/ocr` 不在前端保存模型静态数据。Node 服务按当前下载设置访问下载站的 `GET /ocr/models`，并将下载站清单中的 `name`、`version`、`files`、`sizeBytes`、`sha256`、`available` 等字段返回给模型管理页面；配置了下载代理时，该请求也通过代理发出。

`GET /models` 支持 `page`、`pageSize`、`query`、`status`、`architecture` 和 `locale` 查询参数。`pageSize` 最大为 48，前端默认每页 24 条；接口只返回当前页模型，同时返回 `totalModels`、`filteredModels`、`totalPages`、`architectures` 和 `statusCounts` 等分页元数据。模型管理页轮询下载任务时使用 `/models/downloads?active=true`，避免重复传输已结束的历史任务。

**创建 OCR 模型下载任务：**

```json
{
  "modelId": "pp-ocrv5-mobile"
}
```

OCR 文件保存到 `<modelDir>/ocr/<modelId>/`。每个文件先写入同目录下的 `.part` 文件，下载完成后按清单中的 SHA-256 校验，通过后才改名为正式文件；服务重启或网络中断后会继续使用已有临时文件。

**创建下载任务：**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "architecture": "base-memory",
  "version": "3.0"
}
```

`architecture` 和 `version` 可以省略，服务会选择当前记录中的优先架构和最新版本。响应状态为 `202`：

```json
{
  "id": "下载任务 ID",
  "from": "en",
  "to": "zh-Hans",
  "architecture": "base-memory",
  "version": "3.0",
  "status": "downloading",
  "progress": 42,
  "downloadedBytes": 15000000,
  "totalBytes": 36126137,
  "currentFile": "model.enzh.intgemm.alphas.bin"
}
```

任务状态包括：`queued`、`checking`、`downloading`、`decompressing`、`completed`、`failed`。模型目录接口中的模型状态包括：`available`、`installed`、`downloading`、`decompressing`、`failed`。

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
