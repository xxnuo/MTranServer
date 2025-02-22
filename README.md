# MTranServer
An ultra-low resource consumption super-fast offline translation server, which only requires a CPU + 1G of memory to run. No need for GPU. The average response time for a single request is 50ms.

超低资源消耗超快的离线翻译服务器，仅需 CPU + 1G 内存即可运行，无需 GPU。单个请求平均响应时间 50ms

The quality of translation is comparable to Google Translate

翻译质量与 Google 翻译相当。

## Docker Compose Deployment

Currently, only amd64 CPU is supported.

目前仅支持 amd64 架构 CPU 的 Docker 部署。

```bash
docker run -d --name mtranserver -p 8686:8686 mtranserver:latest
```

还在开发中
