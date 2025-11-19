# MTranServer

[English](README_en.md) | 中文

<!-- <img src="./images/icon.png" width="64px" height="64px" align="right" alt="MTran"> -->

一个超低资源消耗超快的离线翻译服务器，无需显卡。单个请求平均响应时间 50 毫秒。支持全世界主要语言的翻译。

翻译质量与 Google 翻译相当。

> 注意本模型专注于速度和多种设备私有部署，所以翻译质量肯定是不如大模型翻译的效果。需要高质量的翻译建议使用在线大模型 API。

<img src="./images/preview.png" width="auto" height="460">

## 同类项目效果(CPU,英译中)

| 项目名称                                                               | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息                                                                                                                          |
| ---------------------------------------------------------------------- | -------- | -------- | -------- | ---- | --------------------------------------------------------------------------------------------------------------------------------- |
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | 很高     | 差       | 一般     | 慢   | Android 移植版的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快                        |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)     | 很高     | 一般     | 一般     | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句。[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks)           | 高       | 一般     | 略差     | 快   | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                     |
| 其他大模型                                                             | 超高     | 动态     | 非常好   | 很慢 | 32B 及以上参数的模型效果不错，但是对硬件要求很高                                                                                  |
| MTranServer(本项目)                                                    | 低       | 高       | 一般     | 极快 | 单个请求平均响应时间 50ms |

> 表中数据仅供参考，非严格测试，非量化版本对比。

## 使用说明

### 命令行参数

```bash
./mtranserver [选项]

选项：
  -version, -v          显示版本信息
  -log-level string     日志级别 (debug, info, warn, error) (默认 "warn")
  -config-dir string    配置目录 (默认 "~/.config/mtran/server")
  -model-dir string     模型目录 (默认 "~/.config/mtran/models")
  -host string          服务器监听地址 (默认 "0.0.0.0")
  -port string          服务器端口 (默认 "8989")
  -ui                   启用 Web UI (默认 true)
  -offline              启用离线模式，不自动下载新模型 (默认 false)
  -worker-idle-timeout int  Worker 空闲超时时间（秒） (默认 300)

示例：
  ./mtranserver --host 127.0.0.1 --port 8080
  ./mtranserver --ui --offline
  ./mtranserver -v
```

### Docker Compose 部署

```yml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    environment:
      - MT_HOST=0.0.0.0
      - MT_PORT=8989
      - MT_ENABLE_UI=true
      - MT_OFFLINE=false
      # - API_TOKEN=your_secret_token_here
    volumes:
      - ./models:/app/models
```

```bash
docker compose up -d
```

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
| API_TOKEN             | API 访问令牌                             | 空     | 任意字符串                  |
| CORE_API_TOKEN        | API 访问令牌（备选）                     | 空     | 任意字符串                  |

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

#### 翻译插件兼容接口

服务器提供了多个翻译插件的兼容接口：

| 接口 | 方法 | 说明 | 支持的插件 |
| ---- | ---- | ---- | ---------- |
| `/imme` | POST | 沉浸式翻译插件接口 | [沉浸式翻译](https://immersivetranslate.com/) |
| `/kiss` | POST | 简约翻译插件接口 | [简约翻译](https://github.com/fishjar/kiss-translator) |
| `/deepl` | POST | DeepL API v2 兼容接口 | 支持 DeepL API 的客户端 |
| `/google/language/translate/v2` | POST | Google Translate API v2 兼容接口 | 支持 Google Translate API 的客户端 |
| `/google/translate_a/single` | GET | Google translate_a/single 兼容接口 | 支持 Google 网页翻译的客户端 |
| `/hcfy` | POST | 划词翻译兼容接口 | [划词翻译](https://github.com/Selection-Translator/crx-selection-translate) |

**插件配置说明：**

> 注：
>
> - [沉浸式翻译](https://immersivetranslate.com/zh-Hans/docs/services/custom/) 在`设置`页面，开发者模式中启用`Beta`特性，即可在`翻译服务`中看到`自定义 API 设置`([官方图文教程](https://immersivetranslate.com/zh-Hans/docs/services/custom/))。然后将`自定义 API 设置`的`每秒最大请求数`拉高以充分发挥服务器性能准备体验飞一般的感觉。我设置的是`每秒最大请求数`为`5000`，`每次请求最大段落数`为`1`。你可以根据自己服务器配置设置。
>
> - [简约翻译](https://github.com/fishjar/kiss-translator) 在`设置`页面，接口设置中滚动到下面，即可看到自定义接口 `Custom`。同理，设置`最大请求并发数量`、`每次请求间隔时间`以充分发挥服务器性能。我设置的是`最大请求并发数量`为`100`，`每次请求间隔时间`为`1`。你可以根据自己服务器配置设置。
>
> **重要提示：** 首次翻译某个语言对时，服务器会自动下载对应的翻译模型（除非启用了离线模式），这个过程可能需要等待一段时间（取决于网络速度和模型大小）。模型下载完成后，引擎启动也需要几秒钟时间。之后的翻译请求将享受毫秒级的响应速度。建议在正式使用前先测试一次翻译，让服务器预先下载和加载模型。
>
> 接下来按下表的设置方法设置插件的自定义接口地址。

| 名称             | URL                                           | 插件设置                                                                         |
| ---------------- | --------------------------------------------- | -------------------------------------------------------------------------------- |
| 沉浸式翻译无密码 | `http://localhost:8989/imme`                  | `自定义API 设置` - `API URL`                                                     |
| 沉浸式翻译有密码 | `http://localhost:8989/imme?token=your_token` | 同上，需要更改 URL 尾部的 `your_token` 为你的 `API_TOKEN` 或 `CORE_API_TOKEN` 值 |
| 简约翻译无密码   | `http://localhost:8989/kiss`                  | `接口设置` - `Custom` - `URL`                                                    |
| 简约翻译有密码   | `http://localhost:8989/kiss`                  | 同上，需要 `KEY` 填 `your_token`                                                 |
| DeepL 兼容       | `http://localhost:8989/deepl`                 | 使用 `DeepL-Auth-Key` 或 `Bearer` 认证                                           |
| Google 兼容      | `http://localhost:8989/google/language/translate/v2` | 使用 `key` 参数或 `Bearer` 认证                                           |
| 划词翻译         | `http://localhost:8989/hcfy`                  | 支持 `token` 参数或 `Bearer` 认证                                                |

**普通用户参照表格内容设置好插件使用的接口地址就可以使用了。**

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
