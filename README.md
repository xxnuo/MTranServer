# MTranServer

[中文](README.md) | [English](docs/README_en.md) | [日本語](docs/README_ja.md) | [Français](docs/README_fr.md) | [Deutsch](docs/README_de.md)

<!-- <img src="./images/icon.png" width="64px" height="64px" align="right" alt="MTran"> -->

一个在自己电脑上运行的免费离线翻译服务，无需显卡。支持多种语言，可搭配浏览器和编辑器插件使用。翻译速度和内存占用取决于电脑配置、语言和文本长度。

适合日常阅读和理解大意。翻译结果可能有误，重要内容请核对原文。

> v5 采用新版翻译引擎，性能大幅提升。效果更好的新版中英翻译模型正在训练中。升级后首次使用某种翻译方向时，可能需要重新准备翻译文件，请耐心等待。

<img src="./images/preview.png" width="auto" height="460">

## 在线试用 Demo

| 网站                                                                         | TOKEN                     | 其他接口                                                                       | 提供者                               |
| ---------------------------------------------------------------------------- | ------------------------- | ------------------------------------------------------------------------------ | ------------------------------------ |
| [ipacel.cc](https://MTranServer.ipacel.cc/ui/?token=__IpacEL_MT_API_TOKEN__) | `__IpacEL_MT_API_TOKEN__` | 沉浸式翻译: `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | [@ApliNi](https://github.com/ApliNi) |

感谢社区贡献者为用户提供试用服务！

## 使用说明

现在支持桌面端一键启动！支持 Windows、Mac、Linux。

### 桌面端

#### 手动下载

前往 [Releases](https://github.com/xxnuo/MTranServer/releases) 下载对应平台最新桌面端，直接安装启动，即可使用。

桌面端启动后会创建一个托盘菜单，通过菜单可以方便的管理服务。

程序自带的一个简单 UI 的地址和在线调试文档。

具体使用说明可以直接跳转到 [生态项目](#生态项目)

预览（最新版有更新）：

![UI](./images/ui.png)

![文档](./images/swagger.png)

### 服务端

普通用户建议使用桌面端，安装后即可启动。服务器用户可以使用 Docker 或通过命令行安装。v5 暂不提供服务端单文件可执行程序。

#### 快速开始

程序员朋友可以通过命令行直接启动服务器端：

```bash
npx mtranserver@latest
```

> `npx` 可以替换为你喜欢的任意一个包管理器，比如 `bunx`、`pnpx` 等。

> 首次翻译某种语言到另一种语言时，程序会自动下载并准备所需文件，需要联网并等待完成。之后会重复使用已准备好的文件。建议先翻译一小段文字，确认能正常使用。首次使用和升级后准备文件时，耗时与平时翻译不同。

#### 快速安装

```bash
npm i -g mtranserver@latest
```

> `npm` 可以替换为你喜欢的任意一个包管理器，比如 `bun`、`pnpm` 等。

然后启动 `mtranserver` 即可。

#### Docker Compose 部署

找一个空目录，编写 `compose.yml` 文件，内容如下：

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
      - MT_MODEL_DIR=/app/models
      - MT_CONFIG_DIR=/root/.config/mtran/server
      - MT_OFFLINE=false
      # - MT_API_TOKEN=your_secret_token_here
    volumes:
      - ./models:/app/models
      - ./config:/root/.config/mtran
```

```bash
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

### 升级与离线使用

1. 升级前保留原来的配置和翻译模型文件夹。
2. 升级后保持联网，先试译一次需要使用的每个翻译方向，等待文件准备完成。
3. 准备好后可在设置中开启离线模式，再断开网络。离线时只能使用已经准备好的翻译方向。
4. 更换电脑或迁移 Docker 时，同时保存配置和翻译模型两个文件夹。只复制模型文件夹不够。

上面的 Docker 示例会把这两个文件夹保存为当前目录下的 `config` 和 `models`。离线部署前先联网完成准备，再将 `MT_OFFLINE` 改为 `true` 并重新启动。

## 生态项目

### IDE 插件

#### [MTranCode](https://github.com/xxnuo/MTranCode) 代码注释翻译插件

支持 VS Code、Cursor、Augment 等 VS Code 系列 IDE

在插件商店搜索 **`MTranCode`** 即可安装注释翻译插件

插件默认接口会调用 `http://localhost:8989` 接口的服务器进行注释、代码的翻译，可在设置中调整。

该插件由 [vscode-comment-translate](https://github.com/intellism/vscode-comment-translate) fork 而来。

### 浏览器插件

#### [MTranBrowser](https://github.com/xxnuo/MTranBrowser)

TODO: 火热开发中

> 如果你开发了衍生项目，欢迎提交 PR，我会在生态项目中添加你的项目。

> 对了项目已经发布到 npm 包，可以直接在其他程序中调用简单的库接口实现翻译功能，具体信息查看 ts 类型说明。

## 兼容接口

服务器提供了多个翻译插件的兼容接口：

| 接口                            | 方法 | 说明                               | 支持的插件                                                                  |
| ------------------------------- | ---- | ---------------------------------- | --------------------------------------------------------------------------- |
| `/imme`                         | POST | 沉浸式翻译插件接口                 | [沉浸式翻译](https://immersivetranslate.com/)                               |
| `/kiss`                         | POST | 简约翻译插件接口                   | [简约翻译](https://github.com/fishjar/kiss-translator)                      |
| `/deepl`                        | POST | DeepL API v2 兼容接口              | 支持 DeepL API 的客户端                                                     |
| `/deeplx`                       | POST | DeepLX 兼容接口                    | 支持 DeepLX 接口的客户端                                                    |
| `/hcfy`                         | POST | 划词翻译兼容接口                   | [划词翻译](https://github.com/Selection-Translator/crx-selection-translate) |
| `/google/language/translate/v2` | POST | Google Translate API v2 兼容接口   | 支持 Google Translate API 的客户端                                          |
| `/google/translate_a/single`    | GET  | Google translate_a/single 兼容接口 | 支持 Google 网页翻译的客户端                                                |

**插件配置说明：**

> 将下面表格中的地址填入插件设置。请求速度、同时翻译数量等选项建议先保持默认；如果翻译超时或电脑负担过大，可以降低这些设置。

| 名称             | URL                                                  | 插件设置                                                        |
| ---------------- | ---------------------------------------------------- | --------------------------------------------------------------- |
| 沉浸式翻译无密码 | `http://localhost:8989/imme`                         | `自定义API 设置` - `API URL`                                    |
| 沉浸式翻译有密码 | `http://localhost:8989/imme?token=your_token`        | 同上，需要更改 URL 尾部的 `your_token` 为你的 `MT_API_TOKEN` 值 |
| 简约翻译无密码   | `http://localhost:8989/kiss`                         | `接口设置` - `Custom` - `URL`                                   |
| 简约翻译有密码   | `http://localhost:8989/kiss`                         | 同上，需要 `KEY` 填 `your_token`                                |
| DeepL 兼容       | `http://localhost:8989/deepl`                        | 使用 `DeepL-Auth-Key` 或 `Bearer` 认证                          |
| DeepLX 兼容      | `http://localhost:8989/deeplx`                       | 支持 `token` 参数或 `Bearer` 认证                               |
| Google 兼容      | `http://localhost:8989/google/language/translate/v2` | 使用 `key` 参数或 `Bearer` 认证                                 |
| 划词翻译         | `http://localhost:8989/hcfy`                         | 支持 `token` 参数或 `Bearer` 认证                               |

**普通用户参照表格内容设置好插件使用的接口地址就可以使用了。**

### 命令行参数

```bash
mtranserver --help

  --help                              查看帮助
  --host <ip>                         监听地址，默认 0.0.0.0
  --port <port>                       端口，默认 8989
  --ui / --no-ui                      显示或关闭网页界面
  --offline                           使用已经准备好的翻译文件
  --worker-idle-timeout <seconds>      空闲后释放翻译引擎，默认 60 秒
  --cache-size <count>                保存最近的翻译结果，默认 1000 条；0 为关闭
  --download en_zh zh_en              下载并准备指定翻译方向
  --languages [filter]                列出可下载的翻译方向

  mtranserver --host 127.0.0.1 --port 8080
  mtranserver --download en_zh zh_en
  mtranserver --offline
```

## 性能测试（v5 ONNX）

MTranServer **5.1.0**，默认量化 ONNX 模型，关闭翻译缓存（`MT_CACHE_SIZE=0`）。吞吐取三轮中位数；结果仅供参考，实际表现随硬件、文本和负载变化。

| 翻译方向 | 输入（字符数） | 后端 / 推理线程 | 并发请求 | 平均耗时（ms） | P95（ms） | 吞吐（次/秒） |
| --- | --- | --- | --- | --- | --- | --- |
| 英 → 中 | 短句 49–94 | CPU / 6 | 1 | 11.5 | 16.6 | 86.51 |
| 英 → 中 | 短句 49–94 | CPU / 6 | 4 | 35.6 | 50.8 | 108.33 |
| 英 → 中 | 段落 234–344 | CPU / 6 | 1 | 27.7 | 35.0 | 36.31 |
| 中 → 英 | 短句 14–28 | CPU / 7 | 1 | 20.2 | 28.3 | 48.61 |
| 中 → 英 | 短句 14–28 | CPU / 7 | 4 | 68.2 | 89.1 | 56.19 |
| 中 → 英 | 段落 65–104 | CPU / 7 | 1 | 62.5 | 85.7 | 15.52 |
| 德 → 英 | 短句 50–113 | CPU / 5 | 1 | 12.6 | 17.7 | 83.02 |
| 德 → 英 | 短句 50–113 | CPU / 5 | 4 | 39.7 | 51.5 | 96.51 |
| 德 → 英 | 段落 232–395 | CPU / 5 | 1 | 33.4 | 43.4 | 30.00 |

| 翻译方向 | 首次加载、选后端并翻译（秒） | 预热后 RSS（MiB） | 峰值 RSS（MiB） | ONNX 模型（MiB） |
| --- | --- | --- | --- | --- |
| 英 → 中 | 33.47 | 364.3 | 391.7 | 118.8 |
| 中 → 英 | 62.99 | 410.0 | 475.7 | 125.5 |
| 德 → 英 | 26.44 | 319.3 | 345.1 | 71.9 |

### 与已发布 5.0.1 的同轮对照

| 翻译方向 | 输入 | 并发 | 5.0.1（次/秒） | 5.1.0（次/秒） | 速度比 |
| --- | --- | --- | --- | --- | --- |
| 英 → 中 | 短句 | 1 | 46.92 | 83.52 | 1.78× |
| 英 → 中 | 短句 | 4 | 57.25 | 93.65 | 1.64× |
| 英 → 中 | 段落 | 1 | 17.27 | 32.15 | 1.86× |
| 中 → 英 | 短句 | 1 | 28.76 | 48.42 | 1.68× |
| 中 → 英 | 短句 | 4 | 28.86 | 51.69 | 1.79× |
| 中 → 英 | 段落 | 1 | 7.40 | 15.58 | 2.10× |
| 德 → 英 | 短句 | 1 | 48.45 | 75.11 | 1.55× |
| 德 → 英 | 短句 | 4 | 46.20 | 93.65 | 2.03× |
| 德 → 英 | 段落 | 1 | 14.78 | 30.57 | 2.07× |

## 同类项目

下表保留旧版对比记录，并补充当前 v5 的英译中实测结果，测试环境不同，仅供参考：

| 项目名称                                                           | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息                                                                                                                          |
| ------------------------------------------------------------------ | -------- | -------- | -------- | ---- | --------------------------------------------------------------------------------------------------------------------------------- |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)      | 很高     | 差       | 一般     | 慢   | 大佬移植到了 Android 的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快                 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 很高     | 一般     | 一般     | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句，[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)       | 高       | 一般     | 略差     | 快   | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                     |
| 其他大模型                                                         | 超高     | 动态     | 非常好   | 很慢 | 对硬件要求很高，如果需要高并发翻译建议使用 vllm 框架                                                                              |
| 本项目（旧版 Bergamot）                                                             | 低       | 高       | 一般     | 极快 | 单个请求平均响应时间 50ms                                                                                                         |
| 本项目（v5 ONNX，自动后端） | 392MiB（英译中探测峰值） | 按实测限制并发 | 未评测 | 86.51 次/秒 | 英译中短句，自动选择 CPU 6 线程；测试条件见上文 |

> 旧版行沿用原 README 的 CPU、英译中简单测试记录（非严格测试、非量化版本对比）；v5 行使用上方实测数据及默认量化 ONNX 模型。

> 50ms 为旧版 Bergamot 引擎的历史记录，不代表 v5 ONNX Runtime 的实测结果。原 README 未记录具体硬件、文本长度、样本量及缓存状态；表中定性评价也沿用旧版，不是当前各项目在统一环境下的性能排名。

[原始记录](https://github.com/xxnuo/MTranServer/blob/1f05f8536940f825be205e3434fd7fe964c56a26/README.md)

# 高级配置说明

请参考 [API.md](API.md) 文件和启动后的 API 文档。

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
