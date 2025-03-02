# MTranServer 
> 迷你翻译服务器 测试版 ⭐️ 给我个 Star 吧

<img src="./images/icon.png" width="auto" height="128" align="right">

[English](README_en.md) | [日本語](README_ja.md) | 中文

一个超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。单个请求平均响应时间 50ms。支持全世界主要语言的翻译。

翻译质量与 Google 翻译相当。

注意本模型专注于速度和多种设备私有部署，所以翻译质量肯定是不如大模型翻译的效果。

需要高质量的翻译建议使用在线大模型 API。

<img src="./images/preview.png" width="auto" height="328">

## 同类项目效果(CPU,英译中)

| 项目名称 | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息 |
|----------|----------|----------|----------|------|----------|
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | 很高 | 差 | 一般 | 慢 | Android 移植版的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 很高 | 一般 | 一般 | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句。[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | 高 | 一般 | 略差 | 快 | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| 其他大模型 | 超高 | 动态 | 好好好 | 很慢 | 32B 及以上参数的模型效果不错，但是对硬件要求很高|
| MTranServer(本项目) | 低 | 高 | 一般 | 极快 | 单个请求平均响应时间 50ms |

> 现有的 Transformer 架构的大模型的小参数量化版本不在考虑范围，因为实际调研使用发现翻译质量很不稳定且会乱翻，幻觉严重，速度也不快。等以后出了 Diffusion 架构的语言模型，再测试。
>
> 表中数据仅供参考，非严格测试，非量化版本对比。

## Docker Compose 服务器部署

目前仅支持 amd64 架构 CPU 的 Docker 部署。ARM、RISCV 架构在适配中 😳

或者在个人电脑上安装 `Docker Desktop` 后，按下面的指南使用 `Docker Compose` 部署尝鲜。

### 1. 准备

准备一个存放配置的文件夹，打开终端执行以下命令

```bash
mkdir mtranserver
cd mtranserver
touch config.ini
touch compose.yml
mkdir models
```

### 编写配置

#### 1.1 用编辑器打开 `config.ini` 文件，写入以下内容
```ini
CORE_API_TOKEN=your_token
```
注意，修改这里的 `your_token` 为你自己设置的一个密码，使用英文大小写和数字。

自己内网可以不设置，如果是`云服务器`强烈建议设置一个密码，保护服务以免被`扫到、攻击、滥用`。

#### 1.2 用编辑器打开 `compose.yml` 文件，写入以下内容

> 注：如果需要更改端口，请修改 `ports` 的值，比如修改为 `8990:8989` 表示将服务端口映射到本机 8990 端口。

```yaml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    volumes:
      - ./models:/app/models
      - ./config.ini:/app/config.ini
```

#### 1.3 可选步骤

若你的机器在中国大陆无法正常联网下载镜像，可以按如下操作导入镜像

打开 <a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">中国大陆下载地址(内含 Docker 镜像下载)</a>

进入`下载 Docker 镜像文件夹`，选择最新版的镜像 `mtranserver.image.tar` 下载保存到运行 Docker 的机器上。

进入下载到的目录打开终端，执行如下命令导入镜像
```bash
docker load -i mtranserver.image.tar
```
然后正常继续下一步下载模型

### 2. 下载模型

> 持续更新模型中

<a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">中国大陆下载地址(内含 Docker 镜像下载)</a> 模型在`下载模型文件夹内`

<a href="https://github.com/xxnuo/MTranServer/releases/tag/models" target="_blank">国际下载地址</a>

按需要下载模型后`解压`每个语言的压缩包到 `models` 文件夹内。

下载了英译中模型的当前文件夹结构示意图：
```
compose.yml
config.ini
models/
├── enzh
│   ├── lex.50.50.enzh.s2t.bin
│   ├── model.enzh.intgemm.alphas.bin
│   └── vocab.enzh.spm
```
如果你下载添加多个模型，这是有中译英、英译中模型文件夹结构示意图：
```
compose.yml
config.ini
models/
├── enzh
│   ├── lex.50.50.enzh.s2t.bin
│   ├── model.enzh.intgemm.alphas.bin
│   └── vocab.enzh.spm
├── zhen
│   ├── lex.50.50.zhen.t2s.bin
│   ├── model.zhen.intgemm.alphas.bin
│   └── vocab.zhen.spm
```

用不到的模型没必要下载。按自己的需求下载模型。

注意：例如中译日的过程是先中译英，再英译日，也就是需要两个模型 `zhen` 和 `enja`。其他语言翻译过程类似。

### 3. 启动服务

先启动测试，确保模型位置没放错、能正常启动加载模型、端口没被占用。

```bash
docker compose up
```

正常输出示例：
```
[+] Running 2/2
 ✔ Network sample_default  Created  0.1s 
 ✔ Container mtranserver   Created  0.1s 
Attaching to mtranserver
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Using maximum available worker count: 16
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Starting Translation Service
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Service port: 8989
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Worker threads: 16
mtranserver  | Successfully loaded model for language pair: enzh
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Models loaded.
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Using default max parallel translations: 32
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Max parallel translations: 32
```

然后按 `Ctrl+C` 停止服务运行，然后正式启动服务器

```bash
docker compose up -d
```

这时候服务器就在后台运行了。

### 4. API 文档

下面表格内的 `localhost` 可以替换为你的服务器地址或 Docker 容器名。

下面表格内的 `8989` 端口可以替换为你在 `compose.yml` 文件中设置的端口值。

如果未设置 `CORE_API_TOKEN` 或者设置为空，翻译插件使用`无密码`的 API。

如果设置了 `CORE_API_TOKEN`，翻译插件使用`有密码`的 API。

下面表格中的 `your_token` 替换为你在 `config.ini` 文件中设置的 `CORE_API_TOKEN` 值。

#### 翻译插件接口：

> 注：
> 
> - [沉浸式翻译](https://immersivetranslate.com/zh-Hans/docs/services/custom/) 在`设置`页面，开发者模式中启用`Beta`特性，即可在`翻译服务`中看到`自定义 API 设置`([官方图文教程](https://immersivetranslate.com/zh-Hans/docs/services/custom/))。然后将`自定义 API 设置`的`每秒最大请求数`拉高以充分发挥服务器性能准备体验飞一般的感觉。我设置的是`每秒最大请求数`为`5000`，`每次请求最大段落数`为`10`。你可以根据自己服务器配置设置。
> 
> - [简约翻译](https://github.com/fishjar/kiss-translator) 在`设置`页面，接口设置中滚动到下面，即可看到自定义接口 `Custom`。同理，设置`最大请求并发数量`、`每次请求间隔时间`以充分发挥服务器性能。我设置的是`最大请求并发数量`为`100`，`每次请求间隔时间`为`1`。你可以根据自己服务器配置设置。
>
> 接下来按下表的设置方法设置插件的自定义接口地址。注意第一次请求会慢一些，因为需要加载模型。以后的请求会很快。

| 名称 | URL | 插件设置 |
| --- | --- | --- |
| 沉浸式翻译无密码  | `http://localhost:8989/imme` | `自定义API 设置` - `API URL`|
| 沉浸式翻译有密码 | `http://localhost:8989/imme?token=your_token` | 同上，需要更改 URL 尾部的 `your_token` 为你的 `CORE_API_TOKEN` 值 |
| 简约翻译无密码 | `http://localhost:8989/kiss` | `接口设置` - `Custom` - `URL`|
| 简约翻译有密码 | `http://localhost:8989/kiss` | 同上，需要 `KEY` 填 `your_token` |

**普通用户参照表格内容设置好插件使用的接口地址就可以使用了。接下来请跳到下面的`如何更新`。**

#### 开发者接口：

> Base URL: `http://localhost:8989`

| 名称 | URL | 请求格式 | 返回格式 | 认证头 |
| --- | --- | --- | --- | --- |
| 服务版本 | `/version` | 无 | 无 | 无 |
| 语言对列表 | `/models` | 无 | 无 | Authorization: your_token |
| 普通翻译接口 | `/translate` | `{"from": "en", "to": "zh", "text": "Hello, world!"}`| `{"result": "你好，世界！"}` | Authorization: your_token |
| 批量翻译接口 | `/translate/batch` | `{"from": "en", "to": "zh", "texts": ["Hello, world!", "Hello, world!"]}`| `{"results": ["你好，世界！", "你好，世界！"]}` | Authorization: your_token |
| 健康检查 | `/health` | 无 | `{"status": "ok"}` | 无 |
| 心跳检查 | `/__heartbeat__` | 无 | `Ready` | 无 |
| 负载均衡心跳检查 | `/__lbheartbeat__` | 无 | `Ready` | 无 |

### 5. 如何更新

目前是测试版服务器和模型，可能会遇到问题，建议经常保持更新

下载新模型，解压覆盖到原 `models` 模型文件夹，然后更新重启服务器：
```bash
docker compose down
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

## 源码仓库

Windows、Mac 和 Linux 独立客户端软件: [MTranServerClient](https://github.com/xxnuo/MTranServerClient) (未公开，请耐心等待正式版公开)

服务端 API 服务源码仓库: [MTranServerCore](https://github.com/xxnuo/MTranServerCore) (未公开，请耐心等待正式版公开)

## 感谢

推理框架: C++ [Marian-NMT](https://marian-nmt.github.io) 框架

翻译模型: [firefox-translations-models](https://github.com/mozilla/firefox-translations-models)

> Join us: [https://www.mozilla.org/zh-CN/contribute/](https://www.mozilla.org/zh-CN/contribute/)

## 赞助我

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[中国大陆 💗 赞赏](./DONATE.md)

## 联系我

微信: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

欢迎加我交流技术和开源相关项目～

找工作中。可以联系我查看我的简历。

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://star-history.com/#xxnuo/MTranServer&Timeline)