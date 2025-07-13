# MTranServer

[English](README_en.md) | 中文

<img src="./images/icon.png" width="64px" height="64px" align="right" alt="MTran">

一个超低资源消耗超快的离线翻译服务器，无需显卡。单个请求平均响应时间 50 毫秒。支持全世界主要语言的翻译。

翻译质量与 Google 翻译相当。

> 注意本模型专注于速度和多种设备私有部署，所以翻译质量肯定是不如大模型翻译的效果。需要高质量的翻译建议使用在线大模型 API。

## Demo

> 即将上线

<img src="./images/preview.png" width="auto" height="460">

## 同类项目效果(CPU,英译中)

| 项目名称                                                               | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息                                                                                                                          |
| ---------------------------------------------------------------------- | -------- | -------- | -------- | ---- | --------------------------------------------------------------------------------------------------------------------------------- |
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | 很高     | 差       | 一般     | 慢   | Android 移植版的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快                        |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)     | 很高     | 一般     | 一般     | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句。[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks)           | 高       | 一般     | 略差     | 快   | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                     |
| 其他大模型                                                             | 超高     | 动态     | 好好好     | 很慢 | 32B 及以上参数的模型效果不错，但是对硬件要求很高                                                                                  |
| MTranServer(本项目)                                                    | 低       | 高       | 一般     | 极快 | 单个请求平均响应时间 50ms                                                                                                         |

> 现有的 Transformer 架构的大模型的小参数量化版本不在考虑范围。
>
> 因为实际调研使用发现小参数模型的翻译质量很不稳定且会乱翻，幻觉严重，速度也达不到指哪翻哪秒回的效果。
> 出了性能更优的 Diffusion 架构的语言模型，再测试。
>
> 表中数据仅供参考，非严格测试，非量化版本对比。

## 更新日志

2025.07.16 v3.0.0 [即将发布]

- 完全重写
- 兼容性更好
- 性能更强

⚠️⚠️⚠️
注意，本次更新改动较大，正在进行中，下面的指南和镜像尚未更新建设完成 [2025.07.16]，请耐心等待...

## 桌面端

即将发布桌面端软件，敬请期待。

## 服务器部署

> 对普通用户来说有难度，建议使用桌面端。

### 1.1 环境要求

- Docker
- Docker Compose（可选）

### 1.2 Docker 部署

复制下面的命令，在终端执行。

```bash
docker run -d --name mtranserver -p 8989:8989 -e CORE_API_TOKEN=your_token xxnuo/mtranserver:latest
```

### 1.3 Docker Compose 部署

服务器准备一个存放配置的文件夹，打开终端执行以下命令

```bash
mkdir mtranserver
cd mtranserver
touch compose.yml
```

用编辑器打开 `compose.yml` 文件，写入以下内容。

> 1. 修改下面的 `your_token` 为你自己设置的一个密码，使用英文大小写和数字。自己内网可以不设置，如果是`云服务器`强烈建议设置一个密码，保护服务以免被`扫到、攻击、滥用`。
>
> 2. 如果需要更改端口，修改 `ports` 的值，比如修改为 `9999:8989` 表示将服务端口映射到本机 9999 端口。

```yaml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    environment:
      - CORE_API_TOKEN=your_token
```

先启动测试，确保 8989 端口没被占用。

```bash
docker compose up
```

正常输出示例：

```bash
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

## 准备模型

⚠️ 注意：第一次请求翻译 API 时会在后台自动下载模型，无需手动下载。

模型自动下载功能需要连接网络（中国大陆不需要代理），**后续翻译及其他功能均无需联网完全离线**。

**所以第一次翻译不是秒回，要等待一会儿！**

可在 Docker 日志处观察进度。下载速度取决于网络速度，一般在 10s 内能完成一个语言模型的下载。如果下载超时/失败，请检查容器是否能正常联网。

如果属于内网机器无法访问互联网可按照下文指导手动下载模型。

### 4. API 使用

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

| 名称                       | URL                                           | 插件设置                                                          |
| -------------------------- | --------------------------------------------- | ----------------------------------------------------------------- |
| 沉浸式翻译无密码           | `http://localhost:8989/imme`                  | `自定义API 设置` - `API URL`                                      |
| 沉浸式翻译有密码           | `http://localhost:8989/imme?token=your_token` | 同上，需要更改 URL 尾部的 `your_token` 为你的 `CORE_API_TOKEN` 值 |
| 简约翻译无密码             | `http://localhost:8989/kiss`                  | `接口设置` - `Custom` - `URL`                                     |
| 简约翻译有密码             | `http://localhost:8989/kiss`                  | 同上，需要 `KEY` 填 `your_token`                                  |
| 划词翻译自定义翻译源无密码 | `http://localhost:8989/hcfy`                  | `设置`-`其他`-`自定义翻译源`-`接口地址`                           |
| 划词翻译自定义翻译源有密码 | `http://localhost:8989/hcfy?token=your_token` | `设置`-`其他`-`自定义翻译源`-`接口地址`                           |

**普通用户参照表格内容设置好插件使用的接口地址就可以使用了。**

### 5. 保持更新

目前是测试版服务器和模型，可能会遇到问题，建议经常保持更新

从上文地址下载新模型，解压覆盖到原 `models` 模型文件夹

然后更新重启服务器：

```bash
docker compose down
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

> 国内用户若无法正常 `pull` 镜像，按照 `1.3 可选步骤` 手动下载新镜像导入即可。

### 开发者接口：

> Base URL: `http://localhost:8989`

| 名称               | URL                      | 请求格式                                                                               | 返回格式                                        | 认证头                    |
| ------------------ | ------------------------ | -------------------------------------------------------------------------------------- | ----------------------------------------------- | ------------------------- |
| 服务版本           | `/version`               | 无                                                                                     | `{"version": "v1.1.0"}`                         | 无                        |
| 语言对列表         | `/models`                | 无                                                                                     | `{"models":["zhen","enzh"]}`                    | Authorization: your_token |
| 普通翻译接口       | `/translate`             | `{"from": "en", "to": "zh", "text": "Hello, world!"}`                                  | `{"result": "你好，世界！"}`                    | Authorization: your_token |
| 批量翻译接口       | `/translate/batch`       | `{"from": "en", "to": "zh", "texts": ["Hello, world!", "Hello, world!"]}`              | `{"results": ["你好，世界！", "你好，世界！"]}` | Authorization: your_token |
| 健康检查           | `/health`                | 无                                                                                     | `{"status": "ok"}`                              | 无                        |
| 心跳检查           | `/__heartbeat__`         | 无                                                                                     | `Ready`                                         | 无                        |
| 负载均衡心跳检查   | `/__lbheartbeat__`       | 无                                                                                     | `Ready`                                         | 无                        |
| 谷歌翻译兼容接口 1 | `/language/translate/v2` | `{"q": "The Great Pyramid of Giza", "source": "en", "target": "zh", "format": "text"}` | `{"data": {"translations": [{"translatedText": "吉萨大金字塔"}]}}` | Authorization: your_token |

> 开发者高级设置请参考 [CONFIG.md](./CONFIG.md)

## 赞助

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[中国大陆 💗 赞赏](./DONATE.md)

## 贡献者

<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/Devillmy"><img src="https://avatars.githubusercontent.com/u/36851750?v=3?s=100" width="100px;" alt="Lv Meiyang"/><br /><sub><b>Lv Meiyang</b></sub></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/xxnuo"><img src="https://avatars.githubusercontent.com/u/54252779?v=3?s=100" width="100px;" alt="Leo"/><br /><sub><b>Leo</b></sub></td>
    </tr>
  </tbody>
</table>

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)
