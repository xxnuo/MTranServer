# MTranServer 
> 迷你翻译服务器

<img src="./images/logo.jpg" width="auto" height="128" align="right">

[English](README_en.md) | [日本語](README_ja.md) | 中文

一个超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。单个请求平均响应时间 50ms

翻译质量与 Google 翻译相当。

注意本模型专注于性能优化，所以翻译质量肯定是不如大模型翻译的效果。

<img src="./images/preview.png" width="auto" height="328">

## 同类项目效果(CPU,英译中)

| 项目名称 | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息 |
|----------|----------|----------|----------|------|----------|
| [facebook/nllb-200-distilled-600M](https://github.com/thammegowda/nllb-serve) | 很高 | 差 | 一般 | 慢 | Android 的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 很高 | 一般 | 一般 | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句。[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | 高 | 一般 | 略差 | 快 | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| MTranServer(本项目) | 低 | 高 | 一般 | 极快 | 单个请求平均响应时间 50ms |

非严格测试，非量化版本对比，仅供参考。

## Docker Compose 服务器部署

> 还没编写完成，请耐心等待

目前仅支持 amd64 架构 CPU 的 Docker 部署。ARM、RISCV 架构在适配中 😳

### 准备

准备一个存放配置的文件夹，打开终端执行以下命令

```bash
mkdir mtranserver
cd mtranserver
touch config.ini
touch compose.yml
mkdir models
```

### 编写配置

用编辑器打开 `compose.yml` 文件，写入以下内容。

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

> 注：若你的机器在国内无法正常联网下载镜像，可以按如下操作导入镜像
> 
> 打开 <a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">国内下载地址(内含 Docker 镜像下载)</a>
> 
> 进入`下载 Docker 镜像文件夹`，选择最新版的镜像 `mtranserver.image.tar` 下载保存到运行 Docker 的机器上。
> 
> 进入下载到的目录打开终端，执行如下命令导入镜像
> ```bash
> docker load -i mtranserver.image.tar
> ```
>
> 然后正常继续下一步下载模型

### 下载模型

<a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">国内下载地址(内含 Docker 镜像下载)</a> 模型在`下载模型文件夹内`

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

### 启动服务

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

### API 文档

> `your_token` 是你设置在 `config.ini` 文件中的 `CORE_API_TOKEN` 值。若未设置，则不需要进行密码认证。
>
> `localhost` 可以替换为你的服务器地址或 docker 容器名。

| 名称 | API URL | 备注 | 认证头 |
| --- | --- | --- | --- |
| 服务版本 | http://localhost:8989/version | 获取服务版本| 无 |
| 模型列表 | http://localhost:8989/models | 获取模型列表| Authorization: your_token |
| 沉浸式翻译无密码  | http://localhost:8989/imme | 自定义API 设置 - API URL| 无 |
| 沉浸式翻译有密码 | http://localhost:8989/imme?token=your_token | 自定义API 设置 - API URL| 无需你设置 |
| 简约翻译 | http://localhost:8989/kiss | 接口设置 - Custom - URL| 无 |
| 简约翻译有密码 | http://localhost:8989/kiss | KEY 填 your_token | 无需你设置 |

> 注：
> 
> - [沉浸式翻译](https://immersivetranslate.com/zh-Hans/docs/services/custom/) 在`设置`页面，开发者模式中启用`Beta`特性，即可在`翻译服务`中看到`自定义 API 设置`。官方[图文教程](https://immersivetranslate.com/zh-Hans/docs/services/custom/)
> 
> - [简约翻译](https://github.com/fishjar/kiss-translator) 在`设置`页面，接口设置中滚动到下面，即可看到自定义接口 `Custom`

### 如何使用

目前可以在浏览器中使用沉浸式翻译插件、简约翻译(kiss translator)插件调用。

## 客户端版本

服务端翻译核心、Windows 和 Mac 客户端版本在适配中 [MTranServerCore](https://github.com/xxnuo/MTranServerCore) (暂未公开)

## 赞助我

[☕️ 爱发电](https://afdian.com/a/xxnuo)

---

微信: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

欢迎加我交流技术和开源相关项目～

找工作中。可以联系我查看我的简历。

---

