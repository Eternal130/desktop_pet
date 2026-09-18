# 构建指南

## 快速开始

```bash
# 交互式选择构建目标
python build.py

# 命令行指定构建目标
python build.py renderer               # 仅构建渲染引擎
python build.py qt                     # 仅构建 Qt 控制面板
python build.py renderer qt            # 构建两者
python build.py all                    # 全部构建
```

构建产物输出到 `build/bin/`。

## 环境要求

### Python

构建脚本需要 Python 3.6+（仅使用标准库，无需安装额外依赖）。

### Renderer（C++ 渲染引擎）

| 工具 | Windows | Linux |
|:---|:---|:---|
| CMake | ≥ 3.16，加入 PATH | `sudo apt install cmake` |
| C++ 编译器 | MinGW-w64（`mingw32-make` 加入 PATH） | `sudo apt install build-essential` |
| Ninja（可选） | — | `sudo apt install ninja-build`（自动优先使用） |
| OpenGL 开发库 | 系统自带 | `sudo apt install libgl-dev` |
| X11 开发库 | — | `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` |

**Windows 注意事项**：

- 确保 MinGW-w64 的 `bin/` 目录在 PATH 中，且位于 Git 自带的 `Git\mingw64\bin` **之前**，
  否则可能出现 `libwinpthread` DLL 冲突。构建脚本会自动从子进程 PATH 中移除 Git 的 MinGW 路径。
- GLEW 和 GLFW 由构建脚本自动下载，无需手动安装。

## 首次构建前：获取 Cubism SDK 依赖

`third_party/CubismSdkForNative` 是 git submodule（[Live2D/CubismNativeSamples](https://github.com/Live2D/CubismNativeSamples)，固定于 tag `5-r.5-beta.3.1`，含嵌套的 Framework submodule）。submodule 仓库内只包含 Core 的文档，**不包含** Core 预编译二进制（`Core/dll`、`Core/lib`、`Core/include`），首次克隆后需执行以下两步：

```bash
# 1. 初始化 submodule（含嵌套的 Framework submodule）
git submodule update --init --recursive

# 2. 下载 Cubism Core 预编译库（官方 zip，幂等；支持 CUBISM_SDK_URL / CUBISM_SDK_VERSION 覆盖）
scripts/fetch_cubism_core.sh        # Linux / macOS
scripts\fetch_cubism_core.bat      # Windows
```

完成后即可正常构建。GLEW / GLFW 仍由 `build.py` 自动下载到 submodule 的 `Samples/OpenGL/thirdParty/`，无需手动操作；`build.py` 在构建渲染引擎前会检查 submodule 与 Core 是否就绪，缺失时给出上述命令提示。

## 构建产物

```
build/bin/
├── desktop-pet-renderer.exe          # 渲染引擎可执行文件
├── Live2DCubismCore.dll              # Cubism SDK 运行时（自动复制）
├── FrameworkShaders/                 # 着色器文件（自动复制）
└── Resources/                        # 模型资源（自动复制）
```
