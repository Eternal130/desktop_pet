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

## 二、Java 控制面板「Phase 2」

| 项目 | 选型 | 说明 |
|:---|:---|:---|
| 语言版本 | **Java 21 LTS (OpenJDK 21)** | 当前最新长期支持版本，提供 Records、Sealed Classes、Pattern Matching、Virtual Threads 等现代特性 |
| 构建系统 | **Maven ≥ 3.9** | 成熟稳定，依赖管理和插件生态完善 |
| UI 框架 | **JavaFX 21 (OpenJFX 21.0.5)** | 确定选型，弃用 Swing（决策理由见下方）。选择与 JDK 21 对齐的 LTS 版本（JavaFX 25 需要 JDK 23+，不兼容 JDK 21） |
| 打包工具 | **jlink + jpackage** | Java 21 内置，生成包含最小化 JRE 的平台原生安装包，用户无需预装 Java |
| 字节码版本 | **21** | `maven-compiler-plugin` source/target 均设为 21 |

**UI 框架选型决策（JavaFX vs Swing）**：

| 维度 | JavaFX 21 | Swing |
|:---|:---|:---|
| 维护状态 | OpenJFX 社区活跃，持续发布新版 | 仅安全修复，无新特性开发 |
| 样式系统 | CSS 样式表，主题切换便捷 | Look & Feel 机制，深度定制成本高 |
| 布局方式 | FXML 声明式布局 + Scene Builder 可视化设计 | 纯代码布局 |
| 动画支持 | 内置 Animation API（Timeline、Transition） | 需手动实现 Timer + 重绘 |
| 高 DPI | 原生支持，自动缩放 | 部分场景文字/图标模糊 |
| 分发集成 | 与 jpackage 深度集成 | 同等支持 |

**结论**：JavaFX 在样式定制、声明式 UI 和动画能力上明显优于 Swing，适合桌面宠物这类强交互、重视视觉体验的应用。

Maven 配置要点：

```xml
<properties>
    <java.version>21</java.version>
    <javafx.version>21.0.5</javafx.version>
    <maven.compiler.source>21</maven.compiler.source>
    <maven.compiler.target>21</maven.compiler.target>
    <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>
</properties>
```

---

## 三、最低系统要求

| 项目 | MVP 要求 | 当前（含 Windows 扩展） |
|:---|:---|:---|
| 操作系统 | Ubuntu 22.04 LTS (x86_64) | Ubuntu 22.04 LTS / Windows 10+ |
| 窗口系统 | X11 | X11 / Win32 |
| GPU | 支持 OpenGL 3.3+ 的显卡及驱动 | 支持 OpenGL 3.3+ 或 Vulkan 1.2+（Vulkan 后端）的显卡及驱动 |
| 内存 | ≥ 512 MB 可用（应用运行时预期占用 100-200 MB） | 同左 |
| 磁盘 | ≥ 200 MB（含内嵌 JRE + 默认模型） | 同左 |
| 额外依赖（Vulkan 后端） | — | Vulkan SDK（仅 Vulkan 后端构建时需要） |
