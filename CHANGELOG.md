## v5.5.0

- 全栈优化 ONNX 普通模式（`--no-fast`）与 Fast 模式（`--fast`）：引入紧凑 INT8 `SsruTranslate` 融合图（`-ssru-v3.onnx`）、ragged C++ 原生编码器与连续批解码器，同时保持标准 ONNX（含 WASM）与跨平台兼容。
- 新增 `N x K4` AVX-VNNI / AVX-512 VNNI 微内核、`PcorePool` + `TeamBarrier` 持久化团队并行与混合连续批调度，消除双重模型加载并显著降低内存占用（热态约 `442–506 MiB`）与热启动耗时（约 `0.78–0.98 s`）。
- 新增 Linux cgroup v2 `cpu.max` 与 cgroup v1 CPU 配额感知，防止受限容器内过度建线程与单核绑核竞争。
- 更新全部语言 README、i5-14400 ABBA 实测报告与 Ryzen 9 9950X（4 CPU 配额）同口径对比 LinguaSpark 报告。

### English

- Full-stack performance optimization for both ONNX Normal (`--no-fast`) and Fast (`--fast`) modes: introduces the compact INT8 `SsruTranslate` fused graph (`-ssru-v3.onnx`), a ragged C++ native encoder, and continuous batch decoding while preserving standard ONNX (including WASM) and cross-platform compatibility.
- Adds `N x K4` AVX-VNNI / AVX-512 VNNI microkernels, persistent `PcorePool` + `TeamBarrier` team parallelism, and hybrid continuous batch scheduling; eliminates duplicate model loading and substantially reduces memory footprint (~`442–506 MiB` warm RSS) and warm startup latency (~`0.78–0.98 s`).
- Adds Linux cgroup v2 `cpu.max` and cgroup v1 CPU quota awareness to prevent thread oversubscription and per-core pinning contention in CPU-limited containers.
- Updates all localized README benchmark tables, the i5-14400 ABBA optimization report, and the Ryzen 9 9950X (4 CPU quota) comparison against LinguaSpark.

更早的更新见 [HISTORY.md](HISTORY.md)。
