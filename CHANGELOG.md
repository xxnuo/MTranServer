## v5.1.0

- 根据真实推理耗时自动选择可用的 CPU/GPU 后端和 CPU 线程配置，不再固定使用单线程。
- 启用完整图优化、内存复用和线程自旋，实测执行模式、负载均衡及有上限的 CPU 并发，保留更快的配置。
- 同一进程内复用模型的调优结果；加速后端运行失败时自动重试 CPU，并等待已接收的请求完成后释放引擎。
- 恢复历史性能对比，补充关闭缓存的 ONNX 实测结果、固定语料和复测脚本。

### English

- Automatically select available CPU/GPU backends and CPU thread counts using real inference measurements instead of a fixed single thread.
- Enable full graph optimization, memory reuse and thread spinning; measure execution modes, load balancing and bounded CPU concurrency to retain faster settings.
- Reuse model tuning within the process, retry failed accelerators on CPU, and drain accepted requests before releasing an engine.
- Restore historical comparisons and include uncached ONNX measurements, fixed corpora and benchmark scripts.

## v5.0.0

- 更换翻译引擎，首次使用或升级后可能需要重新准备翻译文件，请耐心等待。
- 下载翻译文件时会自动完成准备，之后可重复使用。
- 离线使用前，请先联网试译需要使用的每个翻译方向；迁移时保留配置和翻译模型两个文件夹。
- 服务端使用 Docker 或命令行安装，暂不提供单文件可执行程序。
- 更新使用说明和界面提示，修正设置说明，移除无效的每语言工作数设置和旧版性能数据。

### English

- Updated the translation engine. First use or an upgrade may require preparing translation files again; please wait for this to finish.
- Downloading translation files now also prepares them for reuse.
- Try each required translation direction online before going offline. Keep both configuration and translation model folders when moving files.
- Use Docker or a command-line installation for the server. Standalone server executables are not currently available.
- Updated guides and interface messages, corrected settings descriptions, and removed the ineffective workers-per-language setting and old performance figures.
