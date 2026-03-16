# Cubism SDK 集成架构

> 本节描述 Cubism Native SDK 的内部结构、与渲染引擎各模块的对应关系，以及集成时需关注的关键 API。
> SDK 版本与集成方式详见 [第三方库选型](../engineering/dependencies.md)。
> 渲染引擎模块概览参见 [渲染引擎设计](./README.md)。

---

## 一、SDK 三层结构

Cubism Native SDK 由三层组成，本项目按需集成 Core 和 Framework 两层，不使用 Samples 代码（仅作参考）：

```plain
┌─────────────────────────────────────────────────────────────────┐
│  Samples（示例代码，仅供参考，不编译进本项目）                      │
│  - LAppModel（模型封装参考）、LAppWavFileHandler（WAV 播放参考）    │
│  - LAppAllocator（内存分配器参考实现）                             │
│  - Resources/（内置 8 个免费示例模型：Haru、Hiyori、Mao 等）      │
└─────────────────────────────────────────────────────────────────┘
         ▲ 参考
┌────────┴────────────────────────────────────────────────────────┐
│  Framework（C++ 开源框架，编译为静态库链接）                        │
│  ┌────────────┬────────────┬────────────┬────────────────────┐  │
│  │   Model    │  Motion    │  Effect    │    Rendering       │  │
│  │ 模型生命周期│ 动作/表情   │ 眨眼/呼吸   │  OpenGL 渲染器     │  │
│  ├────────────┼────────────┼────────────┼────────────────────┤  │
│  │  Physics   │    Id      │   Math     │    Utils           │  │
│  │ 物理演算    │ 参数ID管理  │ 矩阵/向量   │  JSON解析/日志     │  │
│  └────────────┴────────────┴────────────┴────────────────────┘  │
└────────┬────────────────────────────────────────────────────────┘
         │ 调用
┌────────┴────────────────────────────────────────────────────────┐
│  Core（闭源预编译库，C 接口）                                      │
│  - Live2DCubismCore.h（唯一头文件，纯 C API）                      │
│  - 负责 .moc3 文件解析、模型参数求值、Drawable 顶点计算             │
│  - 本项目使用 linux/x86_64 静态库（libLive2DCubismCore.a）        │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、Framework 模块与引擎模块映射

| SDK Framework 模块 | 关键类 | 对应引擎模块 | 集成方式 |
|:---|:---|:---|:---|
| **Model** | `CubismUserModel`、`CubismMoc`、`CubismModel` | 模型管理器 (ModelManager) | `Live2DModel` 继承 `CubismUserModel`，封装加载/更新/释放 |
| **Motion** | `CubismMotion`、`CubismMotionManager`、`CubismExpressionMotion` | 动画控制器 (MotionManager) | 使用 `CubismMotionManager` 管理动作队列，`CubismExpressionMotionManager` 管理表情 |
| **Effect** | `CubismEyeBlink`、`CubismBreath`、`CubismPose` | 参数引擎 (ParameterEngine) | 每帧调用 `CubismEyeBlink::UpdateParameters`、`CubismBreath::UpdateParameters` |
| **Physics** | `CubismPhysics` | 参数引擎 (ParameterEngine) | 每帧调用 `CubismPhysics::Evaluate`，驱动头发/饰品的物理摆动 |
| **Rendering** | `CubismRenderer_OpenGLES2` | 渲染模块 (GLRenderer) | 使用 OpenGL ES 2.0 兼容渲染器（桌面 OpenGL 兼容），负责 Drawable 绘制、遮罩裁剪、混合模式 |
| **Id** | `CubismIdManager` | 模型管理器 / 参数引擎 | 管理参数名（`ParamAngleX` 等）和部件名的唯一 ID |
| **Math** | `CubismMatrix44`、`CubismViewMatrix` | 渲染模块 / 交互处理层 | 模型矩阵变换、视口投影、坐标转换 |
| **Utils** | `CubismJson` | 模型管理器 | 解析 `.model3.json`、`.motion3.json`、`.physics3.json` 等模型配置文件 |

---

## 三、SDK 初始化与销毁流程

```plain
引擎启动
   │
   ├─> 1. 实现 ICubismAllocator 接口（自定义内存分配器）
   │      - Allocate()：按对齐要求分配内存（csmAlignofMoc=64, csmAlignofModel=16）
   │      - Deallocate()：释放内存
   │      - AllocateAligned()：对齐分配（用于 moc 和 model 数据）
   │      - DeallocateAligned()：释放对齐内存
   │
    ├─> 2. 调用 CubismFramework::CubismStartUp(&allocator, &logOption)
    │      - 注册内存分配器和日志回调
    │      - 日志回调使用 LAppPal::PrintLogLn（基于 printf 输出到控制台）
   │
   ├─> 3. 调用 CubismFramework::Initialize()
   │      - 初始化 Framework 内部状态（ID 管理器等）
   │
   └─> 4. 记录 SDK 版本信息（日志）
          - csmGetVersion() 返回 Core 版本号
          - 格式：major.minor.patch（如 05.03.0000）

引擎关闭
   │
   ├─> 1. 释放所有 CubismUserModel 实例
   │
   ├─> 2. 调用 CubismFramework::Dispose()
   │      - 释放 Framework 内部资源（ID 管理器等）
   │
   └─> 3. 释放自定义分配器
```

---

## 四、模型加载流程（SDK 层面）

```plain
收到 load_model 指令（model_path = "xxx.model3.json"）
   │
   ├─> 1. 解析 .model3.json（CubismModelSettingJson）
   │      - 获取 moc 文件路径、纹理列表、动作组、表情列表、物理文件、HitArea 等
   │
   ├─> 2. 加载 .moc3 文件 → CubismMoc::Create()
   │      - 内存对齐要求：csmAlignofMoc (64 字节)
   │
   ├─> 3. 创建模型实例 → CubismMoc::CreateModel()
   │      - 内存对齐要求：csmAlignofModel (16 字节)
   │
   ├─> 4. 加载纹理 → OpenGL 纹理绑定
   │      - 使用 stb_image 解码 PNG 纹理文件
   │      - 调用 Renderer::BindTexture() 绑定到渲染器
   │
   ├─> 5. 创建渲染器 → CubismRenderer_OpenGLES2::Create()
   │      - 绑定到模型实例，初始化 OpenGL 资源
   │
   ├─> 6. 加载动作文件（按组预加载）
   │      - .motion3.json → CubismMotion::Create()
   │      - 包含音频路径信息（SoundFile 字段）
   │
   ├─> 7. 加载表情文件
   │      - .exp3.json → CubismExpressionMotion::Create()
   │
   ├─> 8. 加载物理配置
   │      - .physics3.json → CubismPhysics::Create()
   │
   └─> 9. 设置 HitArea（碰撞检测区域）
          - 从 model3.json 的 HitAreas 字段读取
          - 关联到 Drawable ID 用于点击判定
```

---

## 五、渲染主循环中的 SDK 调用顺序

每帧更新需严格按以下顺序调用 SDK API，确保参数叠加正确：

```plain
每帧 Update (deltaTime)
   │
   ├─> 1. CubismMotionManager::UpdateMotion()
   │      - 更新当前动作，混合参数到模型
   │
   ├─> 2. CubismExpressionMotionManager::UpdateMotion()
   │      - 更新表情参数
   │
   ├─> 3. CubismEyeBlink::UpdateParameters()
   │      - 自动眨眼效果
   │
   ├─> 4. CubismBreath::UpdateParameters()
   │      - 自动呼吸效果
   │
   ├─> 5. CubismPhysics::Evaluate()
   │      - 物理演算（头发、饰品摆动等）
   │
   ├─> 6. CubismPose::UpdateParameters()
   │      - 姿势切换（部件可见性过渡）
   │
   ├─> 7. model->Update()
   │      - 提交所有参数变更，Core 重新计算顶点
   │
   └─> 8. CubismRenderer_OpenGLES2::DrawModel()
          - 按 DrawOrder 绘制所有 Drawable
          - 处理遮罩裁剪（ClippingManager）
          - 处理混合模式（Normal / Additive / Multiplicative）
```

---

## 六、关键 API 速览

| 功能 | API | 说明 |
|:---|:---|:---|
| 播放动作 | `CubismMotionManager::StartMotionPriority(motion, priority)` | 按优先级播放，高优先级可中断低优先级 |
| 设置表情 | `CubismExpressionMotionManager::StartMotion(expression)` | 表情叠加在动作之上 |
| 点击检测 | `CubismUserModel::IsHit(hitAreaId, x, y)` | 检测模型坐标 (x,y) 是否命中指定 HitArea |
| 眼球跟踪 | `model->SetParameterValue(idParamEyeBallX, value)` | 直接设置眼球参数值（-1.0 ~ 1.0） |
| 获取动作组 | `CubismModelSettingJson::GetMotionGroupName(index)` | 枚举模型定义的所有动作组名 |
| 获取表情列表 | `CubismModelSettingJson::GetExpressionName(index)` | 枚举所有表情名 |
| 模型矩阵 | `CubismModelMatrix::SetPosition(x, y)` / `SetScale(scale)` | 控制模型在画布中的位置和缩放 |
| 版本查询 | `csmGetVersion()` / `csmGetLatestMocVersion()` | 获取 Core 库版本和支持的最高 moc3 版本 |
