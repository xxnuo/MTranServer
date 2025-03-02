# MTranServer
An ultra-low resource consumption super-fast offline translation server, which only requires a CPU + 1G of memory to run. No need for GPU. The average response time for a single request is 50ms.

超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。单个请求平均响应时间 50ms

The quality of translation is comparable to Google Translate

翻译质量与 Google 翻译相当。

## 同类项目效果(CPU,英译中)

| 项目名称                          | 内存占用 | 并发性能 | 翻译效果 | 速度       | 其他信息                                                                 |
|-----------------------------------|----------|----------|----------|------------|--------------------------------------------------------------------------|
| [facebook/nllb-200-distilled-600M](https://github.com/thammegowda/nllb-serve)  | 3G       | 差     | 一般     | 一般       | [Android](https://github.com/niedev/RTranslator) 运行需要 2.5G RAM，翻译 75 tokens 耗时 8s，[RTranslator](https://github.com/niedev/RTranslator) 优化到了 1.3G RAM，75 tokens 只需要 2s。 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate)                    | -        | 一般        | 一般        | 一般          | LibreTranslate 在中端 CPU 上每秒可以处理约 3 句话，在高端 CPU 上每秒可以处理 15-20 句话。[performance](https://community.libretranslate.com/t/performance-benchmark-data/486)                                                                        |
|[OPUS-MT model](https://github.com/OpenNMT/CTranslate2#benchmarks)|-|一般|略差|快|[benchmarks](https://github.com/OpenNMT/CTranslate2#benchmarks)|
| MTranServer(本项目)                      | 1G       | 高     | 一般        | 快       | 单个请求平均响应时间 50ms |

这是一个粗略的 benchmark，因为上面两个模型实际上并不是针对端到端优化的模型，所以速度和占用是正常的。
## Docker Compose Deployment

Currently, only amd64 CPU is supported.

目前仅支持 amd64 架构 CPU 的 Docker 部署。

还在开发中
