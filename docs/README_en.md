# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

A free offline translation service that runs on your own computer, with no GPU required. Supports multiple languages and browser and editor plugins. Speed and memory use depend on your computer, languages, and text length.

Useful for everyday reading and understanding the main meaning. Translations can contain mistakes; check important content against the original.

> v5 uses a new translation engine with significantly improved performance. New Chinese–English translation models with better translation quality are currently being trained. After upgrading, the first translation in a language direction may require preparing the translation files again. Please wait for this to finish.

<img src="../images/preview.png" width="auto" height="460">

## Online Demo

| Website                                                                      | TOKEN                     | Other Interface                                                                         | Provider                             |
| ---------------------------------------------------------------------------- | ------------------------- | --------------------------------------------------------------------------------------- | ------------------------------------ |
| [ipacel.cc](https://MTranServer.ipacel.cc/ui/?token=__IpacEL_MT_API_TOKEN__) | `__IpacEL_MT_API_TOKEN__` | Immersive Translate: `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | [@ApliNi](https://github.com/ApliNi) |

Thanks to community contributors for providing trial services for users!

## Usage Guide

Now supports desktop one-click launch! Supports Windows, Mac, Linux.

### Desktop

#### Manual Download

Download the latest desktop version for your platform from [Releases](https://github.com/xxnuo/MTranServer/releases), install and launch directly.

After the desktop app launches, it will create a tray menu to conveniently manage the service.

The program includes a simple UI and online debug documentation.

For detailed usage instructions, jump to [Ecosystem Projects](#Ecosystem-Projects)

Preview (latest version has updates):

![UI](../images/ui.png)

![Documentation](../images/swagger.png)

### Server

For everyday use, install the desktop app and launch it. Server users can use Docker or install from the command line. v5 does not currently provide standalone server executables.

#### Quick Start

Programmers can start the server directly via command line:

```bash
npx mtranserver@latest
```

> `npx` can be replaced with any package manager you prefer, such as `bunx`, `pnpx`, etc.

> The first translation in each language direction downloads and prepares the required files. Stay online and wait for this to finish. Prepared files are reused afterward. Try a short text first to check that translation works. File preparation on first use or after an upgrade takes extra time.

#### Quick Install

```bash
npm i -g mtranserver@latest
```

> `npm` can be replaced with any package manager you prefer, such as `bun`, `pnpm`, etc.

Then run `mtranserver`.

#### Docker Compose Deployment

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

### Upgrading and Offline Use

1. Keep your existing configuration and translation model folders when upgrading.
2. Stay online after upgrading and try each translation direction you need. Wait for file preparation to finish.
3. Then enable offline mode in settings before disconnecting. Offline mode can only use translation directions that are already prepared.
4. When moving to another computer or migrating Docker, save both the configuration and translation model folders. Copying only the model folder is not enough.

The Docker example above saves these folders as `config` and `models` in the current directory. Prepare files online first, then set `MT_OFFLINE` to `true` and restart for offline use.

## Ecosystem Projects

### IDE Plugins

#### [MTranCode](https://github.com/xxnuo/MTranCode) Comment Translation Plugin

Supports VS Code, Cursor, Augment, and other VS Code-based IDEs.

Search for **MTranCode** in the extension marketplace to install the comment translation plugin.

The plugin defaults to calling the server at `http://localhost:8989` for comment and code translation. You can adjust it in settings.

This plugin is forked from [vscode-comment-translate](https://github.com/intellism/vscode-comment-translate).

### Browser Extension

#### [MTranBrowser](https://github.com/xxnuo/MTranBrowser)

TODO: Under active development.

> If you develop a derivative project, feel free to submit a PR. I will add your project to the ecosystem list.
>
> By the way, the project has been published to npm. You can directly call the simple library interface in other programs to implement translation functionality. For specific information, check the TypeScript type definitions.

## Compatible Interfaces

The server provides compatible endpoints for multiple translation plugins:

| Endpoint                        | Method | Description                                   | Supported Plugins                                                                       |
| ------------------------------- | ------ | --------------------------------------------- | --------------------------------------------------------------------------------------- |
| `/imme`                         | POST   | Immersive Translation plugin endpoint         | [Immersive Translation](https://immersivetranslate.com/)                                |
| `/kiss`                         | POST   | Kiss Translator plugin endpoint               | [Kiss Translator](https://github.com/fishjar/kiss-translator)                           |
| `/deepl`                        | POST   | DeepL API v2 compatible endpoint              | Clients supporting DeepL API                                                            |
| `/deeplx`                       | POST   | DeepLX compatible endpoint                    | Clients supporting DeepLX API                                                           |
| `/hcfy`                         | POST   | Selection Translator compatible endpoint      | [Selection Translator](https://github.com/Selection-Translator/crx-selection-translate) |
| `/google/language/translate/v2` | POST   | Google Translate API v2 compatible endpoint   | Clients supporting Google Translate API                                                 |
| `/google/translate_a/single`    | GET    | Google translate_a/single compatible endpoint | Clients supporting Google web translation                                               |

**Plugin Configuration Guide:**

> Enter the addresses in the table below in your plugin settings. Start with the default request rate and simultaneous translation settings. Lower them if translations time out or your computer is overloaded.

| Name                                  | URL                                                  | Plugin Setting                                                  |
| ------------------------------------- | ---------------------------------------------------- | --------------------------------------------------------------- |
| Immersive Translation (No Password)   | `http://localhost:8989/imme`                         | `Custom API Settings` - `API URL`                               |
| Immersive Translation (With Password) | `http://localhost:8989/imme?token=your_token`        | Same as above, change `your_token` to your `MT_API_TOKEN` value |
| Kiss Translator (No Password)         | `http://localhost:8989/kiss`                         | `Interface Settings` - `Custom` - `URL`                         |
| Kiss Translator (With Password)       | `http://localhost:8989/kiss`                         | Same as above, fill `KEY` with `your_token`                     |
| DeepL Compatible                      | `http://localhost:8989/deepl`                        | Use `DeepL-Auth-Key` or `Bearer` authentication                 |
| DeepLX Compatible                     | `http://localhost:8989/deeplx`                       | Support `token` parameter or `Bearer` authentication            |
| Google Compatible                     | `http://localhost:8989/google/language/translate/v2` | Use `key` parameter or `Bearer` authentication                  |
| Selection Translator                  | `http://localhost:8989/hcfy`                         | Support `token` parameter or `Bearer` authentication            |

**Regular users can start using the service after setting up the plugin interface address according to the table above.**

### Command Line Options

```bash
mtranserver --help

  --help                              Show help
  --host <ip>                         Listen address, default 0.0.0.0
  --port <port>                       Port, default 8989
  --ui / --no-ui                      Enable or disable the web interface
  --offline                           Use previously prepared translation files
  --worker-idle-timeout <seconds>      Release idle translation engine, default 60 seconds
  --cache-size <count>                Store recent results, default 1000; 0 disables caching
  --download en_zh zh_en              Download and prepare translation directions
  --languages [filter]                List available translation directions

  mtranserver --host 127.0.0.1 --port 8080
  mtranserver --download en_zh zh_en
  mtranserver --offline
```

## Performance Tests (v5 ONNX)

MTranServer **5.1.0**, default quantized ONNX models, translation cache disabled (`MT_CACHE_SIZE=0`). Throughput is the median of three rounds; results are for reference and vary with hardware, text and system load.

| Direction | Input (characters) | Backend / inference threads | Concurrent requests | Mean (ms) | P95 (ms) | Throughput (requests/s) |
| --- | --- | --- | --- | --- | --- | --- |
| English → Chinese | Sentence 49–94 | CPU / 6 | 1 | 11.5 | 16.6 | 86.51 |
| English → Chinese | Sentence 49–94 | CPU / 6 | 4 | 35.6 | 50.8 | 108.33 |
| English → Chinese | Paragraph 234–344 | CPU / 6 | 1 | 27.7 | 35.0 | 36.31 |
| Chinese → English | Sentence 14–28 | CPU / 7 | 1 | 20.2 | 28.3 | 48.61 |
| Chinese → English | Sentence 14–28 | CPU / 7 | 4 | 68.2 | 89.1 | 56.19 |
| Chinese → English | Paragraph 65–104 | CPU / 7 | 1 | 62.5 | 85.7 | 15.52 |
| German → English | Sentence 50–113 | CPU / 5 | 1 | 12.6 | 17.7 | 83.02 |
| German → English | Sentence 50–113 | CPU / 5 | 4 | 39.7 | 51.5 | 96.51 |
| German → English | Paragraph 232–395 | CPU / 5 | 1 | 33.4 | 43.4 | 30.00 |

| Direction | First load, backend selection + translation (s) | RSS after warm-up (MiB) | Peak RSS (MiB) | ONNX model (MiB) |
| --- | --- | --- | --- | --- |
| English → Chinese | 33.47 | 364.3 | 391.7 | 118.8 |
| Chinese → English | 62.99 | 410.0 | 475.7 | 125.5 |
| German → English | 26.44 | 319.3 | 345.1 | 71.9 |

### Same-round Comparison with Published 5.0.1

| Direction | Input | Concurrency | 5.0.1 (requests/s) | 5.1.0 (requests/s) | Speed ratio |
| --- | --- | --- | --- | --- | --- |
| English → Chinese | Sentence | 1 | 46.92 | 83.52 | 1.78× |
| English → Chinese | Sentence | 4 | 57.25 | 93.65 | 1.64× |
| English → Chinese | Paragraph | 1 | 17.27 | 32.15 | 1.86× |
| Chinese → English | Sentence | 1 | 28.76 | 48.42 | 1.68× |
| Chinese → English | Sentence | 4 | 28.86 | 51.69 | 1.79× |
| Chinese → English | Paragraph | 1 | 7.40 | 15.58 | 2.10× |
| German → English | Sentence | 1 | 48.45 | 75.11 | 1.55× |
| German → English | Sentence | 4 | 46.20 | 93.65 | 2.03× |
| German → English | Paragraph | 1 | 14.78 | 30.57 | 2.07× |

## Comparison with Similar Projects

The table preserves earlier comparison records and adds the current v5 English-to-Chinese measurement. Test environments differ; use it as a reference:

| Project Name                                                       | Memory Usage   | Concurrency | Translation Quality | Speed      | Additional Info                                                                                                                                                   |
| ------------------------------------------------------------------ | -------------- | ----------- | ------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)      | Very High      | Poor        | Average             | Slow       | Android port [RTranslator](https://github.com/niedev/RTranslator) has many optimizations, but still has high resource usage and is not fast                       |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | Very High      | Average     | Average             | Medium     | Mid-range CPU processes 3 sentences/s, high-end CPU processes 15-20 sentences/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)       | High           | Average     | Below Average       | Fast       | [Performance Tests](https://github.com/OpenNMT/CTranslate2#benchmarks)                                                                                            |
| Any LLM                                                            | Extremely High | Dynamic     | Very Good           | Very Slow  | High hardware requirements. If you need high concurrency translation, it is recommended to use vllm framework.                                                    |
| MTranServer (previous Bergamot engine)                                         | Low            | High        | Average             | Ultra Fast | 50ms average response time per request.                                                                                                                           |
| MTranServer (v5 ONNX, automatic backend) | 392MiB (peak, EN-ZH) | Measured concurrency limit | Not evaluated | 86.51/s | English-to-Chinese sentences; CPU with 6 threads selected automatically; conditions above |

> Earlier rows retain the original README’s informal CPU English-to-Chinese tests and non-quantized comparison. The v5 row uses the measurements above with default quantized ONNX models.

> The 50ms figure is a historical result from the previous Bergamot engine, not a measurement of v5 ONNX Runtime. The original README did not record the hardware, text length, sample count, or cache settings. Qualitative ratings are also retained from that version; this is not a current ranking measured in a shared test environment.

[Original record](https://github.com/xxnuo/MTranServer/blob/1f05f8536940f825be205e3434fd7fe964c56a26/docs/README_en.md)

# Advanced Configuration Guide

Please refer to [API_en.md](API_en.md) and the API documentation after startup.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
