## v4.0.12

* 改进日志功能 (感谢 @ApliNi)
* 新增 LRU 缓存功能 (感谢 @ApliNi)

## v4.0.11

* 修复认证功能失效的问题
* Fix authentication issue

## v4.0.10
### 中文版本

#### 性能与引擎
* 引擎重构：完成 v4 引擎重构，显著提升运行速度与稳定性。
* 内存优化：内存占用回归至 1GB 以内水平。在 Linux x64 环境下翻译《福尔摩斯探案集》时，btop 显示内存占用低于 600MB。

#### 部署与兼容性
* Docker 修复与支持：修复了 Docker 构建问题，新增标准版（xxnuo/mtranserver:latest）与兼容版（xxnuo/mtranserver:legacy）镜像。
* 多环境支持：新增对旧款 CPU (non-AVX2) 以及 Linux musl 的构建支持。

#### 新功能
* 更新检查器：新增启动时自动检查更新功能。可通过 --check-update 参数或 MT_CHECK_UPDATE 环境变量启用或禁用。

#### 已知问题
* Android 兼容性：当前版本暂时无法在 Android 设备上运行。
---

### English Version

#### Performance & Engine
* Engine Rewrite: The v4 engine has been refactored for significantly faster performance and enhanced stability.
* Memory Efficiency: Memory usage has returned to sub-1GB levels. (Tested on Linux x64 during English-to-Chinese translation of "The Adventures of Sherlock Holmes", btop usage was under 600MB).

#### Deployment & Compatibility
* Docker Improvements: Fixed Docker build issues and added support for both standard (xxnuo/mtranserver:latest) and legacy-compatible (xxnuo/mtranserver:legacy) images.
* Platform Support: Added legacy build support for non-AVX2 CPUs and Linux musl build support.

#### New Features
* Update Checker: Added automatic update checks on startup. This can be toggled via the --check-update flag or the MT_CHECK_UPDATE environment variable.

#### Known Issues
* Android Support: Temporarily unavailable on Android devices.