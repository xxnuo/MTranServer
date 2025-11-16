> 请暂勿更新最新版，暂时使用 2.1.1 手动下载模型的版本
> 最新版使用跨平台运行时在批量翻译时内存占用较高
> 本提示去除后说明修复完成

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

#### 环境变量配置

| 环境变量              | 说明                                     | 默认值 | 可选值                      |
| --------------------- | ---------------------------------------- | ------ | --------------------------- |
| MT_LOG_LEVEL          | 日志级别                                 | warn   | debug, info, warn, error    |
| MT_CONFIG_DIR         | 配置目录                                 | 自动   | 任意路径                    |
| MT_MODEL_DIR          | 模型目录                                 | 自动   | 任意路径                    |
| MT_HOST               | 服务器监听地址                           | 0.0.0.0| 任意 IP 地址                |
| MT_PORT               | 服务器端口                               | 8989   | 1-65535                     |
| MT_UI                 | 启用 Web UI                              | false  | true, false                 |
| MT_OFFLINE            | 离线模式（不自动下载模型）               | false  | true, false                 |
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

#### 翻译插件接口

> 注：
>
> - [沉浸式翻译](https://immersivetranslate.com/zh-Hans/docs/services/custom/) 在`设置`页面，开发者模式中启用`Beta`特性，即可在`翻译服务`中看到`自定义 API 设置`([官方图文教程](https://immersivetranslate.com/zh-Hans/docs/services/custom/))。然后将`自定义 API 设置`的`每秒最大请求数`拉高以充分发挥服务器性能准备体验飞一般的感觉。我设置的是`每秒最大请求数`为`5000`，`每次请求最大段落数`为`1`。你可以根据自己服务器配置设置。
>
> - [简约翻译](https://github.com/fishjar/kiss-translator) 在`设置`页面，接口设置中滚动到下面，即可看到自定义接口 `Custom`。同理，设置`最大请求并发数量`、`每次请求间隔时间`以充分发挥服务器性能。我设置的是`最大请求并发数量`为`100`，`每次请求间隔时间`为`1`。你可以根据自己服务器配置设置。
>
> 接下来按下表的设置方法设置插件的自定义接口地址。注意第一次请求会慢一些，因为需要加载模型。以后的请求会很快。

| 名称             | URL                                           | 插件设置                                                                         |
| ---------------- | --------------------------------------------- | -------------------------------------------------------------------------------- |
| 沉浸式翻译无密码 | `http://localhost:8989/imme`                  | `自定义API 设置` - `API URL`                                                     |
| 沉浸式翻译有密码 | `http://localhost:8989/imme?token=your_token` | 同上，需要更改 URL 尾部的 `your_token` 为你的 `API_TOKEN` 或 `CORE_API_TOKEN` 值 |
| 简约翻译无密码   | `http://localhost:8989/kiss`                  | `接口设置` - `Custom` - `URL`                                                    |
| 简约翻译有密码   | `http://localhost:8989/kiss`                  | 同上，需要 `KEY` 填 `your_token`                                                 |

**普通用户参照表格内容设置好插件使用的接口地址就可以使用了。**

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
