# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

A free offline translation service that runs on your own computer, with no GPU required. Supports multiple languages and browser and editor plugins. Speed and memory use depend on your computer, languages, and text length.

Useful for everyday reading and understanding the main meaning. Translations can contain mistakes; check important content against the original.

> v5 uses a new translation engine. After upgrading, the first translation in a language direction may require preparing the translation files again. Please wait for this to finish.

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

## Comparison with Similar Projects

Speed, memory use, and translation quality vary by device, language, and text. Old test numbers do not represent v5, so performance rankings have been removed. You can also explore these projects:

- [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb)
- [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)
- [OPUS-MT](https://github.com/Helsinki-NLP/Opus-MT)

# Advanced Configuration Guide

Please refer to [API_en.md](API_en.md) and the API documentation after startup.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for the inspiration for local translation.

[ONNX Runtime](https://github.com/microsoft/onnxruntime) for the translation runtime.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
