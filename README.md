# MTranServer 
> 迷你翻译服务器

<img src="./logo.jpg" width="auto" height="128" align="right">

[English](README_en.md) | [日本語](README_ja.md) | 中文

一个超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。单个请求平均响应时间 50ms

翻译质量与 Google 翻译相当。

注意本模型专注于性能优化，所以翻译质量肯定是不如大模型翻译的效果。

<img src="./preview.png" width="auto" height="328">

## 同类项目效果(CPU,英译中)

| 项目名称 | 内存占用 | 并发性能 | 翻译效果 | 速度 | 其他信息 |
|----------|----------|----------|----------|------|----------|
| [facebook/nllb-200-distilled-600M](https://github.com/thammegowda/nllb-serve) | 很高 | 差 | 一般 | 慢 | Android 的 [RTranslator](https://github.com/niedev/RTranslator) 有很多优化，但占用仍然高，速度也不快 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 很高 | 一般 | 一般 | 中等 | 中端 CPU 每秒处理 3 句，高端 CPU 每秒处理 15-20 句。[详情](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | 高 | 一般 | 略差 | 快 | [性能测试](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| MTranServer(本项目) | 低 | 高 | 一般 | 极快 | 单个请求平均响应时间 50ms |

非严格测试，非量化版本对比，仅供参考。

## Docker Compose 服务器部署

目前仅支持 amd64 架构 CPU 的 Docker 部署。ARM、RISCV 架构在适配中 😳

### 下载模型
[国内下载地址](https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink)
[国际下载地址]()

## 使用

目前可以在浏览器中使用沉浸式翻译插件、简约翻译(kiss translator)插件调用。

## 客户端版本

服务端翻译核心、Windows 和 Mac 客户端版本在适配中 [MTranServerCore](https://github.com/xxnuo/MTranServerCore) (暂未公开)

## 赞助我

[☕️ 爱发电](https://afdian.com/a/xxnuo)

---

微信: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

欢迎加我交流技术和开源相关项目～

找工作中。可以联系我查看我的简历。

---

