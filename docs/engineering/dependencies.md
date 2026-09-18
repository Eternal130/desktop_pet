# 第三方库选型

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。
> Cubism SDK 在引擎中的集成架构和关键 API，详见 [Cubism SDK 集成架构](../renderer/cubism-sdk.md)。

---

## 一、C++ 端

| 库 | 版本 | 用途 | 集成方式 | 阶段 |
|:---|:---|:---|:---|:---:|
| Cubism Native SDK | 5-r.5-beta.3.1 | Live2D 模型加载与渲染（兼容 Cubism 5 / 5.3） | 预编译库 + 框架源码（third_party/CubismSdkForNative/） | **MVP** |
| GLFW | 3.4 | 窗口创建与输入事件处理（`platform/WindowManager.cpp` 被 GL/Vulkan 共享） | SDK 内置源码编译（Samples/OpenGL/thirdParty/glfw/），缺失时自动下载 | **MVP** |
| GLEW | 2.2.0 | OpenGL 扩展函数加载（仅 OpenGL 后端） | SDK 内置源码编译（Samples/OpenGL/thirdParty/glew/），缺失时自动下载 | **MVP** |
| Vulkan SDK | 系统（≥ 1.2） | Vulkan 渲染后端（Instance/Device/Swapchain/Render/Present），仅 `-DUSE_VULKAN=ON` 时需要 | 系统安装（`find_package(Vulkan REQUIRED)`） | **Phase 2.x** |
| stb_image | — | 纹理图片加载辅助 | SDK 内置 Header-only（Samples/OpenGL/thirdParty/stb/） | **MVP** |
| nlohmann/json | 3.12.0 | JSON 序列化 / 反序列化 | Header-only（renderer/third_party/nlohmann/） | **Phase 1** |
| IXWebSocket | 11.4.6 | WebSocket 客户端（本地通信，不启用 TLS），渲染引擎启动后主动连接控制面板 | 源码编译（renderer/third_party/ixwebsocket/，CMake subdirectory） | **Phase 1** |
| Google Test | 1.17.0 | C++ 单元测试框架 | 源码编译（renderer/third_party/googletest/，CMake subdirectory） | **Phase 1** |
| miniaudio | 单文件 | 音频播放引擎（`AudioManager` 封装，OGG 解码 + 播放 + 音量控制） | Single-header vendored（renderer/third_party/miniaudio.h） | **Phase 3b** |
| libogg + libvorbis | 1.3.5 / 1.3.7 | OGG/Vorbis 音频解码（配合 miniaudio 播放 OGG 文件） | CMake FetchContent 拉取 | **Phase 3b** |

> **说明**：GLFW、GLEW、stb_image 三个库使用 Cubism SDK 内置的版本（位于 `Samples/OpenGL/thirdParty/`），与 SDK 保持版本一致性（缺失时构建脚本自动下载）。渲染引擎自身的 `renderer/third_party/` 目录包含项目额外引入的依赖：nlohmann/json、IXWebSocket、Google Test、miniaudio（single-header）。Vulkan SDK 为系统级安装（仅 Vulkan 后端），libogg/libvorbis 通过 CMake FetchContent 拉取。
>
> **日志方案**：渲染引擎使用 Cubism SDK 内置的 `LAppPal::PrintLogLn` 函数作为日志输出（基于 `CubismFramework::CubismLogFunction` 回调），**未引入 spdlog**（项目反模式：禁止 spdlog，统一用 `LAppPal::PrintLogLn`）。

**音频库选型决策（OpenAL Soft vs SDL\_mixer）**：

| 维度 | OpenAL Soft | SDL\_mixer |
|:---|:---|:---|
| 依赖体量 | 轻量，仅音频功能 | 需引入 SDL2 核心库 + SDL\_mixer |
| API 风格 | 状态机式，与 OpenGL API 风格一致 | 函数式，上手较简单 |
| 多格式支持 | WAV 原生支持；MP3/OGG 需额外解码器（可选引入 dr\_libs） | WAV/MP3/OGG/FLAC 开箱即用 |
| 跨平台 | Windows / Linux / macOS | 同等支持 |
| 3D 音频 | 原生支持（桌面宠物不需要，但无额外开销） | 不支持 |

**结论**：选择 **OpenAL Soft**。桌面宠物音频需求简单（音效播放 + 口型同步），OpenAL 依赖更轻量且 API 风格与 OpenGL 一致。Live2D 模型内置音频通常为 WAV 格式，已满足需求；如后续需支持 MP3/OGG，可引入 dr\_libs（header-only 单文件）辅助解码，无需引入完整的 SDL 依赖栈。

> **实现更新（Phase 3b，已落地）**：最终实现**未采用 OpenAL Soft 或 SDL\_mixer**，改为 **miniaudio（single-header）+ libvorbis（OGG 解码）**。理由：(1) miniaudio 单文件 vendored，零系统依赖，跨 Windows/Linux 编译更简单；(2) 桌面宠物音频需求为 OGG 单声道播放 + 音量控制 + 口型同步时间戳，miniaudio 配合 libvorbis 已满足；(3) 避免 OpenAL 系统包（`libopenal-dev`）在 Windows/MinGW 下的打包复杂度。渲染器侧 `AudioManager` 已实现 `play_audio`/`stop_audio`/`set_volume` 三指令（错误码 7001/7002/7003），**控制器侧 `AudioMappingManager` 与 UI 待实现**。上述 OpenAL vs SDL\_mixer 对比保留作为历史选型记录。

---

## 二、Qt 控制面板（controller_qt）

| 库 | 版本 | 用途 | 集成方式 | 阶段 |
|:---|:---|:---|:---|:---:|
| Qt | 6.8 LTS 起步（开发版本 6.10.0） | UI 框架（QML + Quick Controls 2）、WebSocket 服务端（`QWebSocketServer`）、系统托盘（`QSystemTrayIcon`）、图表（QtCharts）、测试（QTest） | 系统安装 / Qt 在线安装器（`CMAKE_PREFIX_PATH` 指向 Qt 安装目录） | **Phase 5-9** |
| FluentUI（QML 组件库） | main 分支 | UI 组件（FluWindow / FluNavigationView / FluFrame / FluButton 等） | CMake FetchContent 静态链接 | **Phase 5-9** |
| spdlog | 1.15.0 | 日志（轮转文件 + Qt 消息桥接，`src/logging/Logging.cpp`） | CMake FetchContent | **Phase 5-9** |

> **说明**：语音包 meta.mko 解析使用**手写 protobuf wire-format reader**（`MetaMkoParser`，约 150 行），**不引入 libprotobuf 依赖**；`bundles.proto` schema 参考文件原样保留于 `controller_qt/src/protobuf/`（不编译）。

---

## 三、Cubism Native SDK 集成详解

Cubism SDK 是本项目最核心的第三方依赖，其集成方式有别于常规 C++ 库，此处专门说明。

### 3.1 SDK 版本信息

| 项目 | 值 |
|:---|:---|
| SDK 版本 | 5-r.5-beta.3.1 |
| Cubism 兼容性 | Cubism 5 / 5.3（支持 moc3 v3.0 ~ v5.3 全版本） |
| Framework 许可 | Live2D Open Software License（开源） |
| Core 许可 | Live2D Proprietary Software License（闭源） |
| SDK 手册 | [Cubism SDK Manual](https://docs.live2d.com/cubism-sdk-manual/top/) |

### 3.2 License 要求（重要）

Cubism SDK 采用**双许可模式**，开发和发布前必须确认：

| 许可类型 | 适用组件 | 条件 |
|:---|:---|:---|
| Live2D Open Software License | Framework（源码）、Samples | 需遵守许可条款，可免费使用 |
| Live2D Proprietary Software License | Core（预编译库） | 需遵守许可条款，可免费使用 |
| **Cubism SDK Release License** | **所有组件** | **年营收超过 1000 万日元的企业需获取商业出版许可** |
| Free Material License | 内置示例模型（Haru 等 8 个） | 使用示例模型需单独同意各模型条款 |

> **注意**：即使个人/小规模使用无需商业许可，发布应用时仍需在应用中注明 Live2D 相关版权信息。详见 `third_party/CubismSdkForNative/NOTICE.md`。

### 3.3 SDK 目录结构

```plain
third_party/CubismSdkForNative/
├── Core/                           # 闭源预编译库（C 接口）
│   ├── include/
│   │   └── Live2DCubismCore.h      # 唯一头文件（纯 C API）
│   ├── lib/                        # 静态库（按平台/架构组织）
│   │   ├── linux/x86_64/           # ← 本项目 Linux 版使用
│   │   ├── windows/x86_64/         # ← 本项目 Windows 版使用（当前已支持）
│   │   └── ...                     # android/, ios/, macos/, experimental/
│   └── dll/                        # 动态库（同上结构）
│       └── linux/x86_64/           # libLive2DCubismCore.so
│
├── Framework/                      # 开源 C++ 框架
│   ├── CMakeLists.txt              # 编译为静态库（add_library STATIC）
│   └── src/                        # 框架源码
│       ├── CubismFramework.hpp     # 框架初始化入口
│       ├── Model/                  # 模型加载与管理
│       ├── Motion/                 # 动作播放与表情
│       ├── Effect/                 # 自动眨眼、呼吸、姿势切换
│       ├── Physics/                # 物理演算
│       ├── Rendering/              # 渲染器（含 OpenGL/ 子目录）
│       ├── Id/                     # 参数/部件 ID 管理
│       ├── Math/                   # 矩阵与向量运算
│       └── Utils/                  # JSON 解析、日志工具
│
├── Samples/                        # 示例代码（仅参考，不编译）
│   ├── Common/                     # 跨平台公共代码
│   │   ├── LAppModel_Common.*      # 模型封装参考实现
│   │   ├── LAppWavFileHandler_Common.* # WAV 音频播放参考
│   │   ├── LAppAllocator_Common.*  # 内存分配器参考实现
│   │   └── MouseActionManager_Common.* # 鼠标交互参考
│   ├── OpenGL/                     # OpenGL 示例（含 Linux CMake 项目）
│   └── Resources/                  # 内置示例模型
│       ├── Haru/                   # 8 个免费示例模型
│       ├── Hiyori/
│       ├── Mao/
│       ├── Mark/
│       ├── Natori/
│       ├── Ren/
│       ├── Rice/
│       └── Wanko/
│
├── cubism-info.yml                 # SDK 版本元数据
├── LICENSE.md                      # 许可说明
└── NOTICE.md                       # 使用须知
```

### 3.4 CMake 集成方式

本项目将 Framework 作为 CMake 子目录编译为静态库，Core 作为预编译库直接链接：

```cmake
# 顶层 CMakeLists.txt 中的集成方式

# 1. 设置 SDK 路径
set(CUBISM_SDK_DIR ${CMAKE_SOURCE_DIR}/third_party/CubismSdkForNative)
set(CUBISM_CORE_DIR ${CUBISM_SDK_DIR}/Core)
set(CUBISM_FRAMEWORK_DIR ${CUBISM_SDK_DIR}/Framework)

# 2. Core：预编译库导入
add_library(CubismCore STATIC IMPORTED)
set_target_properties(CubismCore PROPERTIES
    IMPORTED_LOCATION ${CUBISM_CORE_DIR}/lib/linux/x86_64/libLive2DCubismCore.a
    INTERFACE_INCLUDE_DIRECTORIES ${CUBISM_CORE_DIR}/include
)

# 3. Framework：源码编译为静态库
#    需要设置渲染器类型（OpenGL）
set(RENDER_INCLUDE_PATH ${GLEW_INCLUDE_DIRS})  # Framework 需要 OpenGL 头文件路径
add_compile_definitions(CSM_TARGET_LINUX_GL)     # 指定平台渲染目标
add_subdirectory(${CUBISM_FRAMEWORK_DIR})

# 4. 链接到渲染引擎
target_link_libraries(desktop-pet-renderer PRIVATE
    Framework          # Cubism Framework 静态库
    CubismCore         # Cubism Core 预编译静态库
    ...
)
target_include_directories(desktop-pet-renderer PRIVATE
    ${CUBISM_FRAMEWORK_DIR}/src  # Framework 头文件
    ${CUBISM_CORE_DIR}/include   # Core 头文件
)
```

**Framework 编译宏**（通过 `target_compile_definitions` 或 CMake 变量传递）：

| 宏 | 说明 |
|:---|:---|
| `CSM_TARGET_LINUX_GL` | Linux + OpenGL 渲染目标 |
| `CSM_TARGET_WIN_GL` | Windows + OpenGL 渲染目标（当前已支持） |
| `USE_RENDER_TARGET` | 启用离屏渲染到纹理（可选） |
| `USE_MODEL_RENDER_TARGET` | 启用每模型独立渲染目标（可选） |

### 3.5 本项目使用的平台库

| 平台 | 架构 | 静态库路径 | 动态库路径 |
|:---|:---|:---|:---|
| Linux (MVP) | x86_64 | `Core/lib/linux/x86_64/libLive2DCubismCore.a` | `Core/dll/linux/x86_64/libLive2DCubismCore.so` |
| Windows (当前) | x86_64 | `Core/lib/windows/x86_64/` | `Core/dll/windows/x86_64/Live2DCubismCore.dll` |

> 本项目 Linux 版使用**静态链接**方式集成 Core，简化分发打包；Windows 版分发时使用动态库 `Live2DCubismCore.dll`（随 `build/bin/` 一起输出）。

### 3.6 示例模型资源

SDK 内置 8 个免费示例模型（`Samples/Resources/`），可在开发调试阶段直接使用，但正式发布需遵守 [Free Material License](https://www.live2d.com/eula/live2d-free-material-license-agreement_en.html) 及各模型的[使用条款](https://www.live2d.com/eula/live2d-sample-model-terms_en.html)。
