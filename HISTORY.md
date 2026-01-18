## v5.0.0

- 更换翻译引擎，首次使用或升级后可能需要重新准备翻译文件，请耐心等待。
- 下载翻译文件时会自动完成准备，之后可重复使用。
- 离线使用前，请先联网试译需要使用的每个翻译方向；迁移时保留配置和翻译模型两个文件夹。
- 服务端使用 Docker 或命令行安装，暂不提供单文件可执行程序。
- 更新使用说明和界面提示，修正设置说明，移除无效的每语言工作数设置和旧版性能数据。

### English

- Updated the translation engine. First use or an upgrade may require preparing translation files again; please wait for this to finish.
- Downloading translation files now also prepares them for reuse.
- Try each required translation direction online before going offline. Keep both configuration and translation model folders when moving files.
- Use Docker or a command-line installation for the server. Standalone server executables are not currently available.
- Updated guides and interface messages, corrected settings descriptions, and removed the ineffective workers-per-language setting and old performance figures.

## v4.0.31

- 添加中文输出标点全角化功能，默认开启

## v4.0.30

- 默认沉浸式翻译接口输入语言为 auto（因为它经常输入错误的语言代码，只能这样适配它了）
- 改进 auto 的混合语言处理逻辑

## v4.0.29

- UI：新增跟随系统主题功能
- UI：使用 IndexDB 存储历史记录，提升性能

## v4.0.28

- 桌面端：托盘菜单添加开机启动开关
- 桌面端：检测更新功能

## v4.0.27

- 添加 DeepLX 兼容接口
- 增大断句长度，提升了翻译质量，建议升级
- 修复译文标点符号格式错误的问题

## v4.0.20

- 发布到 npm，现在可以通过 `npm i -g mtranserver` 安装或者 `npx mtranserver` 来运行服务器啦！
- 修复语言检测的内存安全问题
- 修复混合语言检测的逻辑问题，现在能够正常翻译混合语言文本为目标语言了
- 新增 `--download` 命令，支持通过命令行批量下载语言对模型 (例如 `mtranserver --download en_zh zh_en`)
- 新增 `--languages` 命令，列出所有可下载的语言对
- UI：新增宽屏模式按钮
- UI：新增多面板并排翻译的功能
- UI：新增记忆语言、主题等功能开关的功能
- UI：新增副标题文档地址按钮
- UI：修复历史记录没有滚动条的问题

## v4.0.13

- 改进 Docker 镜像构建支持，现在任何旧设备都能运行 Docker 版本啦！
- 无论新旧设备，使用 Docker 版本性能更佳！推荐使用 Docker 版本！
- Release 构建的可执行文件暂未跟进该功能，敬请期待！

## v4.0.12

- 改进日志功能 (感谢 @ApliNi)
- 新增 LRU 缓存功能 (感谢 @ApliNi)

## v4.0.11

- 修复认证功能失效的问题
- Fix authentication issue

## v4.0.10

### 中文版本

#### 性能与引擎

- 引擎重构：完成 v4 引擎重构，显著提升运行速度与稳定性。
- 内存优化：内存占用回归至 1GB 以内水平。在 Linux x64 环境下翻译《福尔摩斯探案集》时，btop 显示内存占用低于 600MB。

#### 部署与兼容性

- Docker 修复与支持：修复了 Docker 构建问题，新增标准版（xxnuo/mtranserver:latest）与兼容版（xxnuo/mtranserver:legacy）镜像。
- 多环境支持：新增对旧款 CPU (non-AVX2) 以及 Linux musl 的构建支持。

#### 新功能

- 更新检查器：新增启动时自动检查更新功能。可通过 --check-update 参数或 MT_CHECK_UPDATE 环境变量启用或禁用。

#### 已知问题

- Android 兼容性：当前版本暂时无法在 Android 设备上运行。

---

### English Version

#### Performance & Engine

- Engine Rewrite: The v4 engine has been refactored for significantly faster performance and enhanced stability.
- Memory Efficiency: Memory usage has returned to sub-1GB levels. (Tested on Linux x64 during English-to-Chinese translation of "The Adventures of Sherlock Holmes", btop usage was under 600MB).

#### Deployment & Compatibility

- Docker Improvements: Fixed Docker build issues and added support for both standard (xxnuo/mtranserver:latest) and legacy-compatible (xxnuo/mtranserver:legacy) images.
- Platform Support: Added legacy build support for non-AVX2 CPUs and Linux musl build support.

#### New Features

- Update Checker: Added automatic update checks on startup. This can be toggled via the --check-update flag or the MT_CHECK_UPDATE environment variable.

#### Known Issues

- Android Support: Temporarily unavailable on Android devices.
