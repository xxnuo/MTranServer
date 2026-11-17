## v5.4.4

- 默认 Docker 镜像改为纯 CPU，不再安装 Vulkan/Mesa。需要 WebGPU 时使用 `xxnuo/mtranserver:webgpu`；Intel、AMD、NVIDIA 的 Compose 已改用该镜像。
- Docker 镜像和默认 Compose 默认开启 Fast 模式，设置 `MT_FAST=false` 可关闭。
- Docker 构建可用 `DEBIAN_MIRROR` 更换 Debian 软件源，默认使用中科大镜像。
- 桌面打包前先安装 Electron 运行时，避免包管理器跳过 postinstall 后缺少运行时。
- 发布包和 npm 包的版本跟随当前 tag，不再沿用仓库中更旧的 tag。
- README 的 CPU 性能数据更新为 2026-11-17 的对比，同类项目中加入 LinguaSpark。

### English

- The default Docker image is now CPU-only and no longer installs Vulkan/Mesa. Use `xxnuo/mtranserver:webgpu` for WebGPU; the Intel, AMD and NVIDIA Compose files now use that image.
- The Docker image and default Compose file enable Fast mode by default. Set `MT_FAST=false` to turn it off.
- Docker builds can change the Debian package source with `DEBIAN_MIRROR`. The default is the USTC mirror.
- Desktop packaging installs the Electron runtime first, so a skipped package-manager postinstall does not leave the runtime missing.
- Release and npm package versions follow the current tag instead of an older tag in the repository.
- README CPU figures now use the 2026-11-17 comparison, and LinguaSpark is listed with similar projects.

更早的更新见 [HISTORY.md](HISTORY.md)。
