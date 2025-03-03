# MTranServer 
> Mini Translation Server Beta Version

<img src="./images/logo.jpg" width="auto" height="128" align="right">

[中文](README.md) | [日本語](README_ja.md) | English

A high-performance offline translation server with minimal resource requirements - runs on CPU with just 1GB memory, no GPU needed. Average response time of 50ms per request. Supports translation of major languages worldwide.

Translation quality comparable to Google Translate.

Note: This model focuses on performance optimization and private deployment on various devices, so the translation quality will not match that of large language models.

For high-quality translation, consider using online large language model APIs.
<img src="./images/preview.png" width="auto" height="328">

## Comparison with Similar Projects (CPU, English to Chinese)

| Project Name | Memory Usage | Concurrency | Translation Quality | Speed | Additional Info |
|--------------|--------------|-------------|---------------------|-------|-----------------|
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | Very High | Poor | Average | Slow | Android's [RTranslator](https://github.com/niedev/RTranslator) has optimizations but still has high resource usage and slower speed |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | Very High | Average | Average | Medium | Mid-range CPU: 3 sentences/s, high-end CPU: 15-20 sentences/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | High | Average | Below Average | Fast | [Performance Benchmarks](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| Any LLM | Extremely High | Dynamic | Good | Dynamic Very Slow | 32B or more parameter models perform well, but require high hardware requirements |
| MTranServer (This Project) | Low | High | Average | Ultra Fast | 50ms average response time per request |

*Note: Non-rigorous testing, non-quantized version comparison, for reference only.

## Docker Compose Server Deployment

Currently only supports Docker deployment on amd64 architecture CPUs.

Support for ARM and RISC-V architectures is under development 😳

You can also try it out by installing `Docker Desktop` on your computer and following the guide below to deploy with `Docker Compose`.

### 1. Preparation

Create a folder for configuration files and run the following commands in terminal:

```bash
mkdir mtranserver
cd mtranserver
touch config.ini
touch compose.yml
mkdir models
```

### Configuration

#### 1.1 Open `config.ini` with an editor and write:
```ini
CORE_API_TOKEN=your_token
```
Note: Change `your_token` to your own password using English letters and numbers.

For internal network use, setting a password is optional. However, for cloud servers, it's strongly recommended to set a password to protect against scanning, attacks, and abuse.

#### 1.2 Open `compose.yml` with an editor and write:

> Note: To change the port, modify the `ports` value. For example, change to `8990:8989` to map the service port to local port 8990.

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

#### 1.3 Optional Step

If you cannot download the image normally in mainland China, you can import the image as follows:

Open <a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">Mainland China Download Link (includes Docker image)</a>

Enter the `Docker Image Download` folder, download the latest image `mtranserver.image.tar` to your Docker machine.

Open terminal in the download directory and run the following command to import the image:
```bash
docker load -i mtranserver.image.tar
```
Then proceed normally to the next step to download models.

### 2. Download Models

<a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">Mainland China Download Link (includes Docker image)</a> Models are in the `Download Models` folder

<a href="https://github.com/xxnuo/MTranServer/releases/tag/models" target="_blank">International Download Link</a>

Extract each language's compressed package into the `models` folder.

Example folder structure with English-Chinese model:
```
compose.yml
config.ini
models/
├── enzh
│   ├── lex.50.50.enzh.s2t.bin
│   ├── model.enzh.intgemm.alphas.bin
│   └── vocab.enzh.spm
```

Example with Chinese-English and English-Chinese models:
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

Only download the models you need.

Note: For example, Chinese to Japanese translation first translates Chinese to English, then English to Japanese, requiring both `zhen` and `enja` models. Other language translations work similarly.

### 3. Start Service

First, test the service to ensure models are placed correctly, can load normally, and the port isn't occupied.

```bash
docker compose up
```

Example normal output:
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

Then press `Ctrl+C` to stop the service, and start it officially:

```bash
docker compose up -d
```

The server will now run in the background.

### 4. API Documentation

In the following tables, `localhost` can be replaced with your server address or Docker container name.

The port `8989` can be replaced with the port value you set in `compose.yml`.

If `CORE_API_TOKEN` is not set or empty, translation plugins use the API without password.

If `CORE_API_TOKEN` is set, translation plugins use the API with password.

Replace `your_token` in the following tables with your `CORE_API_TOKEN` value from `config.ini`.

#### Translation Plugin Interfaces:

> Note:
> 
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - Enable `Beta` features in developer mode in `Settings` to see `Custom API Settings` under `Translation Services` ([official tutorial with images](https://immersivetranslate.com/docs/services/custom/)). Then increase the `Maximum Requests per Second` in `Custom API Settings` to fully utilize server performance. I set `Maximum Requests per Second` to `5000` and `Maximum Paragraphs per Request` to `10`. You can adjust based on your server hardware.
> 
> - [Kiss Translator](https://github.com/fishjar/kiss-translator) - Scroll down in `Settings` page to find the custom interface `Custom`. Similarly, set `Maximum Concurrent Requests` and `Request Interval Time` to fully utilize server performance. I set `Maximum Concurrent Requests` to `100` and `Request Interval Time` to `1`. You can adjust based on your server configuration.
>
> Configure the plugin's custom interface address according to the table below.

| Name | URL | Plugin Setting |
| --- | --- | --- |
| Immersive Translation (No Password) | `http://localhost:8989/imme` | `Custom API Settings` - `API URL` |
| Immersive Translation (With Password) | `http://localhost:8989/imme?token=your_token` | Same as above, change `your_token` to your `CORE_API_TOKEN` value |
| Kiss Translator (No Password) | `http://localhost:8989/kiss` | `Interface Settings` - `Custom` - `URL` |
| Kiss Translator (With Password) | `http://localhost:8989/kiss` | Same as above, fill `KEY` with `your_token` |

**Regular users can start using the service after setting up the plugin interface address according to the table above. Skip to "How to Update" below.**

#### Developer APIs:

> Base URL: `http://localhost:8989`

| Name | URL | Request Format | Response Format | Auth Header |
| --- | --- | --- | --- | --- |
| Service Version | `/version` | None | None | None |
| Language Pair List | `/models` | None | None | Authorization: your_token |
| Standard Translation | `/translate` | `{"from": "en", "to": "zh", "text": "Hello, world!"}` | `{"result": "你好，世界！"}` | Authorization: your_token |
| Batch Translation | `/translate/batch` | `{"from": "en", "to": "zh", "texts": ["Hello, world!", "Hello, world!"]}` | `{"results": ["你好，世界！", "你好，世界！"]}` | Authorization: your_token |
| Health Check | `/health` | None | `{"status": "ok"}` | None |
| Heartbeat Check | `/__heartbeat__` | None | `Ready` | None |
| Load Balancer Heartbeat | `/__lbheartbeat__` | None | `Ready` | None |

### How to Update

As this is a beta version of server and models, you may encounter issues. Regular updates are recommended.

Download new models, extract and overwrite the original `models` folder, then update and restart the server:
```bash
docker compose down
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

## Other Information

Windows, Mac, and Linux standalone client software version [MTranServerCore](https://github.com/xxnuo/MTranServerCore) is under development, please be patient.

You can also try it out by installing `Docker Desktop` on your computer and following the guide above to deploy with `Docker Compose`.

The server-side translation inference framework uses the C++-written [marian-nmt](https://github.com/marian-nmt/marian-dev) framework.

Server API source code repository: [MTranServerCore](https://github.com/xxnuo/MTranServerCore) (not yet complete, please be patient)

## Support the Project

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[Mainland China 💗 Afdian](https://afdian.com/a/xxnuo)

---

WeChat: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

Feel free to connect with me to discuss technology and open-source projects!

I'm currently seeking job opportunities. Please contact me to view my resume.

---
