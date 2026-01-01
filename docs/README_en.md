# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

<!-- <img src="../images/icon.png" width="64px" height="64px" align="right" alt="MTran"> -->

A high-performance offline translation model server with minimal resource requirements - no GPU needed. Average response time of 50ms per request. Supports translation of major languages worldwide.

Note: This model server focuses on `offline translation`, `response speed`, `cross-platform deployment`, and `local execution` to achieve `unlimited free translation`. Due to model size and optimization constraints, the translation quality will not match that of large language models. For high-quality translation, consider using online large language model APIs.

> v4 has optimized memory usage, further improved speed, and enhanced stability. If you are using an old version, it is recommended to upgrade immediately!

<img src="../images/preview.png" width="auto" height="460">

## Online Demo

| Website                          | TOKEN                     | Other Interface                                                                | Provider |
| -------------------------------- | ------------------------- | ------------------------------------------------------------------------------ | -------- |
| `https://MTranServer.ipacel.cc/` | `__IpacEL_MT_API_TOKEN__` | Immersive Translate: `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | @ApliNi  |

Thanks to community contributors for providing trial services for users!

## Usage Guide

Download the latest version from [Releases](https://github.com/xxnuo/MTranServer/releases) and start the program in the command line.

[MTranServer](https://github.com/xxnuo/MTranServer) is mainly for server use, so currently only command line service and Docker deployment are available.
 
Plans are in place to provide a desktop application and browser extension for easier use, so stay tuned.

> **Important Note:**
>
> When translating a language pair for the first time, the server will automatically download the corresponding translation model (unless offline mode is enabled). This process may take some time depending on your network speed and model size. After the model is downloaded, subsequent translation requests will enjoy millisecond-level response speeds. It is recommended to test a translation once before formal use to allow the server to pre-download and load the model.
>
> The program is frequently updated. If you encounter problems, please try updating to the latest version.

After the server starts, the console will output the address of the simple UI and the online documentation address. Below is a preview:

![UI](../images/ui.png)

![Documentation](../images/swagger.png)


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
  -ui                   Enable Web UI (default true)
  -offline              Enable offline mode, disable automatic model download (default false)
  -worker-idle-timeout int  Worker idle timeout in seconds (default 300)

Examples:
  ./mtranserver --host 127.0.0.1 --port 8080
  ./mtranserver --ui --offline
  ./mtranserver -v
```

### Docker Compose Deployment

Create a `compose.yml` file in an empty directory, with the following content:

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
      # - MT_API_TOKEN=your_secret_token_here
    volumes:
      - ./models:/app/models
```

```bash
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

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
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - Enable `Beta` features in developer mode in `Settings` to see `Custom API Settings` under `Translation Services` ([official tutorial with images](https://immersivetranslate.com/docs/services/custom/)). Then increase the `Maximum Requests per Second` in `Custom API Settings` to fully utilize server performance. I set `Maximum Requests per Second` to `512` and `Maximum Paragraphs per Request` to `1`. You can adjust based on your server hardware.
>
> - [Kiss Translator](https://github.com/fishjar/kiss-translator) - Scroll down in `Settings` page to find the custom interface `Custom`. Similarly, set `Maximum Concurrent Requests` and `Request Interval Time` to fully utilize server performance. I set `Maximum Concurrent Requests` to `100` and `Request Interval Time` to `1`. You can adjust based on your server configuration.
>
> Configure the plugin's custom interface address according to the table below.

| Name                                  | URL                                           | Plugin Setting                                                                   |
| ------------------------------------- | --------------------------------------------- | -------------------------------------------------------------------------------- |
| Immersive Translation (No Password)   | `http://localhost:8989/imme`                  | `Custom API Settings` - `API URL`                                                |
| Immersive Translation (With Password) | `http://localhost:8989/imme?token=your_token` | Same as above, change `your_token` to your `MT_API_TOKEN` value |
| Kiss Translator (No Password)         | `http://localhost:8989/kiss`                  | `Interface Settings` - `Custom` - `URL`                                          |
| Kiss Translator (With Password)       | `http://localhost:8989/kiss`                  | Same as above, fill `KEY` with `your_token`                                      |
| DeepL Compatible                      | `http://localhost:8989/deepl`                 | Use `DeepL-Auth-Key` or `Bearer` authentication                                  |
| Google Compatible                     | `http://localhost:8989/google/language/translate/v2` | Use `key` parameter or `Bearer` authentication                            |
| Selection Translator                  | `http://localhost:8989/hcfy`                  | Support `token` parameter or `Bearer` authentication                             |

**Regular users can start using the service after setting up the plugin interface address according to the table above.**

## Comparison with Similar Projects

Here are some similar projects, you can try them if you have other needs:

| Project Name                                                           | Memory Usage   | Concurrency | Translation Quality | Speed      | Additional Info                                                                                                                                                   |
| ---------------------------------------------------------------------- | -------------- | ----------- | ------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)          | Very High      | Poor        | Average             | Slow       | Android port [RTranslator](https://github.com/niedev/RTranslator) has many optimizations, but still has high resource usage and is not fast                       |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)     | Very High      | Average     | Average             | Medium     | Mid-range CPU processes 3 sentences/s, high-end CPU processes 15-20 sentences/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks)           | High           | Average     | Below Average       | Fast       | [Performance Tests](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                                            |
| Any LLM                                                                | Extremely High | Dynamic     | Very Good           | Very Slow  | High hardware requirements. If you need high concurrency translation, it is recommended to use vllm framework to control translation concurrency through memory and VRAM usage. |
| MTranServer (This Project)                                             | Low            | High        | Average             | Ultra Fast | 50ms average response time per request.                                                                   |

> Table data is for CPU, English to Chinese scenarios simple testing, not strict testing, non-quantized version comparison, for reference only.

# Advanced Configuration Guide

Please refer to [API_en.md](API_en.md) and the API documentation after startup.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
