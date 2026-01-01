## v4.0.13

* 改进 Docker 镜像构建支持，现在任何旧设备都能运行 Docker 版本啦！
* 无论新旧设备，使用 Docker 版本性能更佳！推荐使用 Docker 版本！
* Release 构建的可执行文件暂未跟进该功能，敬请期待！

## v4.0.12

* 改进日志功能 (感谢 @ApliNi)
* 新增 LRU 缓存功能 (感谢 @ApliNi)

## v4.0.11

* 修复认证功能失效的问题
* Fix authentication issue

## v4.0.10

* 引擎重构：完成 v4 引擎重构，显著提升运行速度与稳定性。
* 内存优化：内存占用回归至 1GB 以内水平。在 Linux x64 环境下翻译《福尔摩斯探案集》时，btop 显示内存占用低于 600MB。
* Docker 修复与支持：修复了 Docker 构建问题，新增标准版（xxnuo/mtranserver:latest）与兼容版（xxnuo/mtranserver:legacy）镜像。
* 多环境支持：新增对旧款 CPU (non-AVX2) 以及 Linux musl 的构建支持。
* 更新检查器：新增启动时自动检查更新功能。可通过 --check-update 参数或 MT_CHECK_UPDATE 环境变量启用或禁用。
* Android 兼容性：当前版本暂时无法在 Android 设备上运行。
