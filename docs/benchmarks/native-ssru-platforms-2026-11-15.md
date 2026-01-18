# Fast 原生 SSRU 六平台适配（5.4.0／5.4.1）

Fast CPU 默认选择原生 SSRU，不再需要 `MT_NATIVE_SSRU=1`；设置 `MT_NATIVE_SSRU=0` 可使用原 Fast。普通模式与 GPU 选择保持原有行为。插件缺失、初始化或运行失败时保留 ONNX 回退，并报告实际 runtime／fallbackReason。

服务启动时预热本地已有 manifest 的方向，不下载全部语言。整批预热完成后监听端口，再开始正常 idle 计时；部分坏模型记录警告，其他方向继续启动。闲置回收后仍会重新加载。桌面退出／重启可以取消并排空正在进行的启动。

## 5.4.1：实体 M4 数值修复与最终发行包验收

5.4.0 的实体 M4 补测发现29条差异，5.4.1已修复。首个确认的分叉位于第二层 query：输入完全一致，但 KleidiAI 先计算 `255/range` 再取倒数，普通 ORT DQL 直接计算 `range/255`。加上 signed activation 的舍入及分离 bias 顺序，细微误差会沿解码放大。原图的 gate/state 和首步常量折叠 query 仍需保留各自原有计算顺序，不能统一启用 FMA 或关闭 KleidiAI。

修复在 Apple ARM SME/SME2 候选设备上，于模型初始化时用固定小图探测当前 ORT 的实际数值行为；尊重禁用 KleidiAI／prepacking 配置，未知结果或探针失败时明确拒绝原生初始化并由产品回退。不按 CPU 型号硬编码，不改 Fast 基准，不逐 token 探测。

[5.4.1 最终 CI](https://github.com/xxnuo/MTranServer/actions/runs/43717420248)六平台全部通过，包含每平台2049条逐token对照、Node／Electron真实HTTP、Linux重复冷启动，以及禁安装脚本安装实际tarball后的三场景验证，无失败重跑。实体 Apple M4 使用同一 CI 发行插件，1025条英→中和1024条中→英全部一致；最终tarball安装后的默认Fast／关闭SSRU／缺插件回退HTTP亦全部通过，插件哈希与完整回归一致。

实体 M4 三轮8并发核心推理中位吞吐：英→中286.76→342.39 req/s（+19.4%），中→英358.10→449.98 req/s（+25.7%）。沿用下述25条混合输入方法，属于核心推理＋N-API，不能当作HTTP吞吐或所有输入的收益。

本地302项测试、类型检查、Node／携带六插件的library构建和packed ASan／UBSan通过。最终包23,590,705 bytes，SHA-256 `4febaa52a5b597b77fcccc34cabcbc5f520ae97943c5ae2580cc66abb68ada8e`。新原始报告在同名JSON的 `fix_5_4_1` 字段；以下表格与失败记录保留为5.4.0历史证据。

2026-11-16用户确认原生架构托管运行器满足本次验收要求，明确标注虚拟机即可。因此六平台适配、启动预热及运行验证已完成。运行器含虚拟机，额外实体设备为Linux三台及Apple M4；未测Windows与Intel Mac实体设备，不将托管运行结果称为这些实体机结果。没有发布npm或部署现有服务。

## 5.4.0 历史平台与执行证据

[原生架构测试](https://github.com/xxnuo/MTranServer/actions/runs/43709749338)覆盖以下六目标，每目标完成双向2024条 FLORES-200 devtest、25条短句／段落，与同机原 Fast 的2049条输出逐 token 一致，全部生成 EOS。Node 22 与 Electron 39.2.7 均通过默认 Fast、关闭 SSRU、缺插件回退，以及双向短句／段落／HTML、并发和关闭排空。Linux 两架构各重复三轮全新 Electron 启动。

运行器使用目标原生指令集，包含托管虚拟机；没有用 QEMU／Rosetta 的性能代替原生执行。另在实体 i5-14400、i7-8550U 和 Jetson AGX Thor 上验证。Windows／macOS 的实体设备不包含在这些托管运行器证据中。

追加实体 Apple M4 Mac mini 验证发现 **29/2049 条 token 不一致**（英→中17条、中→英12条；25条混合小样本均通过）。Node 26.13.0 与官方 Node 22.23.5 重复结果相同，使用的插件与最终 CI 包一致。该机器尚未验收通过，不能用托管 M1 的结果覆盖；正在核对 M4 的 ORT/KleidiAI 数值路径，尚未完成这些差异的独立质量评估。

| 目标 | 本轮运行器 CPU | 实际首选矩阵后端 | 英→中8并发吞吐较 Fast | 中→英8并发吞吐较 Fast |
| --- | --- | --- | ---: | ---: |
| Linux x64 | AMD EPYC 7763 | AVX2，ORT 饱和兼容 | +22.5% | +25.6% |
| Linux ARM64 | 托管 ARM64 | SDOT | +35.9% | +42.2% |
| Windows x64 | AMD EPYC 7763 | AVX2，ORT 饱和兼容 | +10.4% | +17.9% |
| Windows ARM64 | Cobalt 100 | SDOT | +4.1% | +21.7% |
| macOS x64 | Intel i7-8700B | AVX2，ORT 饱和兼容 | +56.4% | +27.6% |
| macOS ARM64 | Apple M1（虚拟机） | SDOT | +13.5% | +4.3% |

这些是核心推理＋N-API 对照，**不是 HTTP 服务吞吐**：相同25条混合输入，每场景重复三次，三轮交替 Fast／候选，单次 ORT 运算1线程、UV线程池8；表中为各轮吞吐的中位数。独立输入仅25条，重复请求不是新增样本。各轮延迟、P95、RSS、模型／插件／源码校验和见[同名 JSON](native-ssru-platforms-2026-11-15.json)。短窗口及托管宿主负载限制了外推，不承诺所有 CPU 或场景都有固定倍数收益。

FLORES devtest 在本轮用于 Linux ARM 数值问题定位，因此这里将其作为跨平台回归集，不能称作本轮未接触的盲测集。逐 token 一致证明这些输入相对同机 Fast 的兼容性，不证明任意输入逐位一致，也不代表翻译质量提高。

## 必须保留的数值差异

- AVX-VNNI 保留原 Gemmology 内核；AVX2 在没有可用 VNNI 时匹配 ORT 的相邻乘积 INT16 饱和行为。支持 VNNI 的 CPU 即使编译器只能提供 AVX2，也采用精确累加，不能统一套用饱和路径。
- ARM64 提供基础 NEON 与按 CPU／OS 能力检测的 SDOT，不把可选指令泄漏到通用入口。
- Linux ARM 的 ORT 优化图将量化线性层 bias 融合为 FMA。原生实现匹配这一路径，但第一步第一层已常量折叠的 gate／query 保留分步舍入。简单地把所有乘加都融合，同样不兼容。
- Sin、Sigmoid、归一化、Attention 保留 ORT 数学实现；QK 使用与优化图一致的 FusedMatMul 缩放。

早期精确 AVX2 累加在旧 CPU 上造成3/25输出差异，Linux ARM 分步 bias 造成完整回归集41条差异，均未作为验收结果。修复分别通过固定张量、逐矩阵对照、回滚重现及整集回归。

## 分发与边界

Linux 发行插件在固定 digest 的 manylinux_2_28 原生容器构建，glibc 最高要求2.27，C++运行库要求不高于 ORT 1.30 自身。静态 C++运行库版本在托管 Linux Electron 中出现卡住／崩溃；改用兼容基线的共享运行库后，双架构重复冷启动通过。没有取得最初故障的原生栈，不进一步断言其内部根因。

上游 ORT 1.30.0 npm 实际缺少 Intel macOS 制品。本项目固定官方 `v1.30.0 / f2c39fe2f838cf35ce7da92824f5a5e3ee6e88a7`，在 Intel macOS 构建同版本 binding 与共享库，记录 SHA-256、许可证及最低 macOS 13。完整包携带该制品；安装脚本可选，启动 bootstrap 同步恢复已校验的随包文件，再加载 ORT，避免依赖包管理器是否允许生命周期脚本。不会在用户启动时下载或编译。

发布构建要求六平台插件、完整回归报告、Node／Electron HTTP 报告及一致的源码哈希；缺件会在发布前失败。Node／library 构建通过，Linux 两架构最终 Docker 镜像完成预热和双向真实 HTTP。镜像验证仅通过临时 Dockerfile 将 apt 下载切换为 HTTPS 镜像，保留 TLS 检查，正式 Dockerfile 保留官方源。

最终安装包（23,573,360 bytes，SHA-256 `2aa64a5a97630630e833926b6ffb835855c6fd8261a24e06e99dd53aa68cfd39`）通过[六平台实际安装验收](https://github.com/xxnuo/MTranServer/actions/runs/43711797091)：`npm install --ignore-scripts` 后运行默认 Fast、关闭 SSRU 和缺插件回退三场景，包含 Intel macOS 的启动恢复。Windows x64 首次在就绪 fetch 失败，原报告未保留 cause 与服务日志，无法确定根因；相同代码与断言的单项重跑通过。随后补充错误链、失败日志及清理前退出状态记录，没有增加重试或放宽断言。

本地完整测试301项通过、0失败，类型检查及最终 Node／library 构建通过。CI 与本地 Bun 版本不同，生成的 JS helper 存在差异；六平台安装验收针对上述 CI tarball，不将本地 JS 的校验和当作同一制品。尚未发布 npm、部署服务或验收桌面 GUI／GPU 性能。Windows 与 Intel macOS 实体设备访问尚未获得；实体 M4 已可访问，但存在上述数值差异，验收尚未通过。托管运行器结果不替代这些实体机项目。
