# 构建与分发

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、目标平台

| 阶段 | 平台 | 窗口系统 |
|:---|:---|:---|
| MVP | Ubuntu 22.04 LTS 桌面版 | X11 |
| 当前 | Ubuntu 22.04 LTS + Windows 10+ | X11 / Win32（MinGW Makefiles） |

---

## 二、构建方案

**C++ 渲染引擎（Linux / Ubuntu 22.04）**：

```bash
# 安装构建依赖（Ubuntu 22.04）
sudo apt install build-essential cmake ninja-build \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# 配置（在 renderer/ 子目录下构建）
cmake -S renderer -B renderer/build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build renderer/build -j$(nproc)

# 运行测试（Google Test + CTest）
ctest --test-dir renderer/build --output-on-failure

# 可执行文件输出路径
# renderer/build/bin/desktop-pet-renderer/desktop-pet-renderer
```

**C++ 渲染引擎（Windows / MinGW）**：

```bash
# 前置：MinGW-w64 (GCC 13+) 的 bin 目录必须在 PATH 上
#   （cc1plus.exe 依赖 mingw64\bin 下的 DLL，缺失会导致静默编译失败）

# 使用 MinGW Makefiles 生成器配置
cmake -S renderer -B build/renderer_mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build build/renderer_mingw --config Release -j

# 或直接使用项目统一构建脚本（推荐，自动过滤 Git bundled MinGW 冲突）
python build.py renderer
```

**Vulkan 后端构建（Phase 2.x，可选）**：

```bash
# 前置：安装 Vulkan SDK，CMake 通过 find_package(Vulkan REQUIRED) 查找

# 启用 Vulkan 后端（默认 OFF，使用 OpenGL）
cmake -S renderer -B build/renderer_vulkan -G "MinGW Makefiles" \
    -DUSE_VULKAN=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/renderer_vulkan --config Release -j

# 或通过统一脚本
python build.py renderer    # build.py 默认构建 OpenGL + Vulkan 双后端
```

> **说明**：GLFW 和 GLEW 从 Cubism SDK 内置源码编译，无需安装系统包（`libglfw3-dev`、`libglew-dev`）。WebSocket 通信使用本地连接，未启用 TLS（`USE_TLS=OFF`），无需 `libssl-dev`。
> **音频依赖（Phase 3b，已集成）**：miniaudio（single-header，vendored 于 `renderer/third_party/miniaudio.h`）+ libogg/libvorbis（CMake FetchContent 拉取，1.3.5 / 1.3.7）。渲染器侧音频播放已实现，**已替代原 OpenAL 方案**，无需 `libopenal-dev`。

**Qt 控制面板（controller_qt，Phase 5-9）**：

```bash
# 统一构建脚本（推荐：自动配置 CMAKE_PREFIX_PATH、隔离 MinGW 工具链、运行 windeployqt）
python build.py qt

# 输出
# build/bin/desktop-pet-controller-qt(.exe)

# 运行测试（41 个 QTest 二进制）
ctest --test-dir build/controller_qt
```

> **手动构建注意（Windows）**：`controller_qt/` 必须使用 Qt 自带的 MinGW 13.1.0（`C:\Qt\Tools\mingw1310_64`），与渲染引擎所用 MinGW 相互独立。裸 `cmake` 手动构建时必须自行将其前置到 `PATH` 并过滤 Git 自带的 MinGW（详见 [开发语言与工具链](./toolchain.md)）。
>
> **首次配置**需要网络：CMake FetchContent 拉取 FluentUI 与 spdlog（后续构建复用 `build/_deps/` 缓存）。Windows 下 `build.py qt` 还会额外复制 `Qt5Compat/GraphicalEffects` 与 `Qt6ShaderTools.dll` 到 exe 旁（windeployqt 无法探测这些 QML 内部导入）。

---

## 三、分发形式

**MVP**：仅 C++ 渲染引擎可执行文件。

**当前（Ubuntu + Windows 双平台）**：渲染引擎为独立可执行文件；Qt 控制面板在 Windows 上由 `windeployqt` 将 Qt 运行时（DLL/插件/QML 导入）自动部署到 `build/bin/` 的 exe 旁，**目标机器无需安装 Qt** 即可运行。

| 形式 | Ubuntu | Windows |
|:---|:---|:---|
| 免安装目录 | .tar.gz | .zip（`build/bin/` 打包即用） |
| 安装包 | 待定 | 待定（NSIS / Inno Setup 生成，推迟到后续任务） |

> **Windows 构建说明**：Windows 版使用 MinGW Makefiles（非 MSVC）。打包时需一并分发 `Live2DCubismCore.dll`、`FrameworkShaders/`（Vulkan 后端的 SPIR-V 着色器）、`Resources/`（模型资源）及 MinGW 运行时依赖 DLL。统一构建脚本 `python build.py all` 产物输出到 `build/bin/`。

---

## 四、打包结构

**MVP**：

```plain
desktop-pet/
├── desktop-pet-renderer          # C++ 渲染引擎可执行文件
└── models/                       # 默认模型（可与可执行文件同目录或使用硬编码路径）
    └── Haru/
```

**完整版（Phase 1-3 + controller_qt）**：

```plain
desktop-pet/
├── bin/
│   ├── desktop-pet-controller-qt[.exe]  # Qt 控制面板可执行文件（windeployqt 部署 Qt 运行时）
│   ├── desktop-pet-renderer[.exe]    # C++ 渲染引擎可执行文件（OpenGL 后端）
│   ├── desktop-pet-renderer-vulkan[.exe]  # C++ 渲染引擎可执行文件（Vulkan 后端，编译时 -DUSE_VULKAN=ON 产物）
│   ├── Live2DCubismCore[.dll|.so]    # Cubism Core 动态库（Windows 分发 .dll）
│   ├── FrameworkShaders/             # Vulkan 后端 SPIR-V 着色器（仅 Vulkan 变体）
│   └── Resources/                    # 默认模型（Haru/Hiyori 等 SDK 示例模型）
├── lib/
│   └── *.so / *.dll                  # C++ 运行时动态库（MinGW 运行时依赖，Windows）
├── models/                           # 默认模型（不含音频文件）
│   └── default/
├── audio/                            # 音频文件独立目录（Phase 3，与模型分离存储）
│   └── default/                      # 默认音频文件（OGG，由渲染器 AudioManager 播放）
└── config/
    ├── config.json                   # 默认配置（首次启动复制到 ~/.config/desktop-pet/）
    └── audio_mapping.json            # 音频映射配置（Phase 3b，控制器侧待实现）
```

> **graphics 双后端说明**：渲染引擎通过 `IGraphicsBackend` 抽象接口提供 `OpenGLBackend` 与 `VulkanBackend` 双实现，编译时由 `USE_VULKAN` 开关决定链接哪个后端（无运行时切换）。分发时可选只发布单一后端可执行文件，或同时发布两个变体由用户/启动器选择。Vulkan 后端依赖系统的 Vulkan 驱动（Vulkan 1.2+），需额外分发 `FrameworkShaders/` 下的 SPIR-V 着色器。
