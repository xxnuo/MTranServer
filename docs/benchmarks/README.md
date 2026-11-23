# 性能实验记录

本目录近期 ONNX 实验采用的 39 条模型级语料包含两条触及生成上限的压力输入，真实服务会将它们报错。历史表格描述的是原混合输入集上的模型调用，不能直接解读为 39 条成功翻译的 HTTP 吞吐。后续应分别报告正常完成、失败/截断及完整服务指标。

[阶段分析与正常请求复测](onnx-stages-normal-corpus.md)记录了这一发现、排除 Loop 重复计时的方法以及正常集的自旋复测。旧实验原始数据保留，其输出一致性结论仍只适用于各自记录的样本和配置。

质量允许变化的候选，采用前需独立参考译文评估；模型自身旧输出仅作兼容性对照。开发集用于筛选，保留测试集作最终验收，并同时报告生成失败和关键事实退化。

[跨平台推理与吞吐优化调查（2026-11-15）](inference-throughput-2026-11-15.md)：ONNX合批、连续补位、设备常驻与融合、GenAI、ggml及CTranslate2的实测结论与聚合数据。

[5.4.2 全链路性能优化与候选方案实测报告（2026-11-16）](performance-optimization-2026-11-16.md)：L3 常驻两阶段 INT8 + Cauchy-Schwarz 精确重排候选表、无锁动态量化、启动与服务热路径优化、六平台与四台实体机 2049 条逐 token 一致验收及否决方案记录。

[CPU 性能对比：普通、Fast 与 LinguaSpark（2026-11-16，v5.4.2）](cpu-comparison-2026-11-16.md)：Ryzen 9 9950X（4 CPU 配额、3 GiB 内存）同口径复测与 [2026-11-14（v5.1.3）基线](cpu-comparison-2026-11-14.md)对照，附结构化数据 [cpu-comparison-2026-11-16.json](cpu-comparison-2026-11-16.json)。

[5.4.3 普通与 Fast 双模式性能优化及候选方案报告（2026-11-17）](performance-optimization-2026-11-17.md)：普通模式原生 SSRU 全词表 `FullVocabQ4`、句级张量复用、精确 INT8 候选表快速路径、零 `ort.Tensor` N-API 热路径、六平台与四台实体机 4098 条逐 token 一致验收及否决方案记录。

[CPU 性能对比：普通、Fast 与 LinguaSpark（2026-11-17，v5.4.3）](cpu-comparison-2026-11-17.md)：Ryzen 9 9950X（4 CPU 配额、3 GiB 内存）普通与 Fast 双模式更新对照，附结构化数据 [cpu-comparison-2026-11-17.json](cpu-comparison-2026-11-17.json)。

[普通与 Fast 双模式深度性能优化与全链路 ABBA 实测报告（2026-11-23）](performance-optimization-2026-11-23.md)：Intel Core i5-14400（6P+4E / 16 线程）相对 `v5.4.6`（`bde0f20`）的 Waves 1–4 优化架构（INT8 `SsruTranslate` 图、ragged 原生编码器、`PcorePool` + `TeamBarrier`、混合 E-core 连续批调度与 `FullVocabQ4` 团队并行）、跨平台/WASM/Electron 验收及 3 轮 Core/HTTP ABBA 对照，附结构化数据 [performance-optimization-2026-11-23.json](performance-optimization-2026-11-23.json)。

[CPU 性能对比：普通、Fast 与 LinguaSpark（2026-11-23，v5.4.6）](cpu-comparison-2026-11-23.md)：Ryzen 9 9950X（4 CPU 配额、3 GiB 内存，`packed-avx512vnni` + Linux cgroup CPU 配额感知提交 `299ece2`）同口径三方复测与 5.4.3 / 5.1.3 演进对照，附结构化数据 [cpu-comparison-2026-11-23.json](cpu-comparison-2026-11-23.json)。
