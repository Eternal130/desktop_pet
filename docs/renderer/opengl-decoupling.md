# 渲染后端解耦方案 — OpenGL → OpenGL/Vulkan 双后端

> **分支**：`refactor/decouple-opengl-renderer`
>
> **目标**：将 `renderer/src/` 中硬编码的 OpenGL 依赖抽取为可替换的渲染后端接口，使项目能够在 OpenGL 和 Vulkan 之间切换，为后续 Vulkan 集成铺平道路。
>
> **实现状态**：✅ 本方案已全部落地（Phase 2.1-2.6）。分支 `refactor/decouple-opengl-renderer` 已合并。详见 `renderer/src/graphics/`（`IGraphicsBackend.hpp` + `OpenGLBackend.cpp` + `VulkanBackend.cpp`，979 行完整 Instance→Device→Swapchain→Render→Present 流水线）与 `renderer/src/platform/WindowManager.cpp`。
>
> **相关文档**：[Cubism SDK 集成](./cubism-sdk.md) | [渲染引擎设计](./README.md)

---

## 〇、实施完成情况速览

> 下表对照本文档「四、分阶段实施方案」与「五、实施优先级」，标注各 Phase 在当前代码库的落地状态。正文设计方案为历史设计记录，保持原样不动。

| Phase | 内容 | 实施状态 |
|:---|:---|:---:|
| Phase 1.3 | 抽取 `WindowManager`（纯提取，零风险） | ✅ 已完成 |
| Phase 1.4 | 重构 `LAppTextureManager`（接口变更） | ✅ 已完成 |
| Phase 1.2 | 实现 `OpenGLBackend` + `IGraphicsBackend` 接口 | ✅ 已完成 |
| Phase 1.1 | 精简 `LAppDelegate`（持有 `WindowManager*` + `IGraphicsBackend*`） | ✅ 已完成 |
| Phase 1.5 | `LAppModel` 条件编译（`CUBISM_RENDERER_TYPE` 宏） | ✅ 已完成 |
| Phase 1 清理 | 去无用 GL/GLFW include | ✅ 已完成 |
| Phase 2.1 | Vulkan 核心基础设施（VulkanManager + SwapchainManager 合并入 `VulkanBackend`） | ✅ 已完成 |
| Phase 2.2 | `CubismRenderer_Vulkan` 静态初始化（`InitializeConstantSettings`） | ✅ 已完成 |
| Phase 2.3 | Vulkan 纹理管线（`CreateTextureFromPngFile` 重载 + `CubismImageVulkan`） | ✅ 已完成 |
| Phase 2.4 | ~~Vulkan 精灵管线~~（跳过，本项目无 Demo UI） | ⏭️ 按计划跳过 |
| Phase 2.5 | Vulkan 渲染循环集成（`LAppView`/`LAppModel`/`LAppLive2DManager`） | ✅ 已完成 |
| Phase 2.6 | 像素回读（点击穿透，`vkCmdCopyImageToBuffer`） | ✅ 已完成 |
| Phase 2.7 | Swapchain 重建与窗口管理适配 | ✅ 已完成 |
| Phase 3 | CMake 构建系统改造（`-DUSE_VULKAN=ON` 条件编译） | ✅ 已完成 |

> **与原方案的实现偏差**（均已在正文相关位置标注，此处汇总）：
> - **动态渲染**：实际使用 `vkCmdBeginRendering` 动态渲染，无 `VkRenderPass` / `VkFramebuffer`（原方案 2.2 节末已修正）。
> - **Vulkan 纹理路径**：`VulkanBackend::CreateTexture` 返回 0 / `DeleteTexture` 空实现是**有意设计**——Vulkan 纹理由 `LAppTextureManager` 的 `CreateTextureFromPngFile(VkFormat,...)` 重载直接生成 `CubismImageVulkan`，不经接口。详见 [渲染引擎设计](./README.md) 的「1.3 渲染后端抽象层」小节。
> - **编译时切换**：确认无运行时切换；`USE_VULKAN` CMake 选项 + `CUBISM_RENDERER_TYPE` 宏控制 CubismRenderer 子类选择。

---

## 一、现状分析

### 1.1 架构分层

```
┌─────────────────────────────────────────────────────────┐
│                    App 层 (renderer/src/)                │
│  LAppDelegate / LAppView / LAppModel / LAppTextureManager│
│  + 业务逻辑: network/, AudioManager                      │
├─────────────────────────────────────────────────────────┤
│               Common 层 (SDK Samples/Common/)            │
│  LApp*_Common 基类 — 平台无关                            │
├─────────────────────────────────────────────────────────┤
│             Framework 层 (SDK Framework/src/Rendering/)  │
│  CubismRenderer (抽象基类)                                │
│  ├── CubismRenderer_OpenGLES2 (GL 实现)                 │
│  ├── CubismRenderer_Vulkan    (VK 实现)                 │
│  ├── D3D11 / Metal ...                                  │
│  CubismRenderer::Create() 工厂方法，由预处理器宏决定实例化 │
└─────────────────────────────────────────────────────────┘
```

SDK Framework 层已经完成了渲染后端的抽象（`CubismRenderer` 基类 + 各平台子类），但本项目的 App 层代码直接硬编码了 OpenGL 调用，未利用该抽象。

### 1.2 耦合清单

| # | 文件 | 耦合类型 | 耦合量 | 说明 |
|---|------|----------|--------|------|
| 1 | `LAppDelegate.hpp/cpp` | GLFW + GL 上下文 + PBO 像素回读 | **最重** ~30 处 GL 调用 | 窗口初始化、渲染循环、清屏、点击穿透 |
| 2 | `LAppTextureManager.hpp/cpp` | GL 纹理 API | 中 ~10 处 | glGenTextures / glTexImage2D / glDeleteTextures |
| 3 | `LAppModel.cpp` | 硬编码 `CubismRenderer_OpenGLES2` | 低 5 处 | 模板参数、#include |
| 4 | `LAppView.hpp` | 无用 GL include | 低 1 处 | `#include <GL/glew.h>` 实际未使用 |
| 5 | `LAppLive2DManager.cpp` | 无用 GL include + glfwGetWindowSize | 低 2 处 | `#include <GL/glew.h>` + 1 处 glfw 调用 |
| 6 | `CMakeLists.txt` | GL 构建配置 | 低 | FRAMEWORK_SOURCE / 宏 / 链接库 / shader 路径 |

### 1.3 各文件 OpenGL 调用详情

#### LAppDelegate.hpp/cpp（最重，~30 处）

| 调用 | 位置 | 用途 |
|------|------|------|
| `#include <GL/glew.h>` / `<GLFW/glfw3.h>` | .hpp:12-13 | 头文件 |
| `GLFWwindow* _window` | .hpp:78 | 成员变量 |
| `GLuint _pbo` | .hpp:117 | PBO 像素回读缓冲 |
| `glfwInit()` | .cpp:76 | GLFW 初始化 |
| `glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, ...)` 等 | .cpp:86-89 | 窗口属性 |
| `glfwCreateWindow()` | .cpp:94 | 创建窗口 |
| `glfwMakeContextCurrent()` | .cpp:131 | GL 上下文绑定 |
| `glewInit()` | .cpp:134 | GLEW 初始化 |
| `glTexParameteri()` ×2 | .cpp:144-145 | 纹理采样参数 |
| `glEnable(GL_BLEND)` / `glBlendFunc()` | .cpp:148-149 | 混合模式 |
| `glGenBuffers/glBufferData` (PBO) | .cpp:151-155 | 像素回读 PBO |
| `glfwSetMouseButton/CursorPos/ScrollCallback` | .cpp:158-160 | 输入回调 |
| `glViewport()` | .cpp:167, 243 | 视口 |
| `glClearColor/glClear/glClearDepth` | .cpp:268-270 | 清屏 |
| PBO 像素回读逻辑 | .cpp:287-298 | 点击穿透检测核心 |
| `glfwSwapBuffers()` | .cpp:319 | 缓冲交换 |
| `glfwSwapInterval()` | .cpp:132, 393-398 | VSync |
| `glfwSetWindowAttrib(GLFW_MOUSE_PASSTHROUGH)` | .cpp:303, 313 | 鼠标穿透 |
| `glDeleteBuffers()` | .cpp:202 | PBO 释放 |
| `glfwDestroyWindow()` / `glfwTerminate()` | .cpp:216, 218 | 清理 |
| `GL_TRUE`/`GL_FALSE` 返回值 | .cpp:79, 187 | 函数返回 |

#### LAppTextureManager.hpp/cpp（中，~10 处）

| 调用 | 位置 | 用途 |
|------|------|------|
| `#include <GL/glew.h>` / `<GLFW/glfw3.h>` | .hpp:11-12 | 头文件 |
| `glGenTextures()` | .cpp:72 | 创建纹理 |
| `glBindTexture()` | .cpp:73, 78 | 绑定纹理 |
| `glTexImage2D()` | .cpp:74 | 上传纹理数据 |
| `glGenerateMipmap()` | .cpp:75 | Mipmap |
| `glTexParameteri()` ×2 | .cpp:76-77 | 过滤模式 |
| `glDeleteTextures()` | .cpp:103, 117, 130 | 删除纹理 |

#### LAppModel.cpp（低，5 处）

| 调用 | 位置 | 用途 |
|------|------|------|
| `#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>` | .cpp:15 | 头文件 |
| `GetRenderer<CubismRenderer_OpenGLES2>()->DrawModel()` | .cpp:505 | 模型绘制 |
| `GetRenderer<CubismRenderer_OpenGLES2>()->SetMvpMatrix()` | .cpp:525 | MVP 矩阵 |
| `GetRenderer<CubismRenderer_OpenGLES2>()->BindTexture()` | .cpp:664 | 纹理绑定 |
| `GetRenderer<CubismRenderer_OpenGLES2>()->IsPremultipliedAlpha()` | .cpp:668-670 | 预乘 Alpha |

#### LAppView.hpp / LAppLive2DManager.cpp（低，各 1-2 处）

| 调用 | 位置 | 用途 |
|------|------|------|
| `#include <GL/glew.h>` | LAppView.hpp:10 | **无用 include**，可安全删除 |
| `#include <GL/glew.h>` | LAppLive2DManager.cpp:13 | **无用 include**，可安全删除 |
| `glfwGetWindowSize()` | LAppLive2DManager.cpp:186 | 获取窗口尺寸 |

### 1.4 SDK 已有的抽象（不需要重复造的轮子）

- **`CubismRenderer`** 抽象基类 — Framework 层已完美隔离了渲染实现
- **`CubismRenderer::Create(width, height)`** 工厂方法 — 根据 `FRAMEWORK_SOURCE` 编译期决定实例化哪个子类
- **`LApp*_Common` 基类** — 平台无关，App 层继承后只需实现平台特定部分
- **`LAppTextureManager_Common`** — 公共纹理信息结构（`TextureInfo` 含 id/width/height/fileName）
- **Vulkan Demo** 已提供完整的 `VulkanManager` / `SwapchainManager` 参考实现

### 1.5 Vulkan 与 OpenGL 的关键 API 差异

| 能力 | OpenGL | Vulkan | 影响范围 |
|------|--------|--------|----------|
| 窗口创建 | `glfwCreateWindow` + `glfwMakeContextCurrent` | `glfwCreateWindow` + `glfwCreateWindowSurface` | GLFW 保留，上下文创建不同 |
| 渲染器创建 | `CubismRenderer::Create(w, h)` 即可 | 需额外调用 `InitializeConstantSettings(device, physicalDevice, ...)` | LAppModel::LoadAssets |
| 纹理绑定 | `renderer->BindTexture(slot, GLuint)` | `renderer->BindTexture(CubismImageVulkan&)` | LAppModel::SetupTextures，参数类型完全不同 |
| 模型绘制 | `renderer->DrawModel()` 直接调用 | 需 `BeginRendering(cmdBuf)` → `DrawModel()` → `EndRendering(cmdBuf)` + `PostDraw()` | LAppLive2DManager::OnUpdate，循环结构不同 |
| 清屏 | `glClearColor` + `glClear()` | 通过 command buffer 录制 clear command | LAppDelegate::Run |
| 缓冲交换 | `glfwSwapBuffers(window)` | `SwapchainManager::Present()` | LAppDelegate::Run |
| 像素回读 | PBO + `glMapBuffer` / `glReadPixels` | `vkCmdReadPixels` 或 staging buffer | 点击穿透检测，逻辑完全不同 |
| VSync | `glfwSwapInterval(1)` | swapchain present mode (`VK_PRESENT_MODE_FIFO`) | LAppDelegate::SetTargetFps |
| 纹理创建 | `glGenTextures` + `glTexImage2D` | `vkCreateImage` + staging buffer + `vkCmdCopyBufferToImage` | LAppTextureManager |

---

## 二、设计决策

### 2.1 策略：编译时后端切换 + 接口抽象

选择 **编译时切换**（`#ifdef USE_VULKAN`）而非运行时多态（虚函数/策略模式），原因：

1. **渲染循环结构差异巨大** — GL 是立即模式（`glClear → draw → swap`），VK 是命令缓冲模式（`begin cmd → record → end cmd → submit → present`）。强行统一为同一接口会导致 Vulkan 调用被迫绕成 GL 的形状，丧失 Vulkan 的设计优势。
2. **SDK 自身的设计** — `CubismRenderer::Create()` 工厂方法本身就是编译期分支（由 `FRAMEWORK_SOURCE` CMake 变量决定），两个渲染器的方法签名甚至不同（如 `BindTexture` 参数类型完全不同）。
3. **桌面宠物场景** — 用户不会运行时切换渲染后端，编译期选一个即可。

### 2.2 接口抽象的边界

并非所有 GL 调用都需要抽象。遵循最小抽象原则：

| 需要抽象 | 不需要抽象（直接条件编译） |
|----------|--------------------------|
| 窗口管理（GLFW 是两个后端共用的） | `CubismRenderer` 的子类选择（SDK 已处理） |
| 纹理创建/删除（`LAppTextureManager` 内部） | 渲染循环结构（`LAppDelegate::Run`） |
| 像素回读（点击穿透检测） | `LAppModel` 中的 `GetRenderer<T>()` 调用 |
| 清屏 / 视口 | `LAppTextureManager` 的 Vulkan 版签名 |

**核心思路**：将 **GLFW 窗口管理** 抽取为公共组件，将 **GL 图形调用** 封装进 `OpenGLBackend`，将 **VK 图形调用** 封装进 `VulkanBackend`。两个 Backend 实现同一接口，由 `LAppDelegate` 持有接口指针，在编译期决定实例化哪个。

---

## 三、目标架构

### 3.1 解耦后的文件结构

```
renderer/src/
├── graphics/                        ← 新建：渲染后端
│   ├── IGraphicsBackend.hpp         ← 接口定义
│   ├── OpenGLBackend.hpp/.cpp       ← GL 实现（从 LAppDelegate 迁移）
│   └── VulkanBackend.hpp/.cpp       ← VK 实现（Phase 2）
├── platform/                        ← 新建：平台抽象
│   └── WindowManager.hpp/.cpp       ← GLFW 窗口管理（GL/VK 共用）
├── main.cpp                         ← 不变
├── LAppDelegate.hpp/.cpp            ← 精简：持有 IGraphicsBackend* + WindowManager*
├── LAppView.hpp/.cpp                ← 去掉无用 GL include
├── LAppModel.hpp/.cpp               ← 条件编译 CubismRenderer 子类
├── LAppTextureManager.hpp/.cpp      ← 去掉 GL include，通过 Backend 操作纹理
├── LAppLive2DManager.hpp/.cpp       ← 去掉无用 GL include
├── LAppDefine.hpp/.cpp              ← 不变
├── LAppPal.hpp/.cpp                 ← 不变
├── AudioManager.hpp/.cpp            ← 不变
└── network/                         ← 不变
```

### 3.2 解耦后的依赖关系

```
main.cpp
  └→ LAppDelegate
       ├→ WindowManager (GLFW 窗口，共用)
       ├→ IGraphicsBackend* (OpenGLBackend 或 VulkanBackend)
       ├→ LAppView (纯逻辑，无图形依赖)
       ├→ LAppLive2DManager
       │    └→ LAppModel (条件编译引用 CubismRenderer 子类)
       │         └→ LAppTextureManager (通过 Backend 创建/删除纹理)
       ├→ Network::WebSocketClient
       └→ AudioManager
```

---

## 四、分阶段实施方案

### Phase 1：抽取渲染后端接口（不影响现有功能）

> **原则**：每步完成后 OpenGL 模式功能与重构前完全一致。

#### 步骤 1.3：抽取 WindowManager（纯提取，零风险）

**新建文件**：`renderer/src/platform/WindowManager.hpp/.cpp`

从 `LAppDelegate.cpp` 中提取所有 GLFW 窗口管理代码：

```cpp
class WindowManager {
public:
    bool Initialize(int width, int height);
    void Release();
    void Run(); // 不包含渲染逻辑，只管事件循环

    GLFWwindow* GetWindow() const;
    void GetWindowSize(int& width, int& height) const;
    void SetWindowSize(int width, int height) const;
    void SetWindowPosition(int x, int y) const;
    void GetWindowPosition(int& x, int& y) const;
    void ShowWindow();
    void SetMousePassthrough(bool enable);
    void SetTargetFps(double fps);
    void WaitEvents(double timeout);
    bool ShouldClose() const;
    void SetShouldClose();

    // Win32 特有
    static void ApplyDesktopPetWindowStyle(HWND hwnd);
    static LRESULT CALLBACK WindowSubclassProc(HWND, UINT, WPARAM, LPARAM);
};
```

**迁移内容**：
- `glfwInit()` / `glfwTerminate()`
- `glfwWindowHint()` 系列（透明、无边框、置顶）
- `glfwCreateWindow()`
- Win32 HWND 操作（`FindWindow` → `SetWindowLongPtr` → `WS_EX_TOOLWINDOW` → `WS_EX_TOPMOST` → 子类化）
- `glfwSetMouseButtonCallback` / `glfwSetCursorPosCallback` / `glfwSetScrollCallback`
- `glfwSetWindowAttrib(GLFW_MOUSE_PASSTHROUGH)`
- `glfwSwapInterval()`
- `glfwWaitEventsTimeout()`
- `glfwGet/SetWindowPos/Size()`
- `glfwWindowShouldClose()` / `glfwSetWindowShouldClose()`
- `glfwShowWindow()`
- 事件回调静态转发类 `EventHandler`

**不迁移内容**（留在 LAppDelegate 或移至 Backend）：
- `glfwMakeContextCurrent()` → OpenGLBackend
- `glewInit()` → OpenGLBackend
- 所有 `gl*` 调用 → OpenGLBackend
- PBO 像素回读 → OpenGLBackend

#### 步骤 1.4：重构 LAppTextureManager

**修改文件**：`renderer/src/LAppTextureManager.hpp/.cpp`

- 删除 `#include <GL/glew.h>` 和 `#include <GLFW/glfw3.h>`
- `TextureInfo.id` 类型从 `GLuint` 改为 `uint64_t`（不透明句柄）
- `CreateTextureFromPngFile()` 内部的 `glGenTextures/glTexImage2D/...` 替换为调用 `IGraphicsBackend::CreateTexture()`
- `ReleaseTextures()` / `ReleaseTexture()` 内部的 `glDeleteTextures()` 替换为调用 `IGraphicsBackend::DeleteTexture()`

#### 步骤 1.2：实现 OpenGLBackend

**新建文件**：`renderer/src/graphics/IGraphicsBackend.hpp` + `renderer/src/graphics/OpenGLBackend.hpp/.cpp`

接口定义（只覆盖 GL/VK 差异化的部分，不覆盖完全相同或完全不同的部分）：

```cpp
class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;

    // 上下文初始化/销毁（OpenGL: glewInit; Vulkan: CreateInstance/Device/Swapchain）
    virtual bool InitializeGraphics(GLFWwindow* window) = 0;
    virtual void ReleaseGraphics() = 0;

    // 每帧操作
    virtual void BeginFrame(int width, int height) = 0;  // glViewport + glClear / vk begin cmd
    virtual void EndFrame(GLFWwindow* window) = 0;        // glfwSwapBuffers / vk present

    // 纹理操作
    virtual uint64_t CreateTexture(const void* data, int width, int height, int channels) = 0;
    virtual void DeleteTexture(uint64_t handle) = 0;

    // 点击穿透像素回读
    virtual bool IsPixelTransparent(int x, int y, int windowHeight) = 0;
};
```

`OpenGLBackend` 实现内容（从 LAppDelegate 迁移）：
- `glewInit()`、`glTexParameteri()`（全局纹理采样）、`glEnable(GL_BLEND)` / `glBlendFunc()`
- PBO 创建/销毁（`glGenBuffers/glBufferData/glDeleteBuffers`）
- `glViewport()`、`glClearColor/glClear/glClearDepth()`
- `glfwSwapBuffers()`
- PBO 像素回读逻辑（`glBindBuffer/glMapBuffer/glUnmapBuffer/glReadPixels`）

#### 步骤 1.5：LAppModel 条件编译

**修改文件**：`renderer/src/LAppModel.cpp`

将 5 处 `GetRenderer<CubismRenderer_OpenGLES2>()` 用条件编译包裹：

```cpp
#ifdef USE_VULKAN
    #include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
    #define CUBISM_RENDERER_TYPE Rendering::CubismRenderer_Vulkan
#else
    #include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
    #define CUBISM_RENDERER_TYPE Rendering::CubismRenderer_OpenGLES2
#endif

// 使用处统一改为:
GetRenderer<CUBISM_RENDERER_TYPE>()->DrawModel();
```

> **为什么不抽象 `BindTexture` 的签名差异**：GL 版参数是 `(csmUint32 slot, GLuint textureId)`，VK 版参数是 `(CubismImageVulkan& image)`。两者的参数类型完全不同，抽象只会增加一层无意义的包装。条件编译是 SDK 自身的设计惯例。

#### 步骤 1 清理

- `LAppView.hpp`：删除无用的 `#include <GL/glew.h>` 和 `#include <GLFW/glfw3.h>`
- `LAppLive2DManager.cpp`：删除无用的 `#include <GL/glew.h>`；将 `glfwGetWindowSize()` 改为通过 `WindowManager` 获取
- `LAppDelegate.hpp/cpp`：删除所有 `#include <GL/...>` 和 `GLFWwindow*` 成员、`GLuint _pbo` 成员；改为持有 `WindowManager*` 和 `IGraphicsBackend*`

#### Phase 1 文件变更汇总

| 操作 | 文件 | 说明 |
|------|------|------|
| **新建** | `renderer/src/platform/WindowManager.hpp` | GLFW 窗口管理声明 |
| **新建** | `renderer/src/platform/WindowManager.cpp` | GLFW 窗口管理实现 |
| **新建** | `renderer/src/graphics/IGraphicsBackend.hpp` | 渲染后端接口 |
| **新建** | `renderer/src/graphics/OpenGLBackend.hpp` | GL 后端声明 |
| **新建** | `renderer/src/graphics/OpenGLBackend.cpp` | GL 后端实现 |
| **修改** | `renderer/src/LAppDelegate.hpp` | 删除 GL 成员和 include，持有 WindowManager* + IGraphicsBackend* |
| **修改** | `renderer/src/LAppDelegate.cpp` | 删除 GL 调用，通过 WindowManager/Backend 委托 |
| **修改** | `renderer/src/LAppTextureManager.hpp` | 删除 GL include |
| **修改** | `renderer/src/LAppTextureManager.cpp` | 通过 Backend 操作纹理 |
| **修改** | `renderer/src/LAppView.hpp` | 删除无用 GL/GLFW include |
| **修改** | `renderer/src/LAppLive2DManager.hpp` | 删除无用 GL include（如有） |
| **修改** | `renderer/src/LAppLive2DManager.cpp` | 删除 GL include，glfwGetWindowSize → WindowManager |
| **修改** | `renderer/src/LAppModel.cpp` | 条件编译包裹 CubismRenderer 子类引用 |
| **修改** | `renderer/CMakeLists.txt` | 添加新源文件 |

---

### Phase 2：Vulkan 后端实现

> **参考代码**：`Samples/Vulkan/Demo/proj.win.cmake/src/` 下共 26 个源文件
>
> **关键发现**：
> - Demo 使用**动态渲染**（`vkCmdBeginRendering`），无 `VkRenderPass` / `VkFramebuffer`
> - Demo 有 3 个 OpenGL Demo 不存在的文件：`LAppSprite` / `LAppSpritePipeline` / `LAppModelSpritePipeline`（Vulkan UI 精灵管线）
> - Demo **没有**像素回读逻辑，点击穿透需自行实现
> - 纹理参数完全不同：`CreateTextureFromPngFile` 需要 `VkFormat/VkImageTiling/VkImageUsageFlags/VkMemoryPropertyFlags`
> - `BindTexture` 签名不同：GL 版是 `(slot, GLuint)`，VK 版是 `(CubismImageVulkan&)`
> - `CubismOffscreenManager_Vulkan` 单例管理离屏渲染目标，GL 路径无对应物

Phase 2 拆分为 7 个子阶段，按依赖顺序实施：

---

#### Phase 2.1：Vulkan 核心基础设施

> **目标**：移植 `VulkanManager` + `SwapchainManager`，创建 `VulkanBackend` 骨架，完成 Vulkan 设备初始化和 Swapchain 创建。

**新建文件**：`renderer/src/graphics/VulkanBackend.hpp/.cpp`

**移植来源**：
- `VulkanManager.hpp/.cpp`（543 行）→ Instance / PhysicalDevice / Device / Surface / Queue / CommandPool / SyncObjects / AcquireNextImage / Present / RecreateSwapchain
- `SwapchainManager.hpp/.cpp`（270 行）→ Swapchain / ImageView / Layout Transition / Present

**VulkanBackend 类设计**（合并 VulkanManager + SwapchainManager，实现 `IGraphicsBackend` 接口）：

```cpp
class VulkanBackend : public IGraphicsBackend {
public:
    // === IGraphicsBackend 接口 ===
    bool InitializeGraphics(GLFWwindow* window) override;
    void ReleaseGraphics() override;
    void BeginFrame(int width, int height) override;
    void EndFrame(GLFWwindow* window) override;
    uint64_t CreateTexture(const void* data, int width, int height, int channels) override;
    void DeleteTexture(uint64_t handle) override;
    bool IsPixelTransparent(int x, int y, int windowHeight) override;

    // === Vulkan 特有接口（条件编译调用） ===
    // 命令缓冲管理（LAppTextureManager、LAppView 等通过条件编译调用）
    VkCommandBuffer BeginSingleTimeCommands();
    void SubmitCommand(VkCommandBuffer cmdBuf, bool isFirstDraw = false);

    // Getter（LAppModel::LoadAssets/SetupTextures、LAppView 等通过条件编译调用）
    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkCommandPool GetCommandPool() const;
    VkQueue GetGraphicQueue() const;
    VkFormat GetDepthFormat() const;
    VkFormat GetImageFormat() const;  // VK_FORMAT_R8G8B8A8_UNORM

    // Swapchain 访问
    VkImage GetSwapchainImage() const;
    VkImageView GetSwapchainImageView() const;
    VkExtent2D GetSwapchainExtent() const;
    int32_t GetSwapchainImageCount() const;
    VkFormat GetSwapchainImageFormat() const;

    // Swapchain 重建
    void RecreateSwapchain();
    bool IsSwapchainInvalid() const;
    void SetSwapchainInvalid(bool flag);

    // OffscreenFrameBuffer 相关（配合 CubismOffscreenManager_Vulkan）
    void SetFrameBufferResized(bool flag);

private:
    // === VulkanManager 成员 ===
    VkInstance _instance = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _graphicQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    VkSemaphore _imageAvailableSemaphore;
    VkDebugUtilsMessengerEXT _debugMessenger;
    VkFormat _depthFormat;
    uint32_t _imageIndex = 0;

    // === SwapchainManager 成员 ===
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    Csm::csmVector<VkImage> _swapchainImages;
    Csm::csmVector<VkImageView> _swapchainImageViews;
    VkExtent2D _swapchainExtent = {0, 0};
    uint32_t _swapchainImageCount = 0;

    // === 状态标志 ===
    bool _isSwapchainInvalid = false;
    bool _framebufferResized = false;
    bool _enableValidationLayers = true;

    GLFWwindow* _window = nullptr;

    // === 内部初始化方法（对应 VulkanManager 的各步骤） ===
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void ChooseSupportedDepthFormat();
    void CreateCommandPool();
    void CreateSyncObjects();
    void CreateSwapchain();
    void TransitionSwapchainLayouts();  // UNDEFINED → PRESENT_SRC_KHR
};
```

**初始化序列**（`InitializeGraphics()` 内部，对应 Demo `VulkanManager::Initialize()` L421-L434）：

```
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)   // 关键：不创建 GL 上下文
→ CreateInstance()          // VK_API_VERSION_1_3, debug layers
→ SetupDebugMessenger()     // VK_EXT_debug_utils
→ CreateSurface()           // glfwCreateWindowSurface
→ PickPhysicalDevice()      // 需要 anisotropy + swapchain 支持
→ CreateLogicalDevice()     // 动态渲染 + 扩展动态状态 + Vulkan 1.3 synchronization2
→ ChooseSupportedDepthFormat()  // D32_SFLOAT_S8_UINT → D16_UNORM 回退链
→ CreateSwapchain()         // 含 ImageView 创建
→ TransitionSwapchainLayouts()  // 所有 swapchain image: UNDEFINED → PRESENT_SRC_KHR
→ CreateCommandPool()       // RESET_COMMAND_BUFFER_BIT
→ CreateSyncObjects()       // 单 semaphore
```

**每帧操作**：
- `BeginFrame()` → `vkAcquireNextImageKHR`（对应 Demo `UpdateDrawFrame()` L490-L503）
- `EndFrame()` → `SwapchainManager::QueuePresent`（对应 Demo `PostDraw()` L505-L515）

> **与原方案的关键差异**：原方案称 `SwapchainManager` 包含 `Framebuffer / RenderPass`，但实际上 Demo 使用**动态渲染**，没有这些对象。SwapchainManager 仅管理 Swapchain + ImageView。

**清理序列**（`ReleaseGraphics()` 内部，对应 Demo `VulkanManager::Destroy()` L526-L543）：
```
CleanupSwapchain() → vkDestroySemaphore → DestroyDebugMessenger →
vkDestroyCommandPool → vkDestroyDevice → vkDestroySurfaceKHR → vkDestroyInstance
```

**验证标准**：
- [ ] Vulkan Instance 创建成功
- [ ] 物理设备选择成功（支持 anisotropy + swapchain）
- [ ] 逻辑设备创建成功（动态渲染 + Vulkan 1.3 特性启用）
- [ ] Swapchain + ImageView 创建成功
- [ ] `vkAcquireNextImageKHR` 返回成功
- [ ] `vkQueuePresentKHR` 返回成功（黑屏即可，后续阶段才渲染内容）

---

#### Phase 2.2：CubismRenderer_Vulkan 静态初始化

> **目标**：调用 `InitializeConstantSettings()` 和 `SetRenderTarget()`，将 VulkanBackend 的设备信息注入 SDK 渲染器。

**修改文件**：`renderer/src/LAppDelegate.cpp`（初始化部分增加 `#ifdef USE_VULKAN` 块）

在 `LAppDelegate::Initialize()` 中，Phase 1 已创建 `IGraphicsBackend*` 并调用 `InitializeGraphics()`。Vulkan 路径需要额外调用 SDK 静态初始化：

```cpp
// LAppDelegate::Initialize() 中，InitializeGraphics() 之后
#ifdef USE_VULKAN
    auto* vkBackend = static_cast<VulkanBackend*>(_graphicsBackend);
    // 将 Vulkan 设备信息注入 SDK 渲染器（对应 Demo LAppDelegate.cpp L106-L112）
    Rendering::CubismRenderer_Vulkan::InitializeConstantSettings(
        vkBackend->GetDevice(),
        vkBackend->GetPhysicalDevice(),
        vkBackend->GetCommandPool(),
        vkBackend->GetGraphicQueue(),
        vkBackend->GetSwapchainImageCount(),
        vkBackend->GetSwapchainExtent(),
        vkBackend->GetSwapchainImageView(),
        vkBackend->GetSwapchainImageFormat(),
        vkBackend->GetDepthFormat()
    );
#endif
```

**Swapchain 重建时重新初始化**（对应 Demo `LAppDelegate::RecreateSwapchain()` L147-L182）：

```cpp
#ifdef USE_VULKAN
bool LAppDelegate::RecreateSwapchain() {
    auto* vkBackend = static_cast<VulkanBackend*>(_graphicsBackend);
    if (!vkBackend->IsSwapchainInvalid()) return false;

    // 等待窗口不再最小化
    int width = 0, height = 0;
    glfwGetFramebufferSize(_windowManager->GetWindow(), &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(_windowManager->GetWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkBackend->RecreateSwapchain();

    // 更新 SDK 渲染器目标（对应 Demo L160-L165）
    Rendering::CubismRenderer_Vulkan::SetRenderTarget(
        vkBackend->GetSwapchainImage(),
        vkBackend->GetSwapchainImageView(),
        vkBackend->GetSwapchainImageFormat(),
        vkBackend->GetSwapchainExtent()
    );

    _view->Initialize(width, height);
    _view->ResizeSprite(width, height);
    _view->DestroyRenderTarget();
    LAppLive2DManager::GetInstance()->SetRenderTargetSize(width, height);
    vkBackend->SetSwapchainInvalid(false);
    return true;
}
#endif
```

**验证标准**：
- [ ] `InitializeConstantSettings()` 调用成功
- [ ] 无 Vulkan 验证层错误
- [ ] Swapchain 重建后 `SetRenderTarget()` 更新正确

---

#### Phase 2.3：Vulkan 纹理管线

> **目标**：实现 Vulkan 版 `LAppTextureManager`，完成 `stb_image 解码 → staging buffer → image → mipmaps → view + sampler` 全流程。

**修改文件**：
- `renderer/src/LAppTextureManager.hpp` — 增加 Vulkan 特有成员和方法
- `renderer/src/LAppTextureManager.cpp` — Vulkan 版 `CreateTextureFromPngFile()` 实现

**LAppTextureManager Vulkan 版改造要点**：

1. **TextureInfo 内部存储差异**：GL 版存 `GLuint`，VK 版存 `CubismImageVulkan`（SDK 提供的 RAII 类，包含 VkImage + VkDeviceMemory + VkImageView + VkSampler）

2. **CreateTextureFromPngFile 签名完全不同**（对应 Demo `LAppTextureManager.cpp` L133-L218）：

```cpp
// Vulkan 版签名（比 GL 版多 5 个参数）
#ifdef USE_VULKAN
TextureInfo* CreateTextureFromPngFile(
    std::string fileName,
    VkFormat format,           // 通常 VK_FORMAT_R8G8B8A8_UNORM
    VkImageTiling tiling,      // 通常 VK_IMAGE_TILING_OPTIMAL
    VkImageUsageFlags usage,   // TRANSFER_SRC | TRANSFER_DST | SAMPLED
    VkMemoryPropertyFlags memProps,  // DEVICE_LOCAL_BIT
    float anisotropy           // 从 renderer 获取
);
#endif
```

3. **纹理创建流程**（对应 Demo L133-L218）：

```
stb_image_load_from_memory(fileName)     // CPU 端 PNG 解码
→ stagingBuffer.CreateBuffer(TRANSFER_SRC, HOST_VISIBLE)  // 创建 staging buffer
→ stagingBuffer.Map() → MemCpy(png) → UnMap()            // 上传像素数据
→ textureImage.CreateImage(w, h, mipLevels, format, OPTIMAL, ...)  // 创建 VkImage
→ BeginSingleTimeCommands()
→ textureImage.SetImageLayout(TRANSFER_DST_OPTIMAL)        // Image layout 转换
→ CopyBufferToImage(stagingBuffer → textureImage)          // vkCmdCopyBufferToImage
→ SubmitCommand()
→ GenerateMipmaps(textureImage, ...)                       // vkCmdBlitImage mipmap 链
→ textureImage.CreateView(format, COLOR_BIT, mipLevels)    // VkImageView
→ textureImage.CreateSampler(anisotropy, mipLevels)        // VkSampler
→ stagingBuffer.Destroy()
```

4. **内部辅助方法**（直接从 Demo 移植）：

```cpp
// Vulkan 版新增的私有方法
#ifdef USE_VULKAN
void CopyBufferToImage(VkCommandBuffer cmdBuf, const VkBuffer& buffer,
                       VkImage image, uint32_t width, uint32_t height);
void GenerateMipmaps(CubismImageVulkan image, uint32_t w, uint32_t h, uint32_t mipLevels);
bool GetTexture(uint32_t textureId, CubismImageVulkan& outTexture) const;
#endif
```

5. **纹理释放**（对应 Demo L220-L295）：`CubismImageVulkan::Destroy(device)` 替换 `glDeleteTextures()`

6. **mipLevels 计算**：`floor(log2(max(width, height))) + 1`

**注意**：Phase 1 将 `TextureInfo.id` 改为 `uint64_t` 后，Vulkan 版可直接复用。纹理查找仍按 `id` 匹配，`_textures` 向量存储 `CubismImageVulkan` 对象。

**验证标准**：
- [ ] PNG 解码 → Vulkan Image 全流程无错误
- [ ] Mipmap 生成正确（无验证层错误）
- [ ] VkSampler 创建成功
- [ ] 多次加载同一文件返回缓存（id 匹配）
- [ ] ReleaseTextures / ReleaseTexture 正确释放所有 Vulkan 资源

---

#### Phase 2.4：~~Vulkan 精灵管线~~ — 跳过

> **决策**：本项目不使用 SDK Demo 的 UI 精灵（背景/齿轮/电源按钮），因此跳过此阶段。

SDK Demo 中的 3 个精灵管线文件（`LAppSprite` / `LAppSpritePipeline` / `LAppModelSpritePipeline`）用于渲染 Demo 特有的 UI 叠加层。它们的存在原因是 Vulkan 的显式 API 需要预先创建 `VkPipeline`、`VkDescriptorSet`、GPU Buffer 等对象来绘制一个简单的 2D 纹理矩形——而这些在 OpenGL 中由驱动隐式处理。

本项目的桌面宠物不需要这些 Demo UI 元素，Cubism 模型渲染由 SDK 内部的 `CubismRenderer_Vulkan` 直接处理，无需额外精灵管线。

> **如果后续需要添加自定义 2D UI 叠加**（如 HUD、提示气泡），再参考 Demo 的 `LAppSpritePipeline` 实现即可。

---

#### Phase 2.5：Vulkan 渲染循环集成

> **目标**：改造 `LAppView`、`LAppModel`、`LAppLive2DManager` 的 Vulkan 渲染路径，完成主循环集成。

这是最复杂的子阶段，涉及 4 个文件的 Vulkan 路径改造。

##### 2.5a：LAppDelegate 主循环改造

**修改文件**：`renderer/src/LAppDelegate.cpp`

Vulkan 的渲染循环与 GL **完全不同**，无法通过 `IGraphicsBackend` 接口统一。Demo 的 `Run()` 循环（`LAppDelegate.cpp` L184-L209）：

```cpp
// LAppDelegate::Run() 中
while (glfwWindowShouldClose(window) == GL_FALSE && !_isEnd)
{
    glfwPollEvents();
    LAppPal::UpdateTime();

    #ifdef USE_VULKAN
        auto* vkBackend = static_cast<VulkanBackend*>(_graphicsBackend);
        vkBackend->BeginFrame(width, height);  // vkAcquireNextImageKHR
        if (RecreateSwapchain()) continue;
        _view->Render();                        // 所有渲染在此完成
        vkBackend->EndFrame(_windowManager->GetWindow());  // vkQueuePresentKHR
        RecreateSwapchain();
    #else
        _graphicsBackend->BeginFrame(width, height);
        // ... GL 渲染循环（透明检测等）...
        _graphicsBackend->EndFrame(_windowManager->GetWindow());
    #endif
}
```

> **关键差异**：Demo 没有 `glClear/glViewport` 这样的独立清屏步骤——清屏在 `LAppView::BeginRendering()` 中通过 `vkCmdBeginRendering` 的 `clearValue` 完成。GL 版 `BeginFrame()` 中的 `glClear/glViewport` 在 Vulkan 版不存在。

##### 2.5b：LAppView Vulkan 渲染

**修改文件**：`renderer/src/LAppView.hpp/.cpp`

Vulkan 版 `LAppView` 需要新增以下方法（对应 Demo `LAppView.cpp`）：

```cpp
#ifdef USE_VULKAN
    // 动态渲染控制
    void BeginRendering(VkCommandBuffer cmdBuf, float r, float g, float b, float a, bool isClear);
    // → VkRenderingAttachmentInfoKHR + VkRenderingInfo + vkCmdBeginRendering

    void EndRendering(VkCommandBuffer cmdBuf);
    // → vkCmdEndRendering

    void ChangeEndLayout(VkCommandBuffer cmdBuf);
    // → ImageMemoryBarrier: COLOR_ATTACHMENT → PRESENT_SRC_KHR
#endif
```

> **注意**：Demo 的 `InitializeSprite()` / `ResizeSprite()` 不再需要——本项目不使用 Demo 的 UI 精灵。

**Render() 方法的 Vulkan 路径**（基于 Demo `LAppView::Render()` L140-L202，已去除精灵步骤）：

```
1. 清屏 + 开始渲染:
   cmdBuf = vkBackend->BeginSingleTimeCommands()
   BeginRendering(cmdBuf, 0,0,0,0, isClear=true)  // alpha=0 透明背景
   EndRendering(cmdBuf)
   vkBackend->SubmitCommand(cmdBuf, isFirstDraw=true)

2. Cubism 模型渲染:
   live2DManager->OnUpdate()
   // 内部调用 CubismRenderer_Vulkan::DoDrawModel()

3. 最终 layout 转换:
   cmdBuf = vkBackend->BeginSingleTimeCommands()
   ChangeEndLayout(cmdBuf)  // COLOR_ATTACHMENT → PRESENT_SRC_KHR
   vkBackend->SubmitCommand(cmdBuf)
```

##### 2.5c：LAppModel Vulkan 适配

**修改文件**：`renderer/src/LAppModel.hpp/.cpp`

Demo 的 `LAppModel` 与 GL 版的关键差异：

| 差异点 | GL 版 | Vulkan 版（Demo 实现） |
|--------|-------|----------------------|
| `LoadAssets` 签名 | `(dir, fileName)` | `(VkDevice, VkFormat, dir, fileName)` — L66 |
| `CreateRenderer` | `CubismRenderer_OpenGLES2` 工厂 | `CubismRenderer_Vulkan` 工厂 |
| `Draw()` | `GetRenderer<OpenGLES2>()->SetMvpMatrix/DrawModel` — L515-516 | `GetRenderer<Vulkan>()->SetMvpMatrix/DrawModel` — L516-517 |
| `SetupTextures` | `BindTexture(slot, GLuint)` | `BindTexture(CubismImageVulkan&)` — L616 |
| `ReloadRenderer` | `DeleteRenderer → CreateRenderer → SetupTextures` | 新增 `(VkDevice, VkFormat)` 参数 — L579-L586 |
| `GetRenderBuffer` | 无 | 返回 `CubismRenderTarget_Vulkan&` — L634-L637 |

条件编译改造：

```cpp
// LAppModel::LoadAssets
#ifdef USE_VULKAN
void LAppModel::LoadAssets(VkDevice device, VkFormat imageFormat,
                           const csmChar* dir, const csmChar* fileName)
{
    // ... 模型加载 ...
    CreateRenderer(w, h);
    SetupTextures(device, imageFormat);
}
#else
void LAppModel::LoadAssets(const csmChar* dir, const csmChar* fileName)
{
    // ... 原有 GL 逻辑 ...
}
#endif
```

```cpp
// LAppModel::SetupTextures
#ifdef USE_VULKAN
void LAppModel::SetupTextures(VkDevice device, VkFormat surfaceFormat)
{
    for (/* each texture */) {
        auto* texInfo = textureManager->CreateTextureFromPngFile(
            texturePath, surfaceFormat, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            GetRenderer<CubismRenderer_Vulkan>()->GetAnisotropy());

        CubismImageVulkan image;
        if (textureManager->GetTexture(texInfo->id, image)) {
            GetRenderer<CubismRenderer_Vulkan>()->BindTexture(image);
        }
    }
    // IsPremultipliedAlpha 设置
}
#endif
```

##### 2.5d：LAppLive2DManager Vulkan 适配

**修改文件**：`renderer/src/LAppLive2DManager.hpp/.cpp`

Demo 的 `LAppLive2DManager` 与 GL 版的关键差异：

1. **Offscreen 管理器**（Demo L212/L252/L254）：

```cpp
#ifdef USE_VULKAN
    // OnUpdate() 中
    CubismOffscreenManager_Vulkan::GetInstance()->BeginFrameProcess();
    // ... 模型渲染循环 ...
    CubismOffscreenManager_Vulkan::GetInstance()->EndFrameProcess();
    CubismOffscreenManager_Vulkan::GetInstance()->ReleaseStaleRenderTextures();
#endif
```

2. **模型加载**（Demo `ChangeScene()` L282-L292）：需要传递 `VkDevice` 和 `VkFormat`：

```cpp
#ifdef USE_VULKAN
    auto* vkBackend = static_cast<VulkanBackend*>(
        LAppDelegate::GetInstance()->GetGraphicsBackend());
    vkDeviceWaitIdle(vkBackend->GetDevice());
    // ...
    _models[0]->LoadAssets(vkBackend->GetDevice(), vkBackend->GetImageFormat(),
                           modelPath, modelJsonName);
#endif
```

3. **析构**（Demo L78）：`CubismOffscreenManager_Vulkan::ReleaseInstance()`

**验证标准**：
- [ ] 模型加载成功（`LoadAssets` 含 Vulkan 参数）
- [ ] 渲染循环无验证层错误
- [ ] Sprite UI 正确渲染（背景/齿轮/电源按钮可见）
- [ ] Swapchain 重建正确处理
- [ ] 窗口大小调整后渲染目标更新

---

#### Phase 2.6：像素回读（点击穿透检测）

> **目标**：实现 Vulkan 版的像素透明度检测，用于点击穿透功能。

**新建文件**：`renderer/src/graphics/VulkanPixelReadback.hpp/.cpp`

> **关键发现**：Vulkan Demo **没有实现像素回读**。Demo 的点击检测完全通过 Live2D 内置的 `HitTest()` 方法（CPU 侧坐标变换 + drawable 范围判定），不涉及 GPU 像素读取。
>
> 但本项目的桌面宠物需要像素级透明度检测（实现窗口级点击穿透），这是 Demo 没有的需求。

**实现方案**：

```cpp
class VulkanPixelReadback {
public:
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkCommandPool commandPool, VkQueue queue);
    void Cleanup();

    // 从 swapchain image 读取指定像素的 alpha 值
    bool IsPixelTransparent(int x, int y, int windowWidth, int windowHeight,
                            VkImage swapchainImage);

private:
    VkDevice _device;
    VkPhysicalDevice _physicalDevice;
    VkCommandPool _commandPool;
    VkQueue _queue;
    VkBuffer _readbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory _readbackMemory = VK_NULL_HANDLE;
};
```

**回读流程**：
```
1. 创建 host-visible staging buffer（窗口尺寸 × RGBA）
2. vkCmdCopyImage(swapchainImage → stagingBuffer)
3. vkMapMemory → 读取 (x, y) 处的 alpha 值
4. alpha == 0 → 透明，允许穿透
```

> **性能优化**：由于回读需要 GPU→CPU 同步（`vkQueueWaitIdle` 或 fence），不应每帧执行。仅在鼠标点击时触发回读，或使用上帧渲染结果缓存。

**在 `VulkanBackend` 中集成**：

```cpp
bool VulkanBackend::IsPixelTransparent(int x, int y, int windowHeight) override {
    #ifdef USE_VULKAN
    return _pixelReadback.IsPixelTransparent(
        x, y, _swapchainExtent.width, _swapchainExtent.height,
        GetSwapchainImage());
    #endif
}
```

**验证标准**：
- [ ] 点击透明区域 → 鼠标穿透
- [ ] 点击模型区域 → 鼠标不穿透
- [ ] 回读延迟不影响交互体验（< 16ms）

---

#### Phase 2.7：Swapchain 重建与窗口管理适配

> **目标**：处理窗口大小变化、最小化恢复等场景下的 Swapchain 重建。

**修改文件**：
- `renderer/src/LAppDelegate.cpp` — ResizeCallback → Swapchain 重建
- `renderer/src/graphics/VulkanBackend.cpp` — `RecreateSwapchain()` 实现

**Swapchain 重建流程**（对应 Demo `VulkanManager::RecreateSwapchain()` L517-L524）：

```
vkDeviceWaitIdle(_device)
→ CleanupSwapchain()       // 销毁旧 ImageView + Swapchain
→ CreateSwapchain()         // 重新创建 Swapchain + ImageView
→ TransitionSwapchainLayouts()  // UNDEFINED → PRESENT_SRC_KHR
```

**窗口事件回调**（对应 Demo `EventHandler::OnFramebufferResizedCallback` L163-L167）：

```cpp
// GLFW frame buffer size callback（Vulkan 版）
static void OnFramebufferResizedCallback(GLFWwindow* window, int w, int h) {
    auto* backend = static_cast<VulkanBackend*>(
        LAppDelegate::GetInstance()->GetGraphicsBackend());
    backend->SetFrameBufferResized(true);
}
```

> **注意**：GL 版的窗口回调在 `WindowManager` 中处理。Vulkan 版额外需要 `OnFramebufferResizedCallback` 触发 swapchain 重建标记。可通过条件编译在 `WindowManager` 中注册该回调。

**验证标准**：
- [ ] 调整窗口大小后渲染正常恢复
- [ ] Ctrl+滚轮缩放窗口后 Swapchain 正确重建
- [ ] 不出现 `VK_ERROR_OUT_OF_DATE_KHR` 未处理的错误
- [ ] 不出现 `VK_SUBOPTIMAL_KHR` 持续未处理的情况

---

#### Phase 2 文件变更汇总

| 操作 | 文件 | 子阶段 |
|------|------|--------|
| **新建** | `graphics/VulkanBackend.hpp` | 2.1 |
| **新建** | `graphics/VulkanBackend.cpp` | 2.1 |
| **新建** | `graphics/VulkanPixelReadback.hpp` | 2.6 |
| **新建** | `graphics/VulkanPixelReadback.cpp` | 2.6 |
| **修改** | `LAppDelegate.hpp` | 2.2, 2.5a |
| **修改** | `LAppDelegate.cpp` | 2.2, 2.5a, 2.7 |
| **修改** | `LAppTextureManager.hpp` | 2.3 |
| **修改** | `LAppTextureManager.cpp` | 2.3 |
| **修改** | `LAppView.hpp` | 2.5b |
| **修改** | `LAppView.cpp` | 2.5b |
| **修改** | `LAppModel.hpp` | 2.5c |
| **修改** | `LAppModel.cpp` | 2.5c |
| **修改** | `LAppLive2DManager.hpp` | 2.5d |
| **修改** | `LAppLive2DManager.cpp` | 2.5d |
| **修改** | `CMakeLists.txt` | 2.1 |

---

### Phase 3：CMake 构建系统改造

#### 3.1 后端选择选项

```cmake
option(USE_VULKAN "Use Vulkan renderer instead of OpenGL" OFF)

if(USE_VULKAN)
    set(FRAMEWORK_SOURCE Vulkan)
    find_package(Vulkan REQUIRED)
    target_compile_definitions(${APP_NAME} PRIVATE USE_VULKAN)
    # 链接 Vulkan SDK
    target_link_libraries(${APP_NAME} Vulkan::Vulkan)
else()
    set(FRAMEWORK_SOURCE OpenGL)
    find_package(OpenGL REQUIRED)
    target_compile_definitions(${APP_NAME} PRIVATE USE_OPENGL CSM_TARGET_WIN_GL)
    # 链接 GLEW + OpenGL
    target_link_libraries(${APP_NAME} glew_s ${OPENGL_LIBRARIES})
endif()

# GLFW 两个后端都需要
target_link_libraries(${APP_NAME} glfw)
```

#### 3.2 Shader 资源路径

```cmake
if(USE_VULKAN)
    set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/Vulkan/Shaders)
else()
    set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard)
endif()
```

#### 3.3 条件编译源文件

```cmake
# 公共源文件（两个后端共用）
set(COMMON_SOURCES
    src/main.cpp
    src/LAppDelegate.cpp
    src/LAppView.cpp
    src/LAppModel.cpp
    src/LAppTextureManager.cpp
    src/LAppLive2DManager.cpp
    src/LAppPal.cpp
    src/LAppDefine.cpp
    src/platform/WindowManager.cpp
    src/network/*.cpp
    src/AudioManager.cpp
)

target_sources(${APP_NAME} PRIVATE ${COMMON_SOURCES})

if(USE_VULKAN)
    target_sources(${APP_NAME} PRIVATE src/graphics/VulkanBackend.cpp)
else()
    target_sources(${APP_NAME} PRIVATE src/graphics/OpenGLBackend.cpp)
endif()
```

---

## 五、实施优先级

```
Phase 1.3  抽取 WindowManager              ← 纯提取，零风险
    ↓
Phase 1.4  重构 LAppTextureManager          ← 接口变更，中风险
    ↓
Phase 1.2  实现 OpenGLBackend + 接口         ← 迁移 GL 调用，中风险
    ↓
Phase 1.1  精简 LAppDelegate                 ← 依赖前几步完成
    ↓
Phase 1.5  LAppModel 条件编译                ← 低风险
    ↓
Phase 1 清理 去无用 include                 ← 低风险
    ↓
[验证: OpenGL 模式功能与重构前完全一致]
    ↓
Phase 2.1  Vulkan 核心基础设施               ← 移植 VulkanManager + SwapchainManager
    ↓                                           验证: Swapchain 创建/销毁/黑屏 Present
Phase 2.2  CubismRenderer_Vulkan 静态初始化    ← 调用 InitializeConstantSettings
    ↓                                           验证: 无验证层错误
Phase 2.3  Vulkan 纹理管线                    ← staging buffer → image → mipmaps → view + sampler
    ↓                                           验证: 纹理加载释放无错误
Phase 2.4  ~~Vulkan 精灵管线~~                 ← 跳过：本项目不使用 Demo UI 精灵
Phase 2.5  Vulkan 渲染循环集成                ← LAppView/LAppModel/LAppLive2DManager Vulkan 路径
    ↓                                           验证: 模型渲染可见
Phase 2.6  像素回读（点击穿透）                ← 自行实现，Demo 无此功能
    ↓                                           验证: 透明区域穿透
Phase 2.7  Swapchain 重建适配                 ← 窗口 resize 处理
    ↓                                           验证: 窗口缩放后渲染正常
[验证: Vulkan 模式功能与 OpenGL 模式完全一致]
    ↓
Phase 3    CMake 条件编译完善                 ← 低复杂度
```

---

## 六、风险评估

| 风险 | 等级 | 影响 | 缓解措施 |
|------|------|------|----------|
| LAppDelegate 拆分引入 bug | 中 | 窗口/渲染异常 | 每步完成后立即手动验证全部功能 |
| PBO 像素回读抽象后性能下降 | 低 | 点击穿透检测延迟 | 接口零开销（虚调用 vs 直接调用差异可忽略），具体实现与当前代码一致 |
| Vulkan 渲染循环与 GL 差异导致架构不兼容 | 高 | Vulkan 无法集成 | Phase 1 完成后先评估 IGraphicsBackend 接口是否足够支撑 Vulkan，必要时调整 |
| TextureInfo.id 类型变更 | 低 | 纹理绑定异常 | id 仅在渲染内部使用，不涉及网络传输或序列化；改为 uint64_t 向下兼容 GLuint |
| Vulkan Demo 的 VulkanManager 不能直接复用 | 中 | 需要适配 | Demo 的 VulkanManager 包含 swapchain 管理和 debug messenger，需裁剪并适配 IGraphicsBackend 接口 |
| Vulkan Demo 缺少像素回读实现 | 高 | 点击穿透功能无法工作 | Demo 完全没有 GPU 回读代码，需自行实现 `vkCmdCopyImageToBuffer`，性能需评估 |
| Vulkan Demo 使用动态渲染（无 RenderPass/Framebuffer） | 低 | 认知偏差 | 原方案中提到 SwapchainManager 包含 Framebuffer/RenderPass 是错误的，实际使用 `vkCmdBeginRendering` 动态渲染 |

---

## 七、验证标准

每个 Phase 完成后必须验证以下功能：

1. [ ] 模型正常加载和渲染
2. [ ] 窗口透明 + 置顶 + 无边框
3. [ ] 鼠标中键拖拽移动窗口
4. [ ] 点击穿透（透明区域穿透，模型区域不穿透）
5. [ ] Shift+滚轮缩放模型
6. [ ] Shift+拖拽调整模型位置
7. [ ] Ctrl+滚轮调整窗口大小（中心缩放）
8. [ ] WebSocket 连接 + 指令收发正常
9. [ ] 动作/表情播放正常
10. [ ] 音频播放正常
11. [ ] 窗口最小化禁用
12. [ ] 首次启动 5 秒后自动显示窗口
