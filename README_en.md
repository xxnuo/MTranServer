# MTranServer 
> Mini Translation Server

[中文版](README.md) | English

A high-performance offline translation server with minimal resource requirements - runs on CPU with just 1GB memory, no GPU needed. Average response time of 50ms per request.

Translation quality comparable to Google Translate.

Note: This model prioritizes performance optimization, so translation quality may not match that of large language models.

## Comparison with Similar Projects (CPU, English to Chinese)

| Project Name | Memory Usage | Concurrency | Translation Quality | Speed | Additional Info |
|--------------|--------------|-------------|---------------------|-------|-----------------|
| [facebook/nllb-200-distilled-600M](https://github.com/thammegowda/nllb-serve) | Very High | Poor | Average | Slow | Android's [RTranslator](https://github.com/niedev/RTranslator) has optimizations but still has high resource usage and slower speed |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | Very High | Average | Average | Medium | Mid-range CPU: 3 sentences/s, high-end CPU: 15-20 sentences/s. [Details](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | High | Average | Below Average | Fast | [Performance Benchmarks](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| MTranServer (This Project) | Low | High | Average | Ultra Fast | 50ms average response time per request |

*Note: Non-rigorous testing, non-quantized version comparison, for reference only.

## Docker Compose Server Deployment

Currently only supports Docker deployment on amd64 architecture CPUs.

Support for ARM and RISC-V architectures is under development 😳

## Client Version

Windows and Mac client versions ([MTranServerCore](https://github.com/xxnuo/MTranServerCore)) are under development (not yet public). Currently available through browser extensions: Immersive Translation and Kiss Translator.

## Support the Project

[☕️ Support me on Afdian](https://afdian.com/a/xxnuo)

---

WeChat: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

Feel free to connect with me to discuss technology and open-source projects!

I'm currently seeking job opportunities. Please contact me to view my resume.

---
