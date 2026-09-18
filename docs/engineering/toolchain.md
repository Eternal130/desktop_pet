# 开发语言与工具链

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、C++ 渲染引擎

| 项目 | 选型 | 说明 |
|:---|:---|:---|
| 语言标准 | **C++17** | 提供 `std::optional`、`std::filesystem`、结构化绑定、`constexpr if` 等现代特性，各主流编译器均已完整支持 |
| 编译器 | **GCC ≥ 11.4（Linux）/ MinGW-w64 (GCC 13+)（Windows）** | Ubuntu 22.04 LTS 默认版本，C++17 完整支持；Windows 版使用 MinGW-w64（MinGW Makefiles 生成器），**不使用 MSVC**（与 Cubism SDK 预编译库及项目构建脚本 `build.py` 兼容性最佳） |
| 构建系统 | **CMake ≥ 3.22** | Ubuntu 22.04 LTS 默认版本，支持 CMake Presets、`target_sources` GENEX 等现代特性 |
| 构建生成器 | **Ninja（推荐）/ Unix Makefiles（Linux）；MinGW Makefiles（Windows）** | Ninja 增量编译速度显著优于 Make；Windows 下使用 MinGW Makefiles 与 `build.py` 统一 |
| 包管理 | **源码集成（third_party/）+ CMake FetchContent** | Cubism SDK 不支持常规包管理器，为统一管理，大部分第三方库采用源码或预编译库形式集成；音频解码库 libogg/libvorbis 通过 CMake FetchContent 拉取 |

**编译选项**：

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)          # 禁用 GNU 扩展，确保标准兼容
```

CMake 项目采用 target-based 风格。GLFW/GLEW/stb 从 SDK 内置路径引用，Framework 和 Core 分别以源码编译和预编译库形式链接：

```cmake
# renderer/CMakeLists.txt 实际结构（简化）
set(SDK_ROOT_PATH ../third_party/CubismSdkForNative)
set(THIRD_PARTY_PATH ${SDK_ROOT_PATH}/Samples/OpenGL/thirdParty)

# SDK 内置第三方库
add_subdirectory(${THIRD_PARTY_PATH}/glew/build/cmake ...)
add_subdirectory(${THIRD_PARTY_PATH}/glfw ...)

# Cubism Framework + Core
add_subdirectory(${SDK_ROOT_PATH}/Framework ...)
add_library(Live2DCubismCore STATIC IMPORTED)

# IXWebSocket（本地通信，不启用 TLS）
set(USE_TLS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/ixwebsocket ...)

# 渲染引擎可执行文件
add_executable(desktop-pet-renderer)
target_link_libraries(desktop-pet-renderer PRIVATE
    Framework glfw ${OPENGL_LIBRARIES} ixwebsocket::ixwebsocket)

# 单元测试（独立于渲染管线，不链接 GL/GLFW/Framework）
add_executable(renderer-tests tests/ProtocolTest.cpp tests/MessageHandlerTest.cpp ...)
target_link_libraries(renderer-tests PRIVATE GTest::gtest_main)
```

**Vulkan 后端构建（Phase 2.x）**：

渲染引擎支持 OpenGL / Vulkan 双后端，通过编译时开关切换（无运行时切换）。`IGraphicsBackend` 抽象接口提供 6 个方法，`OpenGLBackend` 与 `VulkanBackend` 分别实现，`platform/WindowManager.cpp` 被 GL/Vulkan 共享：

```cmake
# 启用 Vulkan 后端（默认 OFF，使用 OpenGL）
cmake -S renderer -B build/renderer_vulkan -G "MinGW Makefiles" \
    -DUSE_VULKAN=ON -DCMAKE_BUILD_TYPE=Release

# Vulkan 后端需要：
#   1. Vulkan SDK（系统安装，find_package(Vulkan REQUIRED)）
#   2. Framework 渲染器类型宏 CUBISM_RENDERER_TYPE 切换为 Vulkan 目标
# VulkanBackend.cpp 实现 Instance → Device → Swapchain → Render → Present，
# 使用 dynamic rendering (vkCmdBeginRendering)，像素回读通过 vkCmdCopyImageToBuffer。
```

---

## 二、Qt 控制面板（controller_qt，Phase 5-9）

| 项目 | 选型 | 说明 |
|:---|:---|:---|
| 语言标准 | **C++17** | 与渲染引擎一致 |
| UI 框架 | **Qt 6.8 LTS 起步（开发版本 6.10.0）+ QML** | 所需组件：`Core`、`Gui`、`Widgets`、`Network`、`WebSockets`、`Qml`、`Quick`、`QuickControls2`、`Concurrent`、`Charts`、`Test` |
| 构建系统 | **CMake ≥ 3.22 + Ninja** | `qt_add_executable` + `qt_add_qml_module`（QTP0001 NEW 布局 `:/qt/qml/<URI>/`） |
| 编译器 | **MinGW 13.1.0（Qt 自带，Windows）/ GCC / Clang（Linux）** | Windows 下必须使用 Qt 自带的 MinGW（`C:\Qt\Tools\mingw1310_64`），与渲染引擎所用 MinGW 相互独立，不得混用 |
| 包管理 | **CMake FetchContent** | spdlog 1.15.0、FluentUI QML 组件库（静态链接） |

**MinGW 工具链隔离（Windows，关键）**：

`build.py qt`（`_get_qt_env()`）通过以下方式保证隔离：

1. 将 `C:\Qt\Tools\mingw1310_64\bin` **前置**到 `PATH`（Qt 的 MinGW 优先）；
2. **过滤** `PATH` 中的 `\Git\mingw64\bin`（与渲染引擎侧 `_get_renderer_env()` 同理）；
3. 前置 Qt 自带 Ninja（`C:\Qt\Tools\ninja`）。

若手动用裸 `cmake` 构建 `controller_qt`，**必须**自行复刻上述 PATH 过滤——确保 Qt 的 MinGW 被优先找到，否则会链接到错误的 `libstdc++`，导致链接失败或启动即崩溃。

---

## 三、最低系统要求

| 项目 | MVP 要求 | 当前（含 Windows 扩展） |
|:---|:---|:---|
| 操作系统 | Ubuntu 22.04 LTS (x86_64) | Ubuntu 22.04 LTS / Windows 10+ |
| 窗口系统 | X11 | X11 / Win32 |
| GPU | 支持 OpenGL 3.3+ 的显卡及驱动 | 支持 OpenGL 3.3+ 或 Vulkan 1.2+（Vulkan 后端）的显卡及驱动 |
| 内存 | ≥ 512 MB 可用（应用运行时预期占用 100-200 MB） | 同左 |
| 磁盘 | ≥ 200 MB（含 Qt 运行时库 + 默认模型） | 同左 |
| 额外依赖（Vulkan 后端） | — | Vulkan SDK（仅 Vulkan 后端构建时需要） |
