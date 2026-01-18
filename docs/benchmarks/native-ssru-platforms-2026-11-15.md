# Fast 原生 SSRU 六平台适配（5.4.0）

Fast CPU 默认选择原生 SSRU，不再需要 `MT_NATIVE_SSRU=1`；设置 `MT_NATIVE_SSRU=0` 可使用原 Fast。普通模式与 GPU 选择保持原有行为。插件缺失、初始化或运行失败时保留 ONNX 回退，并报告实际 runtime／fallbackReason。

服务启动时预热本地已有 manifest 的方向，不下载全部语言。整批预热完成后监听端口，再开始正常 idle 计时；部分坏模型记录警告，其他方向继续启动。闲置回收后仍会重新加载。桌面退出／重启可以取消并排空正在进行的启动。

## 平台与执行证据

[原生架构测试](https://github.com/xxnuo/MTranServer/actions/runs/43709749338)覆盖以下六目标，每目标完成双向2024条 FLORES-200 devtest、25条短句／段落，与同机原 Fast 的2049条输出逐 token 一致，全部生成 EOS。Node 22 与 Electron 39.2.7 均通过默认 Fast、关闭 SSRU、缺插件回退，以及双向短句／段落／HTML、并发和关闭排空。Linux 两架构各重复三轮全新 Electron 启动。

运行器使用目标原生指令集，包含托管虚拟机；没有用 QEMU／Rosetta 的性能代替原生执行。另在实体 i5-14400、i7-8550U 和 Jetson AGX Thor 上验证。Windows／macOS 的实体设备不包含在这些托管运行器证据中。

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

本地完整测试301项通过、0失败，类型检查及最终 Node／library 构建通过。CI 与本地 Bun 版本不同，生成的 JS helper 存在差异；六平台安装验收针对上述 CI tarball，不将本地 JS 的校验和当作同一制品。尚未发布 npm、部署服务或验收桌面 GUI／GPU 性能。Windows／macOS 实体设备验收仍缺少设备访问，托管运行器结果不替代这一项。
