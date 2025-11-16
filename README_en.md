# MTranServer

<!-- <img src="./images/icon.png" width="64px" height="64px" align="right" alt="MTran"> -->

[中文](README.md) | English

A high-performance offline translation server with minimal resource requirements - runs on CPU with just 1GB memory, no GPU needed. Average response time of 50ms per request. Supports translation of major languages worldwide.

Translation quality comparable to Google Translate.

Note: This model focuses on speed and private deployment on various devices, so the translation quality will not match that of large language models.

For high-quality translation, consider using online large language model APIs.

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

## Usage Guide

### Command Line Options

```bash
./mtranserver [options]

Options:
  -version, -v          Show version information
  -log-level string     Log level (debug, info, warn, error) (default "warn")
  -config-dir string    Configuration directory (default "~/.config/mtran/server")
  -model-dir string     Model directory (default "~/.config/mtran/models")
  -host string          Server host address (default "0.0.0.0")
  -port string          Server port (default "8989")
  -ui                   Enable Web UI (default false)
  -offline              Enable offline mode, disable automatic model download (default false)
  -worker-idle-timeout int  Worker idle timeout in seconds (default 300)

Examples:
  ./mtranserver --host 127.0.0.1 --port 8080
  ./mtranserver --ui --offline
  ./mtranserver -v
```

### Environment Variables

| Environment Variable  | Description                              | Default | Options                     |
| --------------------- | ---------------------------------------- | ------- | --------------------------- |
| MT_LOG_LEVEL          | Log level                                | warn    | debug, info, warn, error    |
| MT_CONFIG_DIR         | Configuration directory                  | ~/.config/mtran/server | Any path                    |
| MT_MODEL_DIR          | Model directory                          | ~/.config/mtran/models | Any path                    |
| MT_HOST               | Server host address                      | 0.0.0.0 | Any IP address              |
| MT_PORT               | Server port                              | 8989    | 1-65535                     |
| MT_UI                 | Enable Web UI                            | false   | true, false                 |
| MT_OFFLINE            | Offline mode, disable automatic download of new language models, only use downloaded models | false   | true, false                 |
| MT_WORKER_IDLE_TIMEOUT| Worker idle timeout (seconds)            | 300     | Any positive integer        |
| API_TOKEN             | API access token                         | empty   | Any string                  |
| CORE_API_TOKEN        | API access token (alternative)           | empty   | Any string                  |

Example:

```bash
# Set log level to debug
export MT_LOG_LEVEL=debug

# Set port to 9000
export MT_PORT=9000

# Start the server
./mtranserver
```

### API Documentation

#### System Endpoints

| Endpoint | Method | Description | Auth Required |
| -------- | ------ | ----------- | ------------- |
| `/version` | GET | Get service version | No |
| `/health` | GET | Health check | No |
| `/__heartbeat__` | GET | Heartbeat check | No |
| `/__lbheartbeat__` | GET | Load balancer heartbeat check | No |
| `/docs/*` | GET | Swagger API documentation | No |

#### Translation Endpoints

| Endpoint | Method | Description | Auth Required |
| -------- | ------ | ----------- | ------------- |
| `/languages` | GET | Get supported language list | Yes |
| `/translate` | POST | Single text translation | Yes |
| `/translate/batch` | POST | Batch translation | Yes |

**Single Text Translation Request Example:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**Batch Translation Request Example:**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**Authentication Methods:**

- Header: `Authorization: Bearer <token>`
- Query: `?token=<token>`

#### Translation Plugin Compatible Endpoints

The server provides compatible endpoints for multiple translation plugins:

| Endpoint | Method | Description | Supported Plugins |
| -------- | ------ | ----------- | ----------------- |
| `/imme` | POST | Immersive Translation plugin endpoint | [Immersive Translation](https://immersivetranslate.com/) |
| `/kiss` | POST | Kiss Translator plugin endpoint | [Kiss Translator](https://github.com/fishjar/kiss-translator) |
| `/deepl` | POST | DeepL API v2 compatible endpoint | Clients supporting DeepL API |
| `/google/language/translate/v2` | POST | Google Translate API v2 compatible endpoint | Clients supporting Google Translate API |
| `/google/translate_a/single` | GET | Google translate_a/single compatible endpoint | Clients supporting Google web translation |
| `/hcfy` | POST | Selection Translator compatible endpoint | [Selection Translator](https://github.com/Selection-Translator/crx-selection-translate) |

**Plugin Configuration Guide:**

> Note:
>
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - Enable `Beta` features in developer mode in `Settings` to see `Custom API Settings` under `Translation Services` ([official tutorial with images](https://immersivetranslate.com/docs/services/custom/)). Then increase the `Maximum Requests per Second` in `Custom API Settings` to fully utilize server performance. I set `Maximum Requests per Second` to `5000` and `Maximum Paragraphs per Request` to `1`. You can adjust based on your server hardware.
>
> - [Kiss Translator](https://github.com/fishjar/kiss-translator) - Scroll down in `Settings` page to find the custom interface `Custom`. Similarly, set `Maximum Concurrent Requests` and `Request Interval Time` to fully utilize server performance. I set `Maximum Concurrent Requests` to `100` and `Request Interval Time` to `1`. You can adjust based on your server configuration.
>
> **Important Note:** When translating a language pair for the first time, the server will automatically download the corresponding translation model (unless offline mode is enabled). This process may take some time depending on your network speed and model size. After the model is downloaded, the engine startup also requires a few seconds. Once ready, subsequent translation requests will enjoy millisecond-level response times. It's recommended to test a translation before actual use to allow the server to pre-download and load the models.
>
> Configure the plugin's custom interface address according to the table below.

| Name                                  | URL                                           | Plugin Setting                                                                   |
| ------------------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------- |
| Immersive Translation (No Password)   | `http://localhost:8989/imme`                  | `Custom API Settings` - `API URL`                                                |
| Immersive Translation (With Password) | `http://localhost:8989/imme?token=your_token` | Same as above, change `your_token` to your `API_TOKEN` or `CORE_API_TOKEN` value |
| Kiss Translator (No Password)         | `http://localhost:8989/kiss`                  | `Interface Settings` - `Custom` - `URL`                                          |
| Kiss Translator (With Password)       | `http://localhost:8989/kiss`                  | Same as above, fill `KEY` with `your_token`                                      |
| DeepL Compatible                      | `http://localhost:8989/deepl`                 | Use `DeepL-Auth-Key` or `Bearer` authentication                                  |
| Google Compatible                     | `http://localhost:8989/google/language/translate/v2` | Use `key` parameter or `Bearer` authentication                            |
| Selection Translator                  | `http://localhost:8989/hcfy`                  | Support `token` parameter or `Bearer` authentication                             |

**Regular users can start using the service after setting up the plugin interface address according to the table above.**

## Support Me

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[Mainland China 💗 Like](./DONATE.md)

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
