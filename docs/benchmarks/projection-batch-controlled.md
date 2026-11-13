# 投影组批交替复测

结论：组批值得进入解码器原型验证，但还不能作为默认优化发布。原生 C++ 对照也出现收益，说明改善不只是减少 Node 调用。

## 方法与正确性

- 使用现有 ORT 1.30.0 CPU、单线程、固定同一物理核心，两个方向的原有 INT4 模型。没有替换运行库或改变权重量化。
- 从 39 条真实翻译抓取的 1,146 个 hidden 向量，在 batch 1/2/4/8 下比较加偏置后的有序 Top9；全部一致。末批补齐的重复向量不计入验证数量。
- logits 并非逐位相同；此前 128 向量初筛最大差异约 1.3e-5。本轮扩大的是 Top9 检查，未重新计算全量 logits 的最大误差。候选一致只适用于这些向量，未证明新解码轨迹或边界输入等价。
- Node 对照每配置预热 1,024 行，随后做八轮正反顺序交替，每段固定 1,024 行。多个会话常驻，逐一运行，不同时推理。
- 独立 C++ 对照直接调用 ORT Run，预分配输入输出；batch 1/2/8 四轮正反顺序交替，每段两秒，单独创建会话。每段计时前检查 128 行对应 batch 的参考 logits 逐位一致；此项是验证测试器，不是声称跨 batch 逐位一致。
- 硬件计数器启用；可用计数记录 enabled/running 以反映复用，原始数据保留在 JSON。桌面有其它负载，固定核心不等于独占机器。

## 原生投影结果

每行成本为四段中位数；加速比为每轮 batch1 与对应 batch 的每行成本之比。仅代表独立投影吞吐。

| 方向 | batch | 每行耗时 µs | 配对加速比中位数 | 配对范围 |
|---|---:|---:|---:|---:|
| en_zh-Hans | 1 | 436.6 | 1.00x | 1.00–1.00x |
| en_zh-Hans | 2 | 203.5 | 2.12x | 1.94–2.51x |
| en_zh-Hans | 8 | 199.2 | 2.14x | 2.02–2.40x |
| zh-Hans_en | 1 | 1966.1 | 1.00x | 1.00–1.00x |
| zh-Hans_en | 2 | 882.8 | 1.90x | 1.63–2.55x |
| zh-Hans_en | 8 | 514.5 | 3.81x | 2.89–6.93x |

## 接入门槛与其它方向

- 当前模型把完整自回归解码放在 ONNX Loop 内，native addon 一次 Run 返回整句。不能只把 HTTP 请求放进数组就得到上述收益；需要分离编码、单步解码及投影，或设计真正的批量图。
- 首个原型优先 batch=2，比较现有完整 Loop 与分步调度的净损益。记录单请求延迟、并发吞吐、p95、等待时间、内存，并覆盖不同长度、EOS 提前退出、取消和失败清理。暂未实现这些改动。
- 本轮未继续手工融合 INT8 偏置：实际优化图中的 DynamicQuantizeMatMul / MatMulIntegerToFloat 已带 bias 输入；ORT 现有融合已处理这一点。与之前测试的 INT4 投影偏置融合不同。
- 生产代码、模型格式及版本未变，不需要重建发布包。没有将微基准数字当作整句或服务加速。

## 证据

- 汇总与逐段原始数据：`projection-batch-controlled.json`。
- 本地实验：`.tmp/projection-batch/controlled.mjs`、`references.mjs`、`native.cc`、`native-compare.py`；真实向量位于 `.tmp/projection-kernel/`。脚本哈希记入 JSON；这些临时模型与脚本未上传，记录不是完整可移植复现包。
- ORT 参考源码：https://github.com/microsoft/onnxruntime/blob/v1.30.0/onnxruntime/contrib_ops/cpu/quantization/dynamic_quantize_matmul.cc
