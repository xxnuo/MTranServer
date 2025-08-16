# MTranServer

<img src="./images/icon.png" width="64px" height="64px" align="right" alt="MTran">

[中文](README.md) | English

A high-performance offline translation server with minimal resource requirements - runs on CPU with just 1GB memory, no GPU needed. Average response time of 50ms per request. Supports translation of major languages worldwide.

Translation quality comparable to Google Translate.

Note: This model focuses on speed and private deployment on various devices, so the translation quality will not match that of large language models.

For high-quality translation, consider using online large language model APIs.

## Demo

> Coming soon

<img src="./images/preview.png" width="auto" height="460">

## Comparison with Similar Projects (CPU, English to Chinese)

| Project Name                                                           | Memory Usage   | Concurrency | Translation Quality | Speed      | Additional Info                                                                                                                                                   |
| ---------------------------------------------------------------------- | -------------- | ----------- | ------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | Very High      | Poor        | Average             | Slow       | Android port [RTranslator](https://github.com/niedev/RTranslator) has many optimizations, but still has high resource usage and is not fast                       |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)     | Very High      | Average     | Average             | Medium     | Mid-range CPU processes 3 sentences/s, high-end CPU processes 15-20 sentences/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks)           | High           | Average     | Below Average       | Fast       | [Performance Tests](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                                            |
| Any LLM                                                                | Extremely High | Dynamic     | Very Good           | Very Slow  | 32B+ parameter models work well but have high hardware requirements                                                                                               |
| MTranServer (This Project)                                             | Low            | High        | Average             | Ultra Fast | 50ms average response time per request                                                                                                                    |

> Existing small-parameter quantized versions of Transformer architecture large models are not considered, as actual research and usage have shown that translation quality is very unstable with random translations, severe hallucinations, and slow speeds. We will test Diffusion architecture language models when they are released.
>
> Table data is for reference only, not strict testing, non-quantized version comparison.

## Update Log

[Update Log](./CHANGELOG.md)

## Desktop Client

Desktop client software coming soon, stay tuned.

## Server Deployment

> This may be challenging for regular users, consider using the desktop client when available.

### 1.1 Requirements

- Docker
- Docker Compose (optional)

### 1.2 Image Versions

- Default version (download models automatically when no built-in models are available): `xxnuo/mtranserver:latest`
- Built-in Chinese <-> English translation models: `xxnuo/mtranserver:latest-zh`
- Built-in Japanese <-> English translation models: `xxnuo/mtranserver:latest-ja`

### 1.3 Docker Deployment

Copy the command below and execute it in your terminal.

```bash
docker run -d --name mtranserver -p 8989:8989 -e API_TOKEN=your_token xxnuo/mtranserver:latest
```

### 1.4 Docker Compose Deployment

Prepare a folder for configuration files on your server and run the following commands in terminal:

```bash
mkdir mtranserver
cd mtranserver
touch compose.yml
```

Open `compose.yml` with an editor and add the following content:

> 1. Change `your_token` below to your own password using English letters and numbers. For internal network use, setting a password is optional, but for `cloud servers`, it is strongly recommended to set a password to protect against `scanning, attacks, and abuse`.
>
> 2. To change the port, modify the `ports` value. For example, change to `9999:8989` to map the service port to local port 9999.

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

First, test the service to ensure the port isn't occupied:

```bash
docker compose up
```

Example normal output:

```bash
HTTP Service URL: http://0.0.0.0:8989
Swagger UI: http://0.0.0.0:8989/docs
```

Then press `Ctrl+C` to stop the service, and start it officially:

```bash
docker compose up -d
```

The server will now run in the background.

## Preparing Models

⚠️ Note: Models will be automatically downloaded in the background when you first request the translation API, no manual download needed.

The automatic model download feature requires internet connection (no proxy needed in mainland China), **all subsequent translations and other functions work completely offline without internet**.

**So the first translation won't be instant, you'll need to wait a moment!**

You can monitor the progress in the Docker logs. Download speed depends on your network speed, typically completing a language model download within 10 seconds. If the download times out or fails, check if your container has normal internet access.

If your machine is on an internal network without internet access, you can follow the instructions below to manually download models.

### API Usage

In the following tables, `localhost` can be replaced with your server address or Docker container name.

The port `8989` can be replaced with the port value you set in `compose.yml`.

If `API_TOKEN` or `CORE_API_TOKEN` is not set or empty, translation plugins use the API without password.

If `API_TOKEN` or `CORE_API_TOKEN` is set, translation plugins use the API with password.

Replace `your_token` in the following tables with your `API_TOKEN` or `CORE_API_TOKEN` value from environment variables.

#### Translation Plugin Interfaces

> Note:
>
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - Enable `Beta` features in developer mode in `Settings` to see `Custom API Settings` under `Translation Services` ([official tutorial with images](https://immersivetranslate.com/docs/services/custom/)). Then increase the `Maximum Requests per Second` in `Custom API Settings` to fully utilize server performance. I set `Maximum Requests per Second` to `5000` and `Maximum Paragraphs per Request` to `10`. You can adjust based on your server hardware.
>
> - [Kiss Translator](https://github.com/fishjar/kiss-translator) - Scroll down in `Settings` page to find the custom interface `Custom`. Similarly, set `Maximum Concurrent Requests` and `Request Interval Time` to fully utilize server performance. I set `Maximum Concurrent Requests` to `100` and `Request Interval Time` to `1`. You can adjust based on your server configuration.
>
> Configure the plugin's custom interface address according to the table below. Note: The first request will be slower because it needs to load the model. Subsequent requests will be faster.

| Name                                  | URL                                           | Plugin Setting                                                                   |
| ------------------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------- |
| Immersive Translation (No Password)   | `http://localhost:8989/imme`                  | `Custom API Settings` - `API URL`                                                |
| Immersive Translation (With Password) | `http://localhost:8989/imme?token=your_token` | Same as above, change `your_token` to your `API_TOKEN` or `CORE_API_TOKEN` value |
| Kiss Translator (No Password)         | `http://localhost:8989/kiss`                  | `Interface Settings` - `Custom` - `URL`                                          |
| Kiss Translator (With Password)       | `http://localhost:8989/kiss`                  | Same as above, fill `KEY` with `your_token`                                      |

**Regular users can start using the service after setting up the plugin interface address according to the table above.**

### Keep Updating

```bash
cd mtranserver
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

### Developer APIs

> Base URL: `http://localhost:8989`
>
> In v3.0.0, complete API documentation can be viewed at `http://localhost:8989/docs` using Swagger UI

| Name                                    | URL                      | Request Format                                                                         | Response Format                                                    | Auth Header               |
| --------------------------------------- | ------------------------ | -------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | ------------------------- |
| Service Version                         | `/version`               | None                                                                                   | `{"version": "v3.0.0"}`                                            | None                      |
| Supported Languages                     | `/languages`             | None                                                                                   | `{"languages":["en","zh","ja","ko"...]}`                           | Authorization: your_token |
| Standard Translation                    | `/translate`             | `{"from": "en", "to": "zh", "text": "Hello, world!"}`                                  | `{"result": "你好，世界！"}`                                       | Authorization: your_token |
| Batch Translation                       | `/translate/batch`       | `{"from": "en", "to": "zh", "texts": ["Hello, world!", "Hello, world!"]}`              | `{"results": ["你好，世界！", "你好，世界！"]}`                    | Authorization: your_token |
| Health Check                            | `/health`                | None                                                                                   | `{"status": "ok"}`                                                 | None                      |
| Heartbeat Check                         | `/__heartbeat__`         | None                                                                                   | `Ready`                                                            | None                      |
| Load Balancer Heartbeat                 | `/__lbheartbeat__`       | None                                                                                   | `Ready`                                                            | None                      |
| Google Translate Compatible Interface 1 | `/language/translate/v2` | `{"q": "The Great Pyramid of Giza", "source": "en", "target": "zh", "format": "text"}` | `{"data": {"translations": [{"translatedText": "吉萨大金字塔"}]}}` | Authorization: your_token |

> Developer advanced settings please refer to [CONFIG.md](./CONFIG.md)

## Support Me

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[Mainland China 💗 Like](./DONATE.md)

## Contributors

<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/Devillmy"><img src="https://avatars.githubusercontent.com/u/36851750?v=3?s=100" width="100px;" alt="Lv Meiyang"/><br /><sub><b>Lv Meiyang</b></sub></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/xxnuo"><img src="https://avatars.githubusercontent.com/u/54252779?v=3?s=100" width="100px;" alt="Leo"/><br /><sub><b>Leo</b></sub></td>
    </tr>
  </tbody>
</table>

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://star-history.com/#xxnuo/MTranServer&Timeline)
