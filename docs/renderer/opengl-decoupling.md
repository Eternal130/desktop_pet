# 渲染后端解耦方案 — OpenGL → OpenGL/Vulkan 双后端

> **分支**：`refactor/decouple-opengl-renderer`
>
> **目标**：将 `renderer/src/` 中硬编码的 OpenGL 依赖抽取为可替换的渲染后端接口，使项目能够在 OpenGL 和 Vulkan 之间切换，为后续 Vulkan 集成铺平道路。
>
> **相关文档**：[Cubism SDK 集成](./cubism-sdk.md) | [渲染引擎设计](./README.md)

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

#### 步骤 2.1：新建 VulkanBackend

**新建文件**：`renderer/src/graphics/VulkanBackend.hpp/.cpp`

参考 SDK Vulkan Demo（`Samples/Vulkan/Demo/proj.win.cmake/src/`）的架构：
- `VulkanManager` — Instance / PhysicalDevice / Device / Surface / Queue / CommandPool / SyncObjects
- `SwapchainManager` — Swapchain / ImageView / Framebuffer / RenderPass

合并为 `VulkanBackend`，实现 `IGraphicsBackend` 接口：

```cpp
class VulkanBackend : public IGraphicsBackend {
public:
    bool InitializeGraphics(GLFWwindow* window) override;
    // → CreateInstance → SetupDebugMessenger → CreateSurface →
    //   PickPhysicalDevice → CreateLogicalDevice → CreateCommandPool →
    //   CreateSwapchain → CreateSyncObjects →
    //   CubismRenderer_Vulkan::InitializeConstantSettings(...)
    void ReleaseGraphics() override;

    void BeginFrame(int width, int height) override;
    // → AcquireNextImage → BeginCommandBuffer → BeginRenderPass
    void EndFrame(GLFWwindow* window) override;
    // → EndRenderPass → EndCommandBuffer → SubmitCommand → Present

    uint64_t CreateTexture(const void* data, int width, int height, int channels) override;
    // → CreateImage + CreateStagingBuffer + vkCmdCopyBufferToImage + GenerateMipmaps
    void DeleteTexture(uint64_t handle) override;
    // → vkDestroyImage + vkFreeMemory + vkDestroyImageView

    bool IsPixelTransparent(int x, int y, int windowHeight) override;
    // → vkCmdCopyImage to staging buffer → map → read alpha

    // Vulkan 额外接口（GL Backend 不需要，LAppModel 通过条件编译直接调用）
    VkCommandBuffer BeginSingleTimeCommands();
    void SubmitCommand(VkCommandBuffer cmdBuf);
    VkDevice GetDevice() const;
    // ... 其他 VulkanManager/SwapchainManager 的 getter
};
```

#### 步骤 2.2：Vulkan 渲染循环差异处理

Vulkan 的 `CubismRenderer_Vulkan` 需要显式的命令缓冲管理，渲染循环与 GL 完全不同。需要在 `LAppDelegate::Run()` 中条件编译：

```cpp
#ifdef USE_VULKAN
    auto* vkBackend = static_cast<VulkanBackend*>(_graphicsBackend);
    // 获取当前帧的 command buffer
    auto cmdBuf = vkBackend->BeginSingleTimeCommands();
    CubismRenderer_Vulkan::BeginRendering(cmdBuf, false);
    _view->Render();
    CubismRenderer_Vulkan::EndRendering(cmdBuf);
    vkBackend->EndFrame(_windowManager->GetWindow());
    CubismRenderer_Vulkan::PostDraw();
#else
    _graphicsBackend->BeginFrame(width, height);
    _view->Render();
    _graphicsBackend->EndFrame(_windowManager->GetWindow());
#endif
```

#### 步骤 2.3：纹理管理 Vulkan 版

Vulkan 的 `LAppTextureManager::CreateTextureFromPngFile()` 需要额外的 Vulkan 参数。通过条件编译提供两个版本：

```cpp
#ifdef USE_VULKAN
TextureInfo* LAppTextureManager::CreateTextureFromPngFile(
    std::string fileName,
    VkFormat format, VkImageTiling tiling,
    VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
    float anisotropy)
{
    // ... stb_image 解码 ...
    auto* vkBackend = LAppDelegate::GetInstance()->GetVulkanBackend();
    auto handle = vkBackend->CreateTexture(png, width, height, 4);
    // ...
    GetRenderer<CubismRenderer_Vulkan>()->BindTexture(vkImage);
}
#else
TextureInfo* LAppTextureManager::CreateTextureFromPngFile(std::string fileName)
{
    // ... stb_image 解码 ...
    auto handle = _backend->CreateTexture(png, width, height, 4);
    // ...
    GetRenderer<CubismRenderer_OpenGLES2>()->BindTexture(slot, handle);
}
#endif
```

#### Phase 2 文件变更汇总

| 操作 | 文件 | 说明 |
|------|------|------|
| **新建** | `renderer/src/graphics/VulkanBackend.hpp` | VK 后端声明 |
| **新建** | `renderer/src/graphics/VulkanBackend.cpp` | VK 后端实现 |
| **修改** | `renderer/src/LAppDelegate.hpp/cpp` | Vulkan 渲染循环 + VulkanBackend 访问 |
| **修改** | `renderer/src/LAppTextureManager.hpp/cpp` | 添加 Vulkan 版 CreateTextureFromPngFile |
| **修改** | `renderer/src/LAppModel.cpp` | Vulkan 版 Draw/SetupTextures |
| **修改** | `renderer/src/LAppLive2DManager.cpp` | Vulkan 版渲染循环 |

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
Phase 1.3  抽取 WindowManager          ← 纯提取，零风险
    ↓
Phase 1.4  重构 LAppTextureManager      ← 接口变更，中风险
    ↓
Phase 1.2  实现 OpenGLBackend + 接口     ← 迁移 GL 调用，中风险
    ↓
Phase 1.1  精简 LAppDelegate             ← 依赖前几步完成
    ↓
Phase 1.5  LAppModel 条件编译            ← 低风险
    ↓
Phase 1 清理 去无用 include             ← 低风险
    ↓
[验证: OpenGL 模式功能与重构前完全一致]
    ↓
Phase 2    Vulkan 后端实现              ← 高复杂度
    ↓
Phase 3    CMake 条件编译完善            ← 低复杂度
```

---

## 六、风险评估

| 风险 | 等级 | 影响 | 缓解措施 |
|------|------|------|----------|
| LAppDelegate 拆分引入 bug | 中 | 窗口/渲染异常 | 每步完成后立即手动验证全部功能 |
| PBO 像素回读抽象后性能下降 | 低 | 点击穿透检测延迟 | 接口零开销（虚调用 vs 直接调用差异可忽略），具体实现与当前代码一致 |
| Vulkan 渲染循环与 GL 差异导致架构不兼容 | 高 | Vulkan 无法集成 | Phase 1 完成后先评估 IGraphicsBackend 接口是否足够支撑 Vulkan，必要时调整 |
| TextureInfo.id 类型变更 | 低 | 纹理绑定异常 | id 仅在渲染内部使用，不涉及网络传输或序列化；改为 uint64_t 向下兼容 GLuint |
| Vulkan Demo 的 VulkanManager 不能直接复用 | 中 | 需要适配 | Vulkan Demo 的 VulkanManager 包含 swapchain 管理和 debug messenger，需裁剪并适配 IGraphicsBackend 接口 |

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
