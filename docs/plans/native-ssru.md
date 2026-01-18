# ONNX 原生 SSRU 解码执行器实施计划

目标：在现有 MTranServer 上实现真正的原生完整解码循环，保留 ONNX 编码器、K/V 准备和现有文本处理。优先 Fast qall 模型；普通 ONNX 保留为兼容回退。不是逐 token 调用多个 ORT Session 的包装，也不是仅融合 SSRU 状态更新。

研究目标是显著超过 LinguaSpark。性能与正确性分别验收；不能用换模型、截断、缓存或质量损失冒充加速，不预先承诺倍数。

## 阶段与验收

1. 图与契约：为 Fast 添加可选 com.mtran::SsruDecode v1，替换首步及整个解码 Loop。保持普通模型生成行为；自定义图独立缓存，不覆盖回退图。图测试验证编码器/KV、权重、输入输出与非法配置。
2. 原生核心：实现位置编码、SSRU、cross-attention、FFN、shortlist投影、稳定argmax、EOS和原图长度上限。核心与ORT分离；只读模型与每请求工作区隔离。核心单测覆盖状态、形状、边界及并发。
3. ORT适配：注册自定义CPU算子，复用ORT现有矩阵计算作为首个后端；量化必须与现有qall语义一致。异常不得跨C ABI；核对常量生命周期、线程安全和预打包成本。不使用不稳定内部符号或运行时地址补丁。
4. 产品接入：现有模型准备、原生插件和构建入口接入；缺库、不支持模型/平台及初始化失败回退普通Fast模型。运行失败沿现有生命周期安全恢复。独立可控选择便于A/B，不默认推广未通过收益和质量门槛的候选。
5. 真实模型验证：中英双向真实模型逐token及EOS对照；覆盖短句、长文、混合输入、共享/独立词表、异常候选、关闭/并发恢复。输出变化需独立参考译文评价并报告，不把输出一致当作质量结论。进行相关sanitizer检查。
6. 性能与发布：同机、同CPU预算、关闭缓存、预热、轮换顺序比较普通Fast、原生SSRU和LinguaSpark；报告单句、长文、并发吞吐、P95、首次加载、RSS。微基准不替代HTTP验收。运行项目测试/类型检查/Node和library构建，Astra审阅，修复后bump、显式路径提交并推送。

## 设计边界

- 输入仍为X/eos_id/candidate_ids，输出完整Y。第一步从零embedding开始，后续由上一步token驱动，不能擅自改成BOS token。
- 编码器及每层K/V留在ONNX。解码节点接收动态K/V与常量权重；模型维度、层数、注意力头数及FFN深度必须验证。
- 初始矩阵后端通过ORT NativeOp实现而非重新实现SIMD库。已核对ORT 1.30源码：CreateOp不会预打包常量。保存OrtOp和量化权重不等于保存packed weights；其整句影响需实测，发现瓶颈后再替换后端。
- 原生核心不直接依赖ORT，便于后续跨请求调度。第一阶段不把连续补位、GPU或模型蒸馏混进单句正确性验证。
- 自定义算子只在支持的CPU构建中启用，其余平台保持普通ONNX。不能把一台Linux实测宣传为所有平台收益。

## 当前进展

- [x] 已核对当前图生成器、现有N-API/ORT C接口、已知失败实验。
- [x] 阶段1 图与契约（33项图测试通过）
- [x] 阶段2 原生核心
- [x] 阶段3 ORT适配
- [x] 阶段4 产品接入
- [x] 阶段5 真实模型验证（最终保留集2024条逐token一致）
- [x] 阶段6 性能、审阅、构建与发布准备（随本次功能提交推送）

代理分工：Sol extra high负责编码和测试，Astra medium负责计划与代码审阅，主代理负责契约、集成和最终证据核对。

## 验收细化（Astra 首轮审阅）

- 数值契约：decoder Wf/W、Wq/Wo、FFN使用现有qall逐张量动态INT8；QK、概率乘V、shortlist投影FP32。保留bias[1]=-Infinity；候选顺序和first-tie不改。量化范围不得跨请求共享。
- ABI：输入固定为X/eos/candidates、按层全部K、按层全部V、weight_names排序的FP32常量；属性dim/heads/depth/ffn_depth。K=[1,H,D/H,S]、V=[1,H,S,D/H]；S=X长度。
- 第一个候选通过MT_NATIVE_SSRU=1显式启用；运行时必须报告native-ssru，静默回退不能记为候选性能。回退路径始终加载原始Fast图。
- 首轮量化与核心语义跑通后逐阶段对照，解释每项输出差异。无EOS按现有服务判为生成失败，不提前截断制造收益。
- 独立质量采用dev筛选、冻结后保留集验收；拟定chrF++配对95% CI下界不低于-0.3分，结合严重事实错误与失败率，统计不显著不等于非劣。
- 默认采用须主要HTTP场景稳定净收益至少10%，低负载/竞争负载无超过5%的稳定退化；研究目标为LinguaSpark吞吐2倍，未达则明确未达，不用局部微基准替代。
- 内存单独报告raw、quantized、packed及运行缓冲，不让多份权重驻留被遗漏。并发回退/关闭、异常输入恢复和sanitizer必须检查。

## 前人实现检索

- https://github.com/jerinphilip/slimt ：Marian/Bergamot tiny11 专用推理，包含SSRU与intgemm/ruy/Gemmology后端。实验副本revision 9f0b1a20d14871cc94dbe65b7a3df128e5e81f55。COPYRIGHT声明GPLv2-or-later；本项目不直接复制，研究其权重准备、状态和矩阵后端边界。
- https://github.com/tether-ai-research/qvac-translatepsy-euro-nano ：纯PyTorch的Marian SSRU推理，可作为架构独立参考，不视作已验证的性能替代。
- https://onnxruntime.ai/docs/reference/operators/add-custom-op.html ：受支持的自定义算子、原生算子调用和外部运行时包装入口。
- https://github.com/microsoft/onnxruntime/blob/v1.30.0/onnxruntime/core/session/standalone_op_invoker.cc ：核对CreateOp/InvokeOp实际行为；常量预打包缺失已由源码确认。
- https://github.com/microsoft/onnxruntime/blob/v1.30.0/onnxruntime/core/mlas/inc/mlas.h ：MLAS packed GEMM接口研究入口；当前发行共享库未导出这些内部符号，不采用地址查找补丁。
- https://github.com/google/ruy ：ARM矩阵后端候选，提供预打包缓存；尚未做与当前动态uint8激活量化的等价和性能验收。

## 首轮证据

- 真实中英25条短句/段落：专用原生循环与原Fast图逐token相同；原始输出 `.tmp/native-ssru/real-results.json`。这是小样本正确性，不是独立质量验收。
- addon及图相关38项测试通过。
- 初版InvokeOp线性调用（含张量包装）占解码时间中位数约85.9%；首轮比Fast慢，未通过加速门槛。开始验证可预打包Gemmology后端，保持现有动态量化语义。

- 开发集独立初筛100条/方向：英→中96/100、中→英95/100 token一致；均正常EOS。chrF++差值英→中-0.070（95% CI [-0.275,+0.087]），中→英+0.203（[-0.001,+0.582]），仅开发集，不代表最终质量批准。正在扩大到完整997条并定位数值差异。
- 64路共享会话、非法候选恢复、关闭排空通过；完整相关38项测试及类型检查通过。

## 第二轮证据

- 完整FLORES dev各997条：英→中957、中→英967条token一致；全部EOS。独立chrF++差值分别-0.026（95% CI [-0.065,+0.008]）及+0.004（[-0.038,+0.050]）。保留测试集未使用，严重事实错误审阅与数值定位尚待完成。
- Gemmology预打包AVXVNNI实验通过349440项整数参考、ASan/UBSan；原始AVX2在全uint8范围有饱和错误，禁止直接作为等价回退。完整1994条packed与InvokeOp原生执行器token相同。
- 三输入/方向的小型预热三轮组件对照，Fast/InvokeOp/packed耗时中位数英→中5.378/8.332/4.994ms、中→英5.883/8.796/4.691ms。只作接入筛选，不是HTTP收益或LinguaSpark目标验收。
- core独立scalar stage reference、EOS/ties、非法输入/异常恢复、分配/并发单测通过；ASan/UBSan通过。异构真实请求各96路并发/关闭通过。当前全套159项测试、类型检查、library构建通过；其他代理新增测试落盘后需重跑。
- 开始将packed后端按独立指令集编译单元与运行时能力检测接入，保持opt-in，低指令集不执行AVXVNNI。

## 质量门槛未通过

开发集逐条检查发现zh-Hans_en第502条：原文“往往就有不好的表现”，原Fast为“bad performance”，候选变为“often do well”。这是新增否定语义错误，不能由平均chrF++接近掩盖。当前候选不批准采用，也不消费保留测试集。开始在隔离副本验证ORT sigmoid/归一化数值对齐，若计算更改需要重新冻结、重新开发集验证及HTTP验收。

## 集成审阅补充

Astra发现运行时切换后缓存可能污染，以及Docker单独编译旧addon会覆盖完整SSRU插件。修复缓存按调用前后runtime身份决定是否写入；Docker改为复用生产native构建入口并检查ssruVersion。补充malformed output的rank/长度/token范围校验与恢复测试。数学不变，修复后需重新测试构建和最终镜像验证。

## 数值对齐继续验证

ORT sigmoid+归一化callbacks试验修复了zh502，但完整dev仍有少量新差异，其中en64将“法案由Roberto持有”变成“由Roberto起草”，仍不满足严重事实错误门槛。扩展Sin与attention算子对齐后差异并非单调减少，正在固定输入逐阶段定位；不能按平均分或exact比例选择掩盖错误。所有对齐改动仍在隔离副本，生产core暂未采用，保留测试集未消费。

原候选长文HTTP（三轮、32句）观察到相对Fast吞吐约15%–34%提高、相对LinguaSpark约1.8–4.3倍，属于未通过质量门槛的研究数据，不能作为可发布成果。旧urllib客户端瓶颈的测试已排除，后续统一Node长连接客户端。

## 精确数值对齐实验通过开发集

实际优化图证实QK MatMul+Mul被融合为com.microsoft::FusedMatMul(alpha)，独立MatMul后Mul的舍入路径不同。结合ORT Sigmoid、InstanceNormalization、Sin、Attention数学调用及预打包INT8后端，fused实验双向997+997条全部逐token一致、全部EOS，修复了zh502否定反转及en64作者错误。代码仍在隔离副本，正在集成正式core/adapter；集成后需要重新冻结与保留集/性能验收。不能沿用旧非精确版本性能结论。

## 当前正式候选（5.3.0）

此前段落为实验历史，不是当前状态。精确数学回调已集成正式core/ORT adapter；独立dev1994条、正式版保留集2024条均逐token一致且全部EOS。保留集独立chrF++/BLEU双向与原Fast完全相同，范围限定本机ORT1.30与当前中英模型，不推广为任意平台数值等价。

完整测试237项通过，类型检查与Node构建通过，Astra最新数学接口审阅无新增P1/P2。版本5.3.0，MT_NATIVE_SSRU仍默认关闭。最终精确版短句及长文HTTP各三轮已完成；Node／library构建、tarball解包导入及真实翻译、Docker镜像内真实HTTP均通过。持久化结果见 `docs/benchmarks/native-ssru-2026-11-15.{md,json}`。本功能随5.3.0提交推送，不包含npm发布或部署。

短句8并发吞吐比Fast高29%–35%、比LinguaSpark高13%–30%；长文8并发比Fast高30%–34%，为LinguaSpark的1.90–2.00倍，未实现所有场景翻倍。长文原生峰值RSS约1380 MiB，高于Fast约851 MiB；内存分项归属及其他平台真机性能未验收，因此保持opt-in。首次请求仍需数秒，未解决初始化和探测成本。历史被否决的标量版本及未填满并发的长文结果不计入最终收益。
