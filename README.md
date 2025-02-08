# MTranServer
An ultra-low resource consumption super-fast offline translation server, which only requires a CPU + 1G of memory to run. No need for GPU.

超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。

The quality of translation is comparable to Google Translate

翻译质量与 Google 翻译相当。

Currently supports Chinese to English, and English to Chinese is under rapid development.

目前支持中译英，英译中正在快速开发中。

## Docker Compose Deployment

Currently, only amd64 CPU is supported.

目前仅支持 amd64 架构 CPU 的 Docker 部署。

```bash
docker run -d --name mtranserver -p 8686:8686 mtranserver:latest
```

## Appendix: Supported Language List (in Chinese)

### 已支持

双向翻译 (↔️ 英语):
保加利亚语, 加泰罗尼亚语, 捷克语, 丹麦语, 荷兰语, 爱沙尼亚语, 芬兰语, 法语, 德语, 希腊语, 匈牙利语, 印度尼西亚语, 意大利语, 波兰语, 葡萄牙语, 罗马尼亚语, 俄语, 斯洛文尼亚语, 西班牙语, 瑞典语, 土耳其语

单向翻译 (→ 英语):
简体中文, 克罗地亚语, 日语, 韩语, 拉脱维亚语, 立陶宛语, 塞尔维亚语, 斯洛伐克语, 乌克兰语, 越南语

### 开发中

双向翻译 (↔️ 英语):
波斯语

单向翻译 (→ 英语):
波斯尼亚语, 冰岛语, 马耳他语, 书面挪威语, 新挪威语

单向翻译 (← 英语):
克罗地亚语, 拉脱维亚语, 斯洛伐克语, 乌克兰语, **简体中文**
