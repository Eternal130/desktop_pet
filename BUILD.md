# 构建指南（CMake Presets）

> 构建入口已全面迁移 CMake（原 `build.py` 已删除，构建路径零 Python）。
> 最低要求 **CMake ≥ 3.25**（workflow presets 所需）；工具链由 toolchain file 绝对路径钉死，PATH 顺序不再影响构建结果。

## 新克隆两条命令

```bash
# 1. 初始化 submodule（含嵌套的 Framework submodule）并获取依赖
git submodule update --init --recursive
cmake -P scripts/Bootstrap.cmake        # Cubism Core + GLEW 2.2.0 + GLFW 3.4（幂等，可重复执行）

# 2. 构建（每组件一次；或用 scripts/build.sh | scripts\build.bat 一键跑两个）
cd controller_qt && cmake --workflow --preset linux-release   # Windows 用 win-release
cd renderer     && cmake --workflow --preset linux-gl-release # Windows 用 win-gl-release
```

`cmake --workflow` = configure → build → test 全链。构建产物输出到 `build/bin/`。

## 环境前提

### 通用

| 依赖 | 说明 |
|:---|:---|
| CMake ≥ 3.25 | Linux: `sudo apt install cmake`（发行版版本过旧时用 [Kitware 官方 apt 源](https://apt.kitware.com/)）；Windows: 官方安装器或 Qt Maintenance Tool |
| git submodule | `third_party/CubismSdkForNative`（固定于 tag `5-r.5-beta.3.1`，Core 二进制不在 repo 内，由 Bootstrap.cmake 下载） |
| Ninja | Linux 必装（`sudo apt install ninja-build`，两组件 Linux preset 均为 Ninja generator）。Windows 无需单独安装：两组件 win preset 均用 Qt 自带 Ninja（toolchain 钉死） |

### Renderer（C++ 渲染引擎）

| 工具 | Windows | Linux |
|:---|:---|:---|
| 编译器 | **Qt 自带 MinGW 13.1.0**（与 controller 同一工具链，P1c 归一；`QT_MINGW_ROOT` 覆盖。configure 期硬校验：编译器不含 `mingw1310_64` 即 `FATAL_ERROR`——Git 自带 MinGW 等异种工具链被直接拒绝） | `sudo apt install build-essential` |
| OpenGL 开发库 | 系统自带 | `sudo apt install libgl-dev` |
| X11 开发库 | — | `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` |
| Vulkan（可选变体） | Vulkan SDK | `sudo apt install libvulkan-dev` |

GLEW 2.2.0 与 GLFW 3.4 由 `scripts/Bootstrap.cmake` 自动下载到 submodule 的 `Samples/OpenGL/thirdParty/` 下，无需手动安装。

### Controller（Qt 控制面板）

| 工具 | Windows | Linux |
|:---|:---|:---|
| Qt 6.10 | 官方安装器，选 `mingw_64` kit | 发行版 Qt 6 包（或 [qt.io](https://www.qt.io/) Linux 安装器） |
| 编译器 | **Qt 自带 MinGW 13.1.0**（`C:/Qt/Tools/mingw1310_64`，`QT_MINGW_ROOT` 覆盖；toolchain 硬校验必须含 `mingw1310_64`——官方 Qt 6.10 mingw_64 二进制仅与其 ABI 兼容） | g++（随 Qt Linux 安装） |
| Ninja | Qt 自带（`C:/Qt/Tools/Ninja`，`QT_NINJA` 覆盖；Qt Maintenance Tool → Additional Libraries → Ninja 安装） | 同通用 Ninja |

## 统一工具链（单 MinGW）与覆盖机制

Windows 上两组件自 **P1c 起共用 Qt 自带 MinGW 13.1.0**（原 renderer 独立的系统 MinGW 工具链已退役，`RENDERER_MINGW_ROOT` 一并退役；见 `docs/refactor/plugin-architecture-and-cmake-migration.md` §C.5）：

| 组件 | 工具链 | toolchain file | 环境变量覆盖（默认值） |
|:---|:---|:---|:---|
| renderer | Qt mingw1310_64 | `controller_qt/cmake/qt-mingw-qt.cmake`（**跨组件单一事实源**，win preset 以相对路径引用） | 同下行 |
| controller | Qt mingw1310_64 | `controller_qt/cmake/qt-mingw-qt.cmake` | `QT_MINGW_ROOT`（`C:/Qt/Tools/mingw1310_64`）、`QT_NINJA`（`C:/Qt/Tools/Ninja/ninja.exe`）、`QT_PREFIX_PATH`（`C:/Qt/6.10.0/mingw_64`） |

- toolchain file 以**绝对路径**钉死 gcc/g++/Qt 自带 ninja，PATH 顺序不再影响结果。
- configure 期**硬校验**：编译器不含 `mingw1310_64` 即 `FATAL_ERROR`（Git 自带 MinGW 等异种工具链在 configure 期被拒绝，而非链接期 DLL 冲突）。
- 两组件 win preset 的 generator 均为 **Ninja**——renderer 自 P1c 起从 MinGW Makefiles 切换（P1c 本身改变编译器，P1a 的字节等价验收不再适用，§C.5 明示；Qt 发行版自带 ninja.exe，toolchain 存在性校验兜底）。
- 个人机器差异首选环境变量；备选 `CMakeUserPresets.json`（已 gitignore，用 `inherits` 覆盖 preset 字段）。
- ⚠ **已知风险（待 CI / Windows 实测验证）**：GLEW 2.2.0 / GLFW 3.4 预编译包与 GCC 13.1 的链接（依赖面纯 C ABI，预期无碍，§C.5 风险点；P1d CI 落地后由双平台矩阵覆盖）。

## Bootstrap 语义（`cmake -P scripts/Bootstrap.cmake`）

- **幂等**：Cubism Core 已填充（`Core/include` + `Core/dll` 非空）或 `glew`/`glfw` 目录已存在 → 跳过并提示；可重复执行。
- **`FORCE=1`**：强制重新下载解压 Core（GLEW/GLFW 不受 FORCE 影响，目录存在即跳过）。
- **`CUBISM_SDK_VERSION` / `CUBISM_SDK_URL`**：覆盖 SDK 版本 / 完整 zip URL（本地镜像）；版本必须与 `.gitmodules` 的 submodule tag 一致。
- **`PET_REPO_ROOT`**：覆盖仓库根（默认从脚本位置推导），便于临时目录测试。
- renderer configure 期守卫：`Core/include/Live2DCubismCore.h` 缺失 → `FATAL_ERROR`，提示上述 submodule + Bootstrap 两步。
- 下载失败自动重试 3 次，仍失败则报错并提示检查网络或配置镜像 URL。

## Preset 速查（新旧命令对照）

| 旧命令（已退役） | 新命令 |
|:---|:---|
| `python build.py qt` | `cd controller_qt && cmake --workflow --preset win-release`（Windows）/ `linux-release`（Linux） |
| `python build.py renderer` | `cd renderer && cmake --workflow --preset win-gl-release` / `linux-gl-release` |
| `python build.py`（交互菜单） | `scripts\build.bat`（Windows）/ `scripts/build.sh`（Linux），顺序跑两组件 workflow |
| `cd renderer/build && ctest` | `cd renderer && ctest --preset linux-gl-release`（Windows: `win-gl-release`） |
| `cd build/controller_qt && ctest` | `cd controller_qt && ctest --preset linux-qt-release`（Windows: `win-qt-release`） |

Renderer preset 明细：

| preset | 平台/后端 | binaryDir | 说明 |
|:---|:---|:---|:---|
| `win-gl-release` | Win / OpenGL | `build/renderer_mingw` | generator = **Ninja**（Qt 自带 ninja，toolchain 钉死）；Qt mingw1310_64（P1c 统一工具链） |
| `win-vk-release` | Win / Vulkan | `build/renderer_vulkan_mingw` | 同上 + `USE_VULKAN=ON` |
| `linux-gl-release` | Linux / OpenGL | `build/renderer_build` | generator = Ninja，系统编译器，无 toolchain |
| `linux-vk-release` | Linux / Vulkan | `build/renderer_vulkan_build` | 同上 + `USE_VULKAN=ON` |

- binaryDir 沿用旧目录名（`PathResolve` 的 build-tree 回退依赖该布局，勿改名）。
- test preset 均带 `outputOnFailure` + `noTestsAction: error`（缺测试即失败）。
- 另有 `test-only` workflow（configure→test，跳过 build，绑定 Linux 侧）；Windows 侧跑测试用 `ctest --preset win-gl-release`。
- Debug 构建（controller 侧）：`win-qt-debug` / `linux-qt-debug`。

## 构建产物

```
build/bin/
├── desktop-pet-renderer(.exe)        # 渲染引擎可执行文件
├── desktop-pet-renderer-vulkan(.exe) # Vulkan 变体（构建 vk preset 后）
├── desktop-pet-controller-qt(.exe)   # Qt 控制面板
├── Live2DCubismCore.dll / .so        # Cubism SDK 运行时（自动复制）
├── FrameworkShaders/                 # 着色器文件（自动复制）
└── Resources/                        # 模型资源（自动复制）
```

（`.exe` 后缀仅 Windows；产物清单含 `desktop-pet-controller-qt-poc(.exe)`，P2 将退役。）

## 开发工具（不在构建路径）

- `controller_qt/tests/ui_automation/run_ui_tests.py` — **Windows-only** 开发侧 UI 回归工具（仓库唯一许可的 Python，不参与构建；命令与产物引用需带 `.exe` 后缀）。
- `scripts/setup-dev-env.ps1` — **可选**装机辅助（会话级 PATH / `CMAKE_PREFIX_PATH` 激活），构建已不依赖它。
