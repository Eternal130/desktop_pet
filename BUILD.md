# 构建指南

## 快速开始

```bash
# 交互式选择构建目标
python build.py

# 命令行指定构建目标
python build.py renderer               # 仅构建渲染引擎
python build.py controller             # 仅构建控制面板
python build.py renderer controller    # 构建两者
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

### Controller（Java 控制面板）

| 工具 | 说明 |
|:---|:---|
| JDK 21 | 设置 `JAVA_HOME` 环境变量指向 JDK 安装目录，并将 `$JAVA_HOME/bin` 加入 PATH |
| Maven | Windows 下优先使用项目自带的 `controller/mvnw.cmd`；Linux/macOS 需安装 Maven 并加入 PATH |

**环境变量**：

```bash
# Linux / macOS
export JAVA_HOME=/path/to/jdk-21
export PATH=$JAVA_HOME/bin:$PATH

# Windows (系统环境变量)
JAVA_HOME=C:\path\to\jdk-21
# 将 %JAVA_HOME%\bin 添加到 PATH
```

## 构建产物

```
build/bin/
├── desktop-pet-renderer.exe          # 渲染引擎可执行文件
├── desktop-pet-controller.jar        # 控制面板 fat jar
├── Live2DCubismCore.dll              # Cubism SDK 运行时（自动复制）
├── FrameworkShaders/                 # 着色器文件（自动复制）
└── Resources/                        # 模型资源（自动复制）
```
