v3.2.1
- 重构进程守护，解决进程残留问题
- 添加 workers-per-language 参数，支持每个语言启动多个 worker 进程，默认 1
- 降低 worker 空闲超时时间，默认 60 秒

v3.1.23
- 发布稳定版
- Release stable version

v3.1.22
- 增强语言检测：支持混合脚本检测、置信度评分、动态语言支持
- 优化相邻语言段合并逻辑，提升检测准确性
- 添加内存监控功能
- 改进 worker 停止和清理逻辑
- /imme 接口默认启用 isHTML
- 日志支持彩色输出
- Enhance language detection: support mixed script detection, confidence scoring, dynamic language support
- Optimize merging of adjacent language segments for improved detection accuracy
- Add memory monitoring
- Improve worker stop and cleanup logic
- Default enable isHTML for /imme endpoint
- Add color support for logger

v3.1.21
- 修复格式对齐问题
- Fix format alignment issue

v3.1.20
- 添加 worker 版本信息
- Add worker version info

v3.1.19
- 支持检测和翻译文本中的多语言段落
- 实现分段翻译以改进错误处理
- 增强 Compute 方法的连接错误检查
- 移除 emoji 处理（已移至 MTranCore）
- 添加服务脚本
- Support detecting and translating multiple language segments in text
- Implement segmented translation for improved error handling
- Enhance connection error checks in Compute method
- Remove emoji handling (moved to MTranCore)
- Add service script

v3.1.18
- 修复网页界面类型问题
- Fix web interface type issue

v3.1.17
- 改进网页界面，添加语言名称国际化
- Improve web interface with language name i18n

v3.1.16
- 改进网页界面：添加语言名称、历史记录、工具按钮等
- Improve the web interface: add language names, history records, tool buttons, etc.

v3.1.15
- 修复下载模型提示校验失败的问题
- Fix the issue of model download verification failure

v3.1.14
- 工作时自动下载使用最新版本的模型，无需手动下载（离线模式则不会更新）
- Auto download the latest version of the model when working, no need to manually download (offline mode will not update)

v3.1.13
- 添加详细日志，方便调试
- Add detailed logs for debugging

v3.1.12

- 修复高并发情况下翻译结果错乱的 Bug
- Fix: High concurrency translation result confusion bug