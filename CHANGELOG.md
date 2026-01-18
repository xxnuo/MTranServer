## v5.1.1

- 修复自动调优因探测输入达到生成上限而误报“没有可用 ONNX 后端”的问题；使用真实输入片段测速，实际翻译仍严格检查输出是否完整。
- Docker 镜像补充 CA 证书和 Vulkan/Mesa 依赖，支持容器内 HTTPS 证书校验及 WebGPU 探测。
- 提供默认纯 CPU、Intel、AMD 和 NVIDIA 四份 Compose 配置；默认跳过 GPU 探测，GPU 配置继续按实测性能选择后端并保留 CPU 回退。
- Intel/AMD 配置映射 `/dev/dri`；NVIDIA 配置通过 NVIDIA Container Toolkit 使用宿主驱动，以 WebGPU/Vulkan 推理，不包含 CUDA/TensorRT 依赖。

### English

- Fix automatic tuning incorrectly reporting no usable ONNX backend when a timing probe reaches the generation limit. Benchmark real input fragments while still requiring complete output for actual translations.
- Include CA certificates and Vulkan/Mesa dependencies in Docker images for HTTPS certificate verification and WebGPU discovery.
- Provide separate CPU-only, Intel, AMD and NVIDIA Compose configurations. The default skips GPU discovery; GPU configurations retain measured backend selection and CPU fallback.
- Map `/dev/dri` for Intel/AMD. The NVIDIA configuration uses host drivers through NVIDIA Container Toolkit for WebGPU/Vulkan inference; CUDA/TensorRT dependencies are not included.

更早的更新见 [HISTORY.md](HISTORY.md)。
