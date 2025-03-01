# 高级设置

在 `compose.yml` 文件同级目录创建 `config.ini` 文件，写入以下内容按需修改：

```ini
; API 令牌，默认空
CORE_API_TOKEN=your_token
; 内部端口号，默认 8989
CORE_PORT=8989
; 日志级别，默认 WARNING
CORE_LOG_LEVEL=WARNING
; 工作线程数，默认自动设置
CORE_NUM_WORKERS=
; 请求超时时间，默认 30000ms
CORE_REQUEST_TIMEOUT=
; 最大并行翻译数，默认自动设置
CORE_MAX_PARALLEL_TRANSLATIONS=
```

> 也可以在环境变量使用相同的名字设置配置。
> 
> `config.ini` 配置文件的条目会覆盖环境变量设置。
