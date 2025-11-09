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

### API Usage

In the following tables, `localhost` can be replaced with your server address or Docker container name.

The port `8989` can be replaced with the port value you set in `compose.yml`.

If `API_TOKEN` or `CORE_API_TOKEN` is not set or empty, translation plugins use the API without password.

If `API_TOKEN` or `CORE_API_TOKEN` is set, translation plugins use the API with password.

Replace `your_token` in the following tables with your `API_TOKEN` or `CORE_API_TOKEN` value from environment variables.

#### Translation Plugin Interfaces

> Note:
>
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - Enable `Beta` features in developer mode in `Settings` to see `Custom API Settings` under `Translation Services` ([official tutorial with images](https://immersivetranslate.com/docs/services/custom/)). Then increase the `Maximum Requests per Second` in `Custom API Settings` to fully utilize server performance. I set `Maximum Requests per Second` to `5000` and `Maximum Paragraphs per Request` to `1`. You can adjust based on your server hardware.
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

## Support Me

[Buy me a coffee ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[Mainland China 💗 Like](./DONATE.md)

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
