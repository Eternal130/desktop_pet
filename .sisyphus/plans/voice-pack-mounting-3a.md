# Phase 3a — 语音包基础挂载系统

## TL;DR

> **Quick Summary**: 实现语音包与 Live2D 模型的解耦挂载。用户可在控制面板中选择语音包挂载到任意模型上，点击模型交互区域时触发语音包中对应的动作（motion）。
> 
> **Deliverables**:
> - Protobuf 集成 + `bundles.proto` 编译
> - `VoicePackScanner` — 语音包目录扫描
> - `MetaMkoParser` — meta.mko 解析为 `VoicePackInfo` 数据模型
> - `MountConfig` + `MountConfigManager` — 挂载关系持久化
> - C++ `play_motion_ext` 指令 — 从绝对路径播放外部 motion
> - `MountedBehaviorEngine` — 事件分发引擎（hit area → voice pack motion）
> - `AppOrchestrator` 集成 — 启动加载、事件连接
> - UI 扩展 — 语音包选择器 ComboBox
> - 所有新增模块的单元测试
> 
> **Estimated Effort**: Medium-Large
> **Parallel Execution**: YES - 4 waves
> **Critical Path**: T1(protobuf) → T5(parser) → T7(engine) → T8(orchestrator) → T9(UI) → F1-F4

---

## Context

### Original Request
按照 `docs/system/voice-pack-mounting.md` 设计文档，整理 Phase 3a（基础挂载）的开发计划。

### Interview Summary
**Key Discussions**:
- **范围**: 仅 Phase 3a — 基础挂载，无音频(3b)、无口型(3c)、无行为图(3d)
- **测试策略**: Tests-after — 每个模块实现后补充单元测试
- **交付物**: 点击交互 → 播放语音包 motion（无音频）

**Research Findings**:
- **Java 架构**: Maven/Java 21/JavaFX 21.0.5, `AppOrchestrator` 中央编排, `InteractionHandler` 处理 hit 事件, 13个现有测试
- **C++ 架构**: Cubism SDK 5, `CommandHandlers.cpp` 注册模式, `LAppModel` motion 系统, `StartMotion()` 从 `_motions` map 加载
- **Protobuf**: 使用 `protobuf-java:4.29.6` + `ascopes:protobuf-maven-plugin:5.0.2`, .proto 放 `src/main/protobuf/`
- **Hit 事件流**: C++ 发送 `area_id = lowercase(HitArea Name)`，Java 直接接收（如 `tap_head`）— 与语音包事件名天然对齐
- **现有音频**: 仅 WAV (Cubism SDK 自带)，Phase 3a 不涉及音频

### Metis Review
**Identified Gaps** (addressed):
- **Hit 事件 area_id 格式**: 设计文档称 area_id 为 HitArea Id（如 `HitAreaHead`），但实际 C++ 代码发送的是 `lowercase(HitArea Name)`（如 `tap_head`）。实际流程更简单 — area_id 直接对应语音包 group code
- **JPMS 兼容性**: 需更新 `module-info.java` 添加 `requires com.google.protobuf`，proto `java_package` 须改为 `com.desktoppet.bundle`
- **Idle motion 不在范围内**: Phase 3a 仅覆盖点击交互，Scheduler idle motion 替换推迟到后续阶段
- **eventOverrides 不在范围内**: 直接 area_id → group code 映射，非对齐区域静默跳过

---

## Work Objectives

### Core Objective
实现语音包基础挂载系统：用户选择模型+语音包组合，点击模型 HitArea 时从挂载的语音包中查找对应事件组，随机选取一条 action，将其 motion 文件通过 `play_motion_ext` 指令发送到 Renderer 播放。

### Concrete Deliverables
- `controller/src/main/protobuf/bundles.proto` — Protobuf schema 定义
- `controller/pom.xml` — 新增 protobuf 依赖和编译插件
- `controller/src/main/java/module-info.java` — 更新 JPMS 配置
- `controller/src/main/java/com/desktoppet/core/VoicePackScanner.java`
- `controller/src/main/java/com/desktoppet/model/VoicePackInfo.java` (含 VoicePackGroup, VoicePackAction, VoicePackModule)
- `controller/src/main/java/com/desktoppet/model/MountConfig.java`
- `controller/src/main/java/com/desktoppet/core/MetaMkoParser.java`
- `controller/src/main/java/com/desktoppet/core/MountConfigManager.java`
- `controller/src/main/java/com/desktoppet/core/MountedBehaviorEngine.java`
- `renderer/src/network/CommandHandlers.cpp` — 新增 `play_motion_ext` 处理器
- `renderer/src/LAppModel.hpp/cpp` — 新增 `StartMotionFromFile()` 方法
- UI FXML/Controller 变更 — 语音包选择器
- 所有新增类的单元测试

### Definition of Done
- [ ] `mvn compile` 成功（protobuf 生成 Java 类）
- [ ] `mvn test` 通过（所有 13 个现有测试 + 新增测试均通过）
- [ ] `mvn package` 生成可运行的 fat JAR
- [ ] 挂载配置正确持久化到 `~/.config/desktop-pet/mount.json`
- [ ] 点击已挂载模型的 HitArea → Renderer 接收并播放 `play_motion_ext` 指令
- [ ] 未挂载语音包的模型行为完全不变

### Must Have
- Protobuf 3 解析 `meta.mko` 得到完整 Bundle 数据
- 语音包扫描正确识别含 `meta.mko` 的目录
- 挂载配置持久化并跨重启保留
- `play_motion_ext` 从绝对路径加载并播放 motion
- `play_motion_ext` 发出 `motion_started` 和 `motion_finished` 事件
- Hit 事件到语音包 motion 的完整链路
- 所有新增 Java 类有对应测试文件

### Must NOT Have (Guardrails)
- ❌ 不实现 `play_audio` 命令或任何音频代码（Phase 3b）
- ❌ 不实现 `LipSyncDriver` 或 `set_parameter` 命令（Phase 3c）
- ❌ 不实现文案气泡 UI（Phase 3c）
- ❌ 不解析 `.mkai` 文件或实现 `GraphRuntime`（Phase 3d）
- ❌ 不实现 `MountConfig.eventOverrides` 映射（简化为直接映射）
- ❌ 不实现 C++ 端 motion 缓存（LRU）（性能优化推迟）
- ❌ 不修改 Scheduler idle motion 行为（Phase 3a 仅点击交互）
- ❌ 不将 protobuf 生成代码提交到 git（`target/generated-sources/` 在 `.gitignore`）
- ❌ 不超出设计文档定义的 Phase 3a 6 个步骤
- ❌ 不使用 `protobuf-javalite`（桌面应用使用完整版 `protobuf-java`）
- ❌ 不使用 `com.mimikko.app.lib.bundle` 包名（改为 `com.desktoppet.bundle`）

---

## Verification Strategy (MANDATORY)

> **ZERO HUMAN INTERVENTION** — ALL verification is agent-executed. No exceptions.

### Test Decision
- **Infrastructure exists**: YES
- **Automated tests**: Tests-after
- **Framework**: JUnit 5 (5.11.4) + Mockito (5.14.2) + TestFX (4.0.18, headless Monocle)
- **Test pattern**: Follow existing tests — `@TempDir` for文件系统测试，`ArgumentCaptor<String>` + `Protocol.deserialize()` for 命令验证

### QA Policy
Every task MUST include agent-executed QA scenarios.
Evidence saved to `.sisyphus/evidence/task-{N}-{scenario-slug}.{ext}`.

- **Java 模块**: Use Bash (`mvn test -pl controller`) — 编译、运行测试、验证输出
- **C++ 命令**: Use Bash (`cmake --build` + 手动 WebSocket 测试脚本) — 编译、发送命令、验证事件
- **UI 组件**: Use Bash (`mvn test -pl controller -Dtest=VoicePackUITest`) — TestFX headless 测试
- **集成测试**: Use Bash — 启动应用、发送 hit 事件、验证 play_motion_ext 指令

---

## Execution Strategy

### Parallel Execution Waves

```
Wave 1 (Start Immediately — foundation, all independent):
├── Task 1: Protobuf Maven 集成 (pom.xml + bundles.proto + module-info.java) [quick]
├── Task 2: 语音包数据模型 records (VoicePackInfo/Group/Action/Module + MountConfig) [quick]
├── Task 3: play_motion_ext C++ 指令实现 [unspecified-high]
└── Task 4: VoicePackScanner + VoicePackScannerTest [quick]

Wave 2 (After Wave 1 — core parsing & persistence):
├── Task 5: MetaMkoParser + MetaMkoParserTest (depends: T1, T2) [unspecified-high]
├── Task 6: MountConfigManager + MountConfigManagerTest (depends: T2) [quick]
└── Task 7: MountedBehaviorEngine + MountedBehaviorEngineTest (depends: T2) [deep]

Wave 3 (After Wave 2 — integration & UI):
├── Task 8: AppOrchestrator 集成接线 (depends: T4, T5, T6, T7) [unspecified-high]
└── Task 9: UI 语音包选择器扩展 (depends: T4, T6, T8) [visual-engineering]

Wave FINAL (After ALL tasks — 4 parallel reviews, then user okay):
├── Task F1: Plan compliance audit (oracle)
├── Task F2: Code quality review (unspecified-high)
├── Task F3: Real manual QA (unspecified-high)
└── Task F4: Scope fidelity check (deep)
-> Present results -> Get explicit user okay
```

**Critical Path**: T1 → T5 → T7 → T8 → T9 → F1-F4 → user okay
**Parallel Speedup**: ~55% faster than sequential
**Max Concurrent**: 4 (Wave 1)

### Dependency Matrix

| Task | Depends On | Blocks | Wave |
|------|-----------|--------|------|
| T1 | — | T5 | 1 |
| T2 | — | T5, T6, T7 | 1 |
| T3 | — | (C++ side ready for T8 integration test) | 1 |
| T4 | — | T8, T9 | 1 |
| T5 | T1, T2 | T8 | 2 |
| T6 | T2 | T8, T9 | 2 |
| T7 | T2 | T8 | 2 |
| T8 | T4, T5, T6, T7 | T9 | 3 |
| T9 | T4, T6, T8 | — | 3 |

### Agent Dispatch Summary

- **Wave 1**: **4 tasks** — T1 → `quick`, T2 → `quick`, T3 → `unspecified-high`, T4 → `quick`
- **Wave 2**: **3 tasks** — T5 → `unspecified-high`, T6 → `quick`, T7 → `deep`
- **Wave 3**: **2 tasks** — T8 → `unspecified-high`, T9 → `visual-engineering`
- **FINAL**: **4 tasks** — F1 → `oracle`, F2 → `unspecified-high`, F3 → `unspecified-high`, F4 → `deep`

---

## TODOs

- [x] 1. Protobuf Maven 集成 — pom.xml + bundles.proto + module-info.java

  **What to do**:
  - 在 `controller/pom.xml` 中添加 `protobuf-java:4.29.6` 依赖和 `io.github.ascopes:protobuf-maven-plugin:5.0.2` 插件配置
  - 创建 `controller/src/main/protobuf/bundles.proto`，内容来自 `docs/system/voice-pack-mounting.md` 第八章 8.1 节的 proto schema
  - **关键修改**: `option java_package` 从 `"com.mimikko.app.lib.bundle"` 改为 `"com.desktoppet.bundle"`
  - 更新 `controller/src/main/java/module-info.java`，添加 `requires com.google.protobuf;`
  - 确认 `.gitignore` 包含 `target/generated-sources/`（不提交生成代码）
  - 运行 `mvn compile` 验证 protobuf 编译成功，生成 `Bundle.java`、`Action.java`、`ActionGroup.java` 等

  **Must NOT do**:
  - 不添加 `protobuf-java-util`（Phase 3a 不需要 JSON 格式化）
  - 不添加 `protobuf-javalite`（桌面应用使用完整版）
  - 不手动运行 `protoc`（插件自动处理）
  - 不将生成的 Java 文件提交到 git

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 仅涉及配置文件修改（pom.xml, proto, module-info），无复杂逻辑
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 2, 3, 4)
  - **Blocks**: Task 5 (MetaMkoParser 需要 protobuf 生成类)
  - **Blocked By**: None (can start immediately)

  **References**:

  **Pattern References**:
  - `controller/pom.xml:24-88` — 现有依赖结构和 shade plugin 配置，新依赖添加在 `</dependencies>` 前，插件添加在 `<plugins>` 内
  - `controller/src/main/java/module-info.java` — 现有 JPMS 模块声明，需添加 protobuf requires 行

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:762-854` — 完整 protobuf schema (bundles.proto) 源码

  **External References**:
  - Maven 插件文档: ascopes protobuf-maven-plugin v5.0.2 — `.proto` 放 `src/main/protobuf/`，生成到 `target/generated-sources/protobuf/`
  - protobuf-java:4.29.6 — Maven Central groupId `com.google.protobuf`，artifactId `protobuf-java`

  **WHY Each Reference Matters**:
  - pom.xml 结构决定了新依赖的插入位置，shade plugin 配置决定了 protobuf 类是否正确包含在 fat JAR 中
  - module-info.java 必须声明 protobuf 模块，否则 JPMS 编译失败
  - 设计文档的 proto schema 是唯一权威来源，必须完整复制（仅改 java_package）

  **Acceptance Criteria**:

  - [ ] `controller/src/main/protobuf/bundles.proto` 存在且 `java_package` 为 `com.desktoppet.bundle`
  - [ ] `controller/pom.xml` 包含 `protobuf-java:4.29.6` 依赖
  - [ ] `controller/pom.xml` 包含 `protobuf-maven-plugin:5.0.2` 插件
  - [ ] `mvn compile -pl controller` → BUILD SUCCESS
  - [ ] `target/generated-sources/protobuf/` 下存在 `com/desktoppet/bundle/Bundle.java`
  - [ ] 现有 13 个测试仍通过: `mvn test -pl controller`

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Protobuf compilation succeeds
    Tool: Bash
    Preconditions: clean workspace (mvn clean)
    Steps:
      1. Run `cd controller && mvn clean compile`
      2. Check exit code is 0
      3. Verify generated file: `ls target/generated-sources/protobuf/com/desktoppet/bundle/Bundle.java`
      4. Verify file is non-empty: `wc -l target/generated-sources/protobuf/com/desktoppet/bundle/Bundle.java`
    Expected Result: Compile succeeds, Bundle.java exists with >100 lines
    Failure Indicators: BUILD FAILURE, missing Bundle.java, protoc download failure
    Evidence: .sisyphus/evidence/task-1-protobuf-compile.txt

  Scenario: Existing tests still pass after protobuf integration
    Tool: Bash
    Preconditions: protobuf compile succeeded
    Steps:
      1. Run `cd controller && mvn test`
      2. Parse output for test results
    Expected Result: All 13 existing tests pass, 0 failures
    Failure Indicators: Any test failure, compilation error in test phase
    Evidence: .sisyphus/evidence/task-1-existing-tests.txt

  Scenario: Fat JAR packages correctly with protobuf classes
    Tool: Bash
    Preconditions: compile and test succeeded
    Steps:
      1. Run `cd controller && mvn package -DskipTests`
      2. Verify JAR contains protobuf classes: `jar tf ../build/bin/desktop-pet-controller.jar | grep "desktoppet/bundle/Bundle.class"`
    Expected Result: Bundle.class found in fat JAR
    Failure Indicators: Missing class in JAR, shade plugin error
    Evidence: .sisyphus/evidence/task-1-fat-jar.txt
  ```

  **Commit**: YES — commit 1
  - Message: `feat(voice-pack): add protobuf dependency and bundles.proto schema`
  - Files: `controller/pom.xml`, `controller/src/main/java/module-info.java`, `controller/src/main/protobuf/bundles.proto`
  - Pre-commit: `cd controller && mvn test`

---

- [x] 2. 语音包数据模型 Records — VoicePackInfo, VoicePackGroup, VoicePackAction, VoicePackModule, MountConfig

  **What to do**:
  - 创建 `controller/src/main/java/com/desktoppet/model/VoicePackInfo.java`:
    ```java
    public record VoicePackInfo(
        String dirName,           // 语音包目录名
        String displayName,       // 显示名 (Meta.name)
        String code,              // 包标识码 (Meta.code)
        Path basePath,            // 语音包绝对路径
        Map<String, VoicePackGroup> groups,  // 事件名 → 分组
        List<VoicePackModule> modules        // 行为模块列表
    ) {}
    ```
  - 在同一文件或独立文件中定义 `VoicePackGroup`、`VoicePackAction`、`VoicePackModule` records（参照设计文档 4.4 节）
  - 创建 `controller/src/main/java/com/desktoppet/model/MountConfig.java`:
    ```java
    public record MountConfig(
        String modelName,         // 模型目录名
        String voicePackName      // 语音包目录名, null = 未挂载
    ) {}
    ```
  - **注意**: Phase 3a 不实现 `eventOverrides` 字段（简化为直接映射）
  - 确保 `module-info.java` 已 exports `com.desktoppet.model` 包（现有代码已 exports）

  **Must NOT do**:
  - 不实现 `eventOverrides` 字段（deferred）
  - 不添加验证逻辑或构建器（records 保持简单 DTO）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 纯数据定义，无复杂逻辑，仅 Java record 声明
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 3, 4)
  - **Blocks**: Tasks 5, 6, 7 (所有使用这些 records 的模块)
  - **Blocked By**: None

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/model/ModelInfo.java` — 现有模型元数据 record 模式（字段命名风格、import 模式）
  - `controller/src/main/java/com/desktoppet/model/HitAction.java` — 简单 record 定义参考
  - `controller/src/main/java/com/desktoppet/model/ModelConfig.java` — 使用 Map 和 List 的 record 参考

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:364-395` — VoicePackInfo, VoicePackGroup, VoicePackAction, VoicePackModule 完整字段定义
  - `docs/system/voice-pack-mounting.md:400-405` — MountConfig 字段定义（Phase 3a 简化版：不含 eventOverrides）

  **WHY Each Reference Matters**:
  - ModelInfo/HitAction/ModelConfig 展示了项目中 record 的命名习惯和字段风格
  - 设计文档的字段定义是唯一权威来源，record 字段必须与之一致

  **Acceptance Criteria**:

  - [ ] `VoicePackInfo.java` 存在，包含 dirName, displayName, code, basePath, groups, modules 字段
  - [ ] `VoicePackGroup.java` 存在，包含 code, name, priority, actions 字段
  - [ ] `VoicePackAction.java` 存在，包含 id, motionPath, audioPath, lipSyncPath, doc, fadeInMs, fadeOutMs 字段
  - [ ] `VoicePackModule.java` 存在，包含 key, priority, filePath 字段
  - [ ] `MountConfig.java` 存在，包含 modelName, voicePackName 字段（无 eventOverrides）
  - [ ] `mvn compile -pl controller` → BUILD SUCCESS

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Data model records compile correctly
    Tool: Bash
    Preconditions: Wave 1 protobuf setup complete or parallel
    Steps:
      1. Run `cd controller && mvn compile`
      2. Verify no compilation errors
      3. Verify class files exist: `find target/classes -name "VoicePackInfo.class"`
    Expected Result: All record classes compile without errors
    Failure Indicators: Compilation errors, missing imports
    Evidence: .sisyphus/evidence/task-2-records-compile.txt

  Scenario: Records are valid Java records with expected fields
    Tool: Bash
    Preconditions: Compilation succeeded
    Steps:
      1. Use javap to inspect record: `javap -p target/classes/com/desktoppet/model/VoicePackInfo.class`
      2. Verify fields: dirName, displayName, code, basePath, groups, modules
    Expected Result: All 6 fields present as record components
    Failure Indicators: Missing fields, wrong types
    Evidence: .sisyphus/evidence/task-2-records-inspect.txt
  ```

  **Commit**: YES — commit 2
  - Message: `feat(voice-pack): add voice pack data model records`
  - Files: `controller/src/main/java/com/desktoppet/model/VoicePackInfo.java`, `MountConfig.java`, etc.
  - Pre-commit: `cd controller && mvn compile`

- [x] 3. play_motion_ext C++ 指令実装

  **What to do**:
  - 在 `renderer/src/network/CommandHandlers.cpp` 中注册新命令 `play_motion_ext`
  - 命令 payload: `{ "motion_path": "/absolute/path/to/motion3.json", "priority": 2, "fade_in": 1.0, "fade_out": 1.0 }`
  - 在 `renderer/src/LAppModel.hpp/cpp` 中新增 `StartMotionFromFile(const std::string& filePath, int priority, float fadeIn, float fadeOut)` 方法:
    1. 使用 `LAppPal::CreateBuffer(filePath)` 读取 motion3.json 文件内容
    2. 使用 `CubismMotion::Create(buffer, size)` 创建 motion 对象
    3. 设置 fade-in/fade-out: `motion->SetFadeInTime(fadeIn)` / `motion->SetFadeOutTime(fadeOut)`
    4. 设置 eye blink 和 lip sync 效果 IDs: `SetEffectIds()`
    5. 使用 `_motionManager->StartMotionPriority(motion, false, priority)` 播放
    6. 释放 buffer
  - 发出 `motion_started` 事件（motion_path 作为标识）和注册 `motion_finished` 回调
  - 验证: 文件路径不存在时返回 error response（error_code + error_message），不崩溃
  - **关键**: `StartMotionFromFile` 的 autoDelete 参数设为 `false`——外部 motion 不自动删除，由 caller 管理生命周期

  **Must NOT do**:
  - 不实现 LRU motion 缓存（Phase 3a 每次从磁盘加载）
  - 不修改现有 `play_motion` 命令
  - 不添加音频播放逻辑

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: C++ 编程涉及 Cubism SDK API 调用、内存管理、回调机制
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 4)
  - **Blocks**: Task 8 (AppOrchestrator 集成测试需要 C++ 端就绪)
  - **Blocked By**: None (C++ 独立代码库)

  **References**:

  **Pattern References**:
  - `renderer/src/network/CommandHandlers.cpp:83-101` — 现有 `play_motion` 命令处理器，展示了参数提取、Manager 调用、事件发射模式。`play_motion_ext` 应遵循相同模式
  - `renderer/src/LAppModel.cpp:413-473` — `StartMotion()` 方法，展示了 motion 加载 (`LoadMotion`)、`SetEffectIds`、`_motionManager->StartMotionPriority` 的完整流程。`StartMotionFromFile` 需复用此逻辑但从绝对路径加载
  - `renderer/src/LAppModel.cpp:238-272` — `PreloadMotionGroup()` 展示了 `CreateBuffer` + `LoadMotion` 的文件读取和 motion 创建模式
  - `renderer/src/network/CommandHandlers.cpp:39-82` — `load_model` 命令展示了路径验证（`stat()` 检查文件存在性）和错误响应模式

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:486-518` — `play_motion_ext` 完整指令定义（payload 字段、类型、默认值）
  - `docs/protocol/commands.md` — 现有命令协议格式参考
  - `renderer/src/network/Protocol.hpp:11-16` — `Envelope` 结构体（type, action, id, payload, timestamp）
  - `renderer/src/network/EventEmitter.cpp` — `emit()` 方法签名和用法

  **WHY Each Reference Matters**:
  - CommandHandlers.cpp play_motion 是最近的模式参考——play_motion_ext 几乎是它的变体
  - StartMotion() 展示了完整的 motion 生命周期——从文件读取到播放到回调
  - 设计文档定义了指令格式和默认值

  **Acceptance Criteria**:

  - [ ] `play_motion_ext` 命令在 CommandHandlers.cpp 中注册
  - [ ] `LAppModel::StartMotionFromFile()` 方法实现
  - [ ] 有效路径 → motion 播放 + `motion_started` 事件发出
  - [ ] motion 播放结束 → `motion_finished` 事件发出
  - [ ] 无效路径 → error response (success=false, error_code, error_message)，不崩溃
  - [ ] `fade_in` / `fade_out` 参数从 payload 读取并应用
  - [ ] C++ 项目编译成功（CMake build）

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: play_motion_ext with valid motion file
    Tool: Bash
    Preconditions: Renderer running, model loaded, valid motion3.json exists on disk
    Steps:
      1. Connect to ws://localhost:9000 via wscat or curl
      2. Send command: {"type":"command","action":"play_motion_ext","id":"test-1","payload":{"motion_path":"<absolute-path>/motions/idle01.motion3.json","priority":2,"fade_in":0.5,"fade_out":0.5},"timestamp":0}
      3. Wait for response and events (2s timeout)
    Expected Result: Response with success=true, then motion_started event, then motion_finished event
    Failure Indicators: Error response, crash, no events
    Evidence: .sisyphus/evidence/task-3-valid-motion.txt

  Scenario: play_motion_ext with non-existent path
    Tool: Bash
    Preconditions: Renderer running, model loaded
    Steps:
      1. Send command with non-existent path: {"type":"command","action":"play_motion_ext","id":"test-2","payload":{"motion_path":"/nonexistent/path.motion3.json","priority":2},"timestamp":0}
      2. Wait for response
    Expected Result: Response with success=false, error_code set, error_message describes file not found
    Failure Indicators: Crash, hang, success=true with no motion played
    Evidence: .sisyphus/evidence/task-3-invalid-path.txt
  ```

  **Commit**: YES — commit 3
  - Message: `feat(voice-pack): add play_motion_ext renderer command`
  - Files: `renderer/src/network/CommandHandlers.cpp`, `renderer/src/LAppModel.hpp`, `renderer/src/LAppModel.cpp`
  - Pre-commit: CMake build succeeds

---

- [x] 4. VoicePackScanner — 语音包扫描 + VoicePackScannerTest

  **What to do**:
  - 创建 `controller/src/main/java/com/desktoppet/core/VoicePackScanner.java`:
    ```java
    public final class VoicePackScanner {
        /** 扫描 Resources 目录，返回包含 meta.mko 的子目录名列表 */
        public static List<String> scanAvailableVoicePacks(Path resourcesDir) { ... }
    }
    ```
  - 遍历 `resourcesDir` 下所有一级子目录
  - 过滤条件: 子目录中存在 `meta.mko` 文件
  - 跳过隐藏目录（以 `.` 开头）
  - 返回排序后的目录名列表（`List<String>`）
  - 处理 `resourcesDir` 不存在或不是目录的情况（返回空列表）
  - 创建 `controller/src/test/java/com/desktoppet/core/VoicePackScannerTest.java`:
    - 使用 `@TempDir` 创建临时目录结构
    - 测试用例: 含 meta.mko 的目录被识别、不含 meta.mko 的目录被排除、空目录返回空列表、隐藏目录被跳过

  **Must NOT do**:
  - 不解析 meta.mko 内容（仅检测文件存在）
  - 不递归扫描子目录的子目录
  - 不返回 VoicePackInfo 对象（解析由 MetaMkoParser 负责）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 简单文件系统操作，与现有 ModelScanner 高度相似
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 1 (with Tasks 1, 2, 3)
  - **Blocks**: Tasks 8, 9 (AppOrchestrator 和 UI 需要扫描结果)
  - **Blocked By**: None

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/core/ModelScanner.java` — 现有模型扫描器，展示了 `static` 工具类模式、`Path` 参数、目录遍历、文件名过滤。VoicePackScanner 应完全复用此模式
  - `controller/src/test/java/com/desktoppet/core/ModelScannerTest.java` — 现有测试，展示了 `@TempDir` 使用、目录结构搭建、断言模式

  **WHY Each Reference Matters**:
  - ModelScanner 是最直接的模式参考 — VoicePackScanner 本质上是 "以 meta.mko 替代 .model3.json 作为识别标志的 ModelScanner"
  - ModelScannerTest 展示了项目中文件系统测试的标准模式

  **Acceptance Criteria**:

  - [ ] `VoicePackScanner.java` 存在，包含 `scanAvailableVoicePacks(Path)` 静态方法
  - [ ] 含 `meta.mko` 的目录被正确识别
  - [ ] 不含 `meta.mko` 的目录被排除（不报错）
  - [ ] 空 Resources 目录 → 返回空列表
  - [ ] 不存在的 Resources 目录 → 返回空列表（不抛异常）
  - [ ] `VoicePackScannerTest.java` 存在，包含至少 4 个测试用例
  - [ ] `mvn test -pl controller -Dtest=VoicePackScannerTest` → ALL PASS

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Scanner identifies voice pack directories
    Tool: Bash
    Preconditions: Tests written with @TempDir fixtures
    Steps:
      1. Run `cd controller && mvn test -Dtest=VoicePackScannerTest`
      2. Check all test cases pass
    Expected Result: 4+ tests pass, 0 failures
    Failure Indicators: Any test failure
    Evidence: .sisyphus/evidence/task-4-scanner-tests.txt

  Scenario: Scanner handles edge cases
    Tool: Bash
    Preconditions: Tests include edge cases (empty dir, non-existent dir, hidden dirs)
    Steps:
      1. Verify test methods exist for edge cases: grep -c "void.*empty\|void.*nonExist\|void.*hidden" VoicePackScannerTest.java
      2. Run tests
    Expected Result: Edge case tests exist and pass
    Failure Indicators: Missing edge case tests
    Evidence: .sisyphus/evidence/task-4-scanner-edge-cases.txt
  ```

  **Commit**: YES — commit 4
  - Message: `feat(voice-pack): add VoicePackScanner with tests`
  - Files: `controller/src/main/java/com/desktoppet/core/VoicePackScanner.java`, `controller/src/test/java/com/desktoppet/core/VoicePackScannerTest.java`
  - Pre-commit: `cd controller && mvn test`

- [x] 5. MetaMkoParser — meta.mko 解析器 + MetaMkoParserTest

  **What to do**:
  - 创建 `controller/src/main/java/com/desktoppet/core/MetaMkoParser.java`:
    ```java
    public final class MetaMkoParser {
        /**
         * 解析 meta.mko 文件，返回 VoicePackInfo。
         * @param voicePackDir 语音包目录路径（包含 meta.mko 的目录）
         * @return VoicePackInfo 或 null（解析失败时）
         */
        public static VoicePackInfo parse(Path voicePackDir) { ... }
    }
    ```
  - 实现逻辑:
    1. 读取 `voicePackDir/meta.mko` 为 byte 数组
    2. 调用 `Bundle.parseFrom(bytes)` 反序列化
    3. 从 `Bundle.getMeta()` 提取 name, code
    4. 遍历 `Bundle.getGroupsList()` 构建 `Map<String, VoicePackGroup>`
    5. 遍历 `Bundle.getActionsList()` 按 `Action.getGroup()` 分组，填充到对应 VoicePackGroup 的 actions 列表
    6. 将 `Action` 字段映射到 `VoicePackAction` record:
       - `motion` → `motionPath`（相对路径）
       - `audio` → `audioPath`（相对路径）
       - `lipSync` → `lipSyncPath`（可为 null）
       - `doc` → `doc`
       - `fadeIn` → `fadeInMs`（int64 毫秒）
       - `fadeOut` → `fadeOutMs`（int64 毫秒）
    7. 遍历 `Bundle.getModulesList()` 构建 `List<VoicePackModule>`
    8. 构造并返回 `VoicePackInfo` record
  - 异常处理: `InvalidProtocolBufferException` → log warning + return null
  - 创建 `controller/src/test/java/com/desktoppet/core/MetaMkoParserTest.java`:
    - 需要一个真实的 `meta.mko` 文件作为测试 fixture（放 `src/test/resources/voice-pack-fixture/meta.mko`）
    - 测试: 解析成功返回非 null VoicePackInfo、groups count > 0、tap_head group 存在且有 actions、corrupt 文件返回 null

  **Must NOT do**:
  - 不解析 `.mkai` 模块文件内容（仅记录路径）
  - 不验证 motion/audio 文件是否存在（解析时不做 I/O）
  - 不添加缓存

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 涉及 protobuf API 使用、数据结构映射、异常处理
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 6, 7)
  - **Blocks**: Task 8 (AppOrchestrator 需要解析结果)
  - **Blocked By**: Task 1 (protobuf 生成类), Task 2 (VoicePackInfo records)

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/core/ModelInfoParser.java` — 现有模型解析器，展示了 `static parse(Path)` 方法模式、异常处理、返回值构造
  - `controller/src/test/java/com/desktoppet/core/ModelInfoParserTest.java` — 测试模式参考

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:856-889` — Bundle 数据关系图，展示了 Meta → groups → actions 的层级结构
  - `docs/system/voice-pack-mounting.md:893-919` — 解析实现示例代码（Bundle.parseFrom, getGroupsList, getActionsList）
  - `docs/system/voice-pack-mounting.md:137-143` — Action 三种形态（motion+audio+lipSync+doc, motion+audio+doc, motion only）
  - protobuf-java API: `Bundle.parseFrom(byte[])`, `Bundle.getGroupsList()`, `Bundle.getActionsList()`, `Action.getGroup()`, `Action.getMotion()`, `Action.hasLipSync()`

  **WHY Each Reference Matters**:
  - ModelInfoParser 是最直接的模式参考 — 静态解析方法、Path 参数、返回 record
  - 数据关系图展示了 groups 和 actions 的 1:N 关系，Parser 需要按 group code 聚合 actions
  - Action 三种形态说明了 lipSync 可能为空、audio 可能为空，Parser 需正确处理

  **Acceptance Criteria**:

  - [ ] `MetaMkoParser.java` 存在，包含 `parse(Path)` 静态方法
  - [ ] 解析真实 `meta.mko` → 返回非 null VoicePackInfo
  - [ ] `voicePackInfo.displayName()` 非空
  - [ ] `voicePackInfo.groups().size()` > 0（预期 88 个）
  - [ ] `voicePackInfo.groups().get("tap_head")` 存在且 actions 数量 > 0（预期 4 个）
  - [ ] Action 中 motionPath 为相对路径（如 `"motions/33.motion3.json"`）
  - [ ] 损坏的 meta.mko → 返回 null（不抛异常）
  - [ ] `MetaMkoParserTest.java` 存在，包含至少 4 个测试用例
  - [ ] `mvn test -pl controller -Dtest=MetaMkoParserTest` → ALL PASS

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Parse real meta.mko fixture successfully
    Tool: Bash
    Preconditions: meta.mko fixture file exists at src/test/resources/voice-pack-fixture/meta.mko
    Steps:
      1. Run `cd controller && mvn test -Dtest=MetaMkoParserTest`
      2. Check all tests pass
    Expected Result: All tests pass, including: non-null result, correct group count, tap_head exists with actions
    Failure Indicators: InvalidProtocolBufferException, null result, wrong group count
    Evidence: .sisyphus/evidence/task-5-parser-tests.txt

  Scenario: Corrupt meta.mko returns null gracefully
    Tool: Bash
    Preconditions: Test includes corrupt file case
    Steps:
      1. Verify test method: grep "corrupt\|invalid\|malformed" MetaMkoParserTest.java
      2. Run test
    Expected Result: Test passes — corrupt file returns null without exception
    Failure Indicators: Unhandled exception, test failure
    Evidence: .sisyphus/evidence/task-5-parser-corrupt.txt
  ```

  **Commit**: YES — commit 5
  - Message: `feat(voice-pack): add MetaMkoParser with tests`
  - Files: `controller/src/main/java/com/desktoppet/core/MetaMkoParser.java`, `controller/src/test/java/com/desktoppet/core/MetaMkoParserTest.java`, `controller/src/test/resources/voice-pack-fixture/meta.mko`
  - Pre-commit: `cd controller && mvn test`

---

- [x] 6. MountConfigManager — 挂载配置持久化 + MountConfigManagerTest

  **What to do**:
  - 创建 `controller/src/main/java/com/desktoppet/core/MountConfigManager.java`:
    ```java
    public class MountConfigManager {
        private static final Path CONFIG_PATH = 
            Path.of(System.getProperty("user.home"), ".config", "desktop-pet", "mount.json");
        
        /** 加载指定模型的挂载配置。文件不存在或无配置时返回 null voicePackName */
        public MountConfig loadForModel(String modelName) { ... }
        
        /** 保存模型的挂载配置 */
        public void saveForModel(MountConfig config) { ... }
        
        /** 获取所有已配置的挂载关系 */
        public Map<String, MountConfig> loadAll() { ... }
    }
    ```
  - 配置文件格式 (`mount.json`):
    ```json
    {
      "mounts": {
        "giwa-idol2023": {
          "voice_pack": "锦瑟-锦瑟-中文-voice"
        }
      }
    }
    ```
  - 使用 Gson 进行 JSON 序列化/反序列化（项目已有 Gson 依赖）
  - 文件不存在时自动创建空配置 `{"mounts":{}}`
  - 目录不存在时自动创建 `~/.config/desktop-pet/`
  - 支持通过构造函数传入自定义 Path（便于测试）
  - 创建 `controller/src/test/java/com/desktoppet/core/MountConfigManagerTest.java`:
    - 使用 `@TempDir` 避免污染用户配置
    - 测试: save-then-load 一致性、文件不存在返回 null voicePackName、多模型配置互不影响

  **Must NOT do**:
  - 不实现 `eventOverrides` 字段
  - 不添加文件监听或热重载
  - 不加锁（单线程 JavaFX 应用线程内使用）

  **Recommended Agent Profile**:
  - **Category**: `quick`
    - Reason: 简单 JSON 读写，与现有 ConfigManager/HitAreaCacheManager 模式一致
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 5, 7)
  - **Blocks**: Tasks 8, 9 (AppOrchestrator 和 UI 需要配置管理)
  - **Blocked By**: Task 2 (MountConfig record)

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/core/ConfigManager.java` — 现有配置管理器，展示了 `Path` 构造、Gson 序列化、文件不存在时创建默认值的模式
  - `controller/src/main/java/com/desktoppet/core/HitAreaCacheManager.java` — per-entity JSON 持久化模式（最接近 MountConfigManager 的职责）
  - `controller/src/test/java/com/desktoppet/core/ConfigManagerTest.java` — 配置管理测试模式，展示了 `@TempDir`、save-load 一致性断言

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:576-600` — mount.json 完整格式定义
  - `docs/system/configuration.md` — 配置文件层级关系（config.json, model_config.json, mount.json）

  **WHY Each Reference Matters**:
  - ConfigManager/HitAreaCacheManager 是项目中 JSON 持久化的标准模式——MountConfigManager 必须遵循相同风格
  - mount.json 格式定义是唯一权威来源

  **Acceptance Criteria**:

  - [ ] `MountConfigManager.java` 存在，包含 `loadForModel()`, `saveForModel()`, `loadAll()` 方法
  - [ ] 支持通过构造函数传入自定义 Path
  - [ ] save 后 load 返回相同数据
  - [ ] 文件不存在 → `loadForModel()` 返回 `MountConfig(modelName, null)`
  - [ ] 多模型配置互不影响
  - [ ] 生成的 mount.json 格式与设计文档一致
  - [ ] `MountConfigManagerTest.java` 存在，包含至少 3 个测试用例
  - [ ] `mvn test -pl controller -Dtest=MountConfigManagerTest` → ALL PASS

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Save and load mount config round-trip
    Tool: Bash
    Preconditions: Tests written with @TempDir
    Steps:
      1. Run `cd controller && mvn test -Dtest=MountConfigManagerTest`
      2. Check all tests pass
    Expected Result: All tests pass — save/load consistency, null handling, multi-model isolation
    Failure Indicators: Data mismatch, file creation failure, JSON parse error
    Evidence: .sisyphus/evidence/task-6-config-tests.txt

  Scenario: mount.json format matches specification
    Tool: Bash
    Preconditions: Test includes format verification
    Steps:
      1. Run test that saves and reads raw JSON
      2. Verify JSON structure contains "mounts" object with model name key and "voice_pack" field
    Expected Result: JSON matches spec format
    Failure Indicators: Wrong JSON structure
    Evidence: .sisyphus/evidence/task-6-config-format.txt
  ```

  **Commit**: YES — commit 6
  - Message: `feat(voice-pack): add MountConfigManager with tests`
  - Files: `controller/src/main/java/com/desktoppet/core/MountConfigManager.java`, `controller/src/test/java/com/desktoppet/core/MountConfigManagerTest.java`
  - Pre-commit: `cd controller && mvn test`

- [x] 7. MountedBehaviorEngine — 事件分发引擎 + MountedBehaviorEngineTest

  **What to do**:
  - 创建 `controller/src/main/java/com/desktoppet/core/MountedBehaviorEngine.java`:
    ```java
    public class MountedBehaviorEngine {
        private final VoicePackInfo voicePack;
        private final Random random = new Random();
        
        public MountedBehaviorEngine(VoicePackInfo voicePack) { ... }
        
        /**
         * 处理 hit 事件。
         * 1. 在 voicePack.groups() 中查找 areaId 对应的 VoicePackGroup
         * 2. 从该 group 的 actions 中随机选一条 VoicePackAction
         * 3. 过滤: 如果 action.motionPath() 为空或 null → 跳过（Phase 3a 只处理有 motion 的 action）
         * 4. 构建绝对路径: voicePack.basePath() + "/" + action.motionPath()
         * 5. 构造 play_motion_ext Envelope:
         *    { type: "command", action: "play_motion_ext", payload: { motion_path, priority: group.priority(), fade_in, fade_out } }
         * 6. fadeIn/fadeOut: 从 action.fadeInMs()/fadeOutMs() 转换为秒 (ms / 1000.0)
         * @return 构造好的 Envelope JSON 字符串，或 null（无匹配 group 或无 motion）
         */
        public String buildMotionCommand(String areaId) { ... }
        
        /** 检查是否有指定 areaId 的事件组 */
        public boolean hasGroupForArea(String areaId) { ... }
    }
    ```
  - **关键设计决策**: Engine 不直接发送命令——返回构造好的 JSON 字符串，由 AppOrchestrator 负责发送。这保持了与现有 InteractionHandler 相同的职责分离模式
  - 使用 `Protocol.serialize()` 构造 Envelope（参考 InteractionHandler 的 Envelope 构造方式）
  - 创建 `controller/src/test/java/com/desktoppet/core/MountedBehaviorEngineTest.java`:
    - 构造 mock VoicePackInfo（包含 tap_head group 和多个 actions）
    - 测试: 匹配的 areaId 返回非 null 命令、不匹配的 areaId 返回 null、motion 为空的 action 被跳过、fadeIn/fadeOut 正确转换为秒

  **Must NOT do**:
  - 不直接发送 WebSocket 消息（返回 JSON 字符串，由调用者发送）
  - 不处理音频相关字段（Phase 3b）
  - 不处理 lipSync 或 doc 字段（Phase 3c）
  - 不实现 eventOverrides 映射

  **Recommended Agent Profile**:
  - **Category**: `deep`
    - Reason: 核心业务逻辑，涉及数据查找、随机选择、Envelope 构造、边界条件处理
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: YES
  - **Parallel Group**: Wave 2 (with Tasks 5, 6)
  - **Blocks**: Task 8 (AppOrchestrator 集成需要 Engine)
  - **Blocked By**: Task 2 (VoicePackInfo records)

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/core/InteractionHandler.java` — 现有 hit 事件处理器，展示了 `handleHitEvent(String areaName)` 方法签名、Envelope 构造（Protocol.serialize）、命令输出模式。MountedBehaviorEngine 替代了它在挂载场景下的角色
  - `controller/src/test/java/com/desktoppet/core/InteractionHandlerTest.java` — 现有测试展示了如何用 `ArgumentCaptor<String>` 捕获输出的命令字符串并用 `Protocol.deserialize()` 解析验证
  - `controller/src/main/java/com/desktoppet/network/Protocol.java` — `serialize(Envelope)` 和 `deserialize(String)` 方法

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:409-436` — MountedBehaviorEngine 设计（handleHitEvent, getIdleMotionPaths, handleSystemEvent）
  - `docs/system/voice-pack-mounting.md:264-286` — 场景 A 数据流（hit → 查询语音包 → 构建指令）
  - `docs/system/voice-pack-mounting.md:486-498` — play_motion_ext 指令 payload 格式
  - `docs/system/voice-pack-mounting.md:508` — fadeIn/fadeOut 单位转换: proto int64 毫秒 → 指令 float 秒

  **WHY Each Reference Matters**:
  - InteractionHandler 是最直接的替代对象——MountedBehaviorEngine 在挂载场景下接管其职责
  - InteractionHandlerTest 展示了命令验证的标准模式——用 ArgumentCaptor 捕获 JSON + Protocol.deserialize 解析
  - 场景 A 数据流是 Engine 的核心逻辑蓝图
  - 单位转换说明（ms → s）防止了常见 bug

  **Acceptance Criteria**:

  - [ ] `MountedBehaviorEngine.java` 存在，包含 `buildMotionCommand(String)` 和 `hasGroupForArea(String)` 方法
  - [ ] 匹配的 areaId（如 `"tap_head"`）→ 返回非 null JSON 字符串
  - [ ] 返回的 JSON 反序列化后: `action == "play_motion_ext"`, payload 包含 `motion_path`(绝对路径), `priority`, `fade_in`(秒), `fade_out`(秒)
  - [ ] 不匹配的 areaId → 返回 null
  - [ ] motion 为空的 action → 被跳过，选择其他 action 或返回 null
  - [ ] fadeIn=1000ms → fade_in=1.0s 转换正确
  - [ ] `MountedBehaviorEngineTest.java` 存在，包含至少 4 个测试用例
  - [ ] `mvn test -pl controller -Dtest=MountedBehaviorEngineTest` → ALL PASS

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Engine builds correct play_motion_ext command for tap_head
    Tool: Bash
    Preconditions: Tests with mock VoicePackInfo containing tap_head group
    Steps:
      1. Run `cd controller && mvn test -Dtest=MountedBehaviorEngineTest`
      2. Check all tests pass
    Expected Result: All tests pass — command structure correct, fade conversion correct, null for unknown areas
    Failure Indicators: Wrong action name, missing motion_path, incorrect fade values
    Evidence: .sisyphus/evidence/task-7-engine-tests.txt

  Scenario: Engine handles edge cases (empty motion, unknown area)
    Tool: Bash
    Preconditions: Tests include edge cases
    Steps:
      1. Verify edge case test methods exist
      2. Run all engine tests
    Expected Result: Edge case tests pass — empty motion skipped, unknown area returns null
    Failure Indicators: NPE, ArrayIndexOutOfBoundsException
    Evidence: .sisyphus/evidence/task-7-engine-edge-cases.txt
  ```

  **Commit**: YES — commit 7
  - Message: `feat(voice-pack): add MountedBehaviorEngine with tests`
  - Files: `controller/src/main/java/com/desktoppet/core/MountedBehaviorEngine.java`, `controller/src/test/java/com/desktoppet/core/MountedBehaviorEngineTest.java`
  - Pre-commit: `cd controller && mvn test`

---

- [x] 8. AppOrchestrator 集成 — 启动加载、Hit 事件连接、模型切换处理

  **What to do**:
  - 修改 `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java`:
    1. **新增字段**:
       - `MountConfigManager mountConfigManager` — 挂载配置管理器
       - `MountedBehaviorEngine mountedEngine` — 当前挂载引擎（可为 null）
       - `Map<String, VoicePackInfo> voicePackInfoCache` — 已解析的语音包缓存
    2. **startup() 扩展**:
       - 扫描语音包: `VoicePackScanner.scanAvailableVoicePacks(resourcesDir)`
       - 对每个语音包调用 `MetaMkoParser.parse()` 预解析（缓存结果）
       - 初始化 `MountConfigManager`
    3. **模型加载成功后 (model_loaded 事件处理中)**:
       - 读取当前模型的挂载配置: `mountConfigManager.loadForModel(modelName)`
       - 如果有语音包挂载 → 从缓存中获取 `VoicePackInfo` → 创建 `MountedBehaviorEngine`
       - 如果无挂载 → `mountedEngine = null`
    4. **hit 事件处理修改**:
       - 在现有 `interactionHandler.handleHitEvent()` 调用前检查:
       - 如果 `mountedEngine != null && mountedEngine.hasGroupForArea(areaId)`:
         - 调用 `mountedEngine.buildMotionCommand(areaId)` 获取 JSON
         - 通过 `sendOrCache(json)` 发送到 Renderer
         - 跳过 InteractionHandler 的默认处理
       - 否则: 继续使用 InteractionHandler 的默认处理
    5. **提供 public 方法给 UI 调用**:
       - `List<String> getAvailableVoicePacks()` — 返回已扫描的语音包名列表
       - `String getCurrentVoicePackForModel(String modelName)` — 返回当前挂载的语音包名
       - `void mountVoicePack(String modelName, String voicePackName)` — 挂载语音包并保存
       - `void unmountVoicePack(String modelName)` — 卸载语音包

  **Must NOT do**:
  - 不修改 Scheduler idle motion 逻辑
  - 不处理音频/lipSync/text 相关逻辑
  - 不删除或重构 InteractionHandler（保持 fallback）
  - 不添加语音包热重载

  **Recommended Agent Profile**:
  - **Category**: `unspecified-high`
    - Reason: 集成任务，需修改核心编排器，涉及生命周期管理、事件处理、多模块协调
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (sequential within wave)
  - **Blocks**: Task 9 (UI 需要 orchestrator 的 public 方法)
  - **Blocked By**: Tasks 4, 5, 6, 7 (需要所有核心模块就绪)

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java:67-76` — 现有 InteractionHandler 初始化模式（构造函数中创建，传入 `Consumer<String>` 命令发送器）
  - `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java:208-214` — 现有 hit 事件处理: `registerEventHandler("hit", ...)` + `interactionHandler.handleHitEvent(areaId)`。MountedBehaviorEngine 在此插入优先级检查
  - `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java:261-291` — `startSchedulerForModel()` 展示了模型加载后的初始化模式（解析模型信息、设置调度器）。语音包初始化应插入到相似位置
  - `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java:136-160` — `startup()` 方法展示了初始化顺序和错误处理模式

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:334-337` — AppOrchestrator 修改点列表（启动加载、初始化 MountedBehaviorEngine）
  - `docs/system/voice-pack-mounting.md:264-286` — 场景 A 数据流（hit event → Engine 查询 → 下发指令）

  **WHY Each Reference Matters**:
  - AppOrchestrator 的现有 hit 事件处理流程（行 208-214）是 MountedBehaviorEngine 的精确插入点
  - startup() 和 startSchedulerForModel() 展示了初始化时机——语音包扫描应在 startup() 中，Engine 创建应在 model_loaded 后

  **Acceptance Criteria**:

  - [ ] `AppOrchestrator.java` 包含 MountConfigManager 和 MountedBehaviorEngine 字段
  - [ ] startup() 中调用 VoicePackScanner 和 MetaMkoParser 完成预扫描
  - [ ] model_loaded 事件处理中读取挂载配置并创建/销毁 Engine
  - [ ] hit 事件: 有挂载 + 匹配 group → play_motion_ext 命令发送
  - [ ] hit 事件: 无挂载 → InteractionHandler 正常处理（行为不变）
  - [ ] hit 事件: 有挂载但 group 不匹配 → 静默跳过或 fallback 到 InteractionHandler
  - [ ] `getAvailableVoicePacks()`, `mountVoicePack()`, `unmountVoicePack()` 方法存在
  - [ ] 所有现有 13 个测试仍通过: `mvn test -pl controller`
  - [ ] 无挂载的模型行为完全不变

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Existing tests pass after AppOrchestrator modification
    Tool: Bash
    Preconditions: All Wave 1 + Wave 2 tasks complete
    Steps:
      1. Run `cd controller && mvn test`
      2. Verify all 13+ tests pass
    Expected Result: 0 failures, 0 errors
    Failure Indicators: Any existing test failure (regression)
    Evidence: .sisyphus/evidence/task-8-regression-tests.txt

  Scenario: Hit event with mounted voice pack sends play_motion_ext
    Tool: Bash
    Preconditions: AppOrchestrator wired, voice pack mounted for test model
    Steps:
      1. Run integration test or inspect AppOrchestrator hit handler logic
      2. Verify: when mountedEngine != null and area matches, play_motion_ext command is constructed
      3. Verify: when mountedEngine is null, InteractionHandler.handleHitEvent is called
    Expected Result: Correct delegation based on mount state
    Failure Indicators: Always using InteractionHandler, NPE on unmounted model
    Evidence: .sisyphus/evidence/task-8-hit-delegation.txt

  Scenario: Model switch reloads mount config
    Tool: Bash
    Preconditions: Two models with different mount configs
    Steps:
      1. Inspect code path: model_loaded event → loadForModel(newModelName)
      2. Verify: switching to unmounted model sets mountedEngine to null
      3. Verify: switching to mounted model creates new MountedBehaviorEngine
    Expected Result: Engine state correctly reflects new model's mount config
    Failure Indicators: Stale engine from previous model, NPE
    Evidence: .sisyphus/evidence/task-8-model-switch.txt
  ```

  **Commit**: YES — commit 8
  - Message: `feat(voice-pack): integrate voice pack mounting into AppOrchestrator`
  - Files: `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java`
  - Pre-commit: `cd controller && mvn test`

- [x] 9. UI 语音包选择器扩展

  **What to do**:
  - 在现有设置面板中添加语音包选择区域（具体位置需参考现有 FXML 布局）
  - 添加 UI 元素:
    1. `Label`: "语音包" / "Voice Pack"
    2. `ComboBox<String>`: 选项包含 "(无)" / "(None)" + 所有已扫描的语音包名
    3. 初始值: 从 `orchestrator.getCurrentVoicePackForModel(currentModel)` 获取
  - ComboBox 选择变化时:
    - 选择语音包 → 调用 `orchestrator.mountVoicePack(currentModel, selectedVoicePack)`
    - 选择 "(None)" → 调用 `orchestrator.unmountVoicePack(currentModel)`
  - 模型切换时刷新 ComboBox 的选中值（监听模型变化事件）
  - 创建对应 TestFX 测试验证 UI 元素存在和基本交互

  **Must NOT do**:
  - 不添加参数兼容度预览 UI（Phase 3 后续）
  - 不添加语音包详情页面
  - 不添加拖拽排序或复杂交互

  **Recommended Agent Profile**:
  - **Category**: `visual-engineering`
    - Reason: UI 布局、JavaFX 组件、FXML 修改、用户交互
  - **Skills**: []

  **Parallelization**:
  - **Can Run In Parallel**: NO
  - **Parallel Group**: Wave 3 (after Task 8)
  - **Blocks**: None (final implementation task)
  - **Blocked By**: Tasks 4, 6, 8 (需要 Scanner 列表、ConfigManager、Orchestrator 方法)

  **References**:

  **Pattern References**:
  - `controller/src/main/java/com/desktoppet/ui/SettingsTabController.java` — 现有设置标签控制器，展示了 ComboBox 初始化、事件监听、Orchestrator 调用模式。语音包选择器应添加在此或新建区域
  - `controller/src/main/java/com/desktoppet/ui/DashboardTabController.java` — 另一个 tab 控制器参考
  - `controller/src/test/java/com/desktoppet/ui/SettingsPanelTest.java` — 现有 UI 测试，展示了 TestFX headless 测试模式
  - 对应的 FXML 布局文件（需根据 SettingsTabController 中的 `@FXML` 注解定位）

  **API/Type References**:
  - `docs/system/voice-pack-mounting.md:214-217` — 语音包管理 UI 设计（语音包列表、挂载关系配置）

  **WHY Each Reference Matters**:
  - SettingsTabController 是 UI 扩展的直接目标文件——语音包选择器添加在其中
  - 现有 SettingsPanelTest 展示了 TestFX headless 测试的标准模式（Monocle 配置已在 pom.xml 中设置）
  - FXML 文件决定了 UI 布局结构

  **Acceptance Criteria**:

  - [ ] 设置面板中出现 "语音包" 标签和 ComboBox
  - [ ] ComboBox 包含 "(无)" 选项和所有已扫描的语音包名
  - [ ] 选择语音包 → mount.json 更新
  - [ ] 选择 "(无)" → mount.json 中对应模型的 voice_pack 设为 null
  - [ ] 模型切换 → ComboBox 更新为新模型的挂载状态
  - [ ] UI 测试存在并通过
  - [ ] `mvn test -pl controller` → ALL PASS（包括所有现有测试）

  **QA Scenarios (MANDATORY)**:

  ```
  Scenario: Voice pack selector appears in settings panel
    Tool: Bash
    Preconditions: UI extension implemented, TestFX test written
    Steps:
      1. Run `cd controller && mvn test -Dtest=*VoicePack*`
      2. Or run all UI tests: `cd controller && mvn test -Dtest=SettingsPanelTest`
    Expected Result: TestFX tests pass — ComboBox exists, items populated
    Failure Indicators: TestFX element not found, headless rendering failure
    Evidence: .sisyphus/evidence/task-9-ui-tests.txt

  Scenario: Full regression after UI changes
    Tool: Bash
    Preconditions: All tasks complete
    Steps:
      1. Run `cd controller && mvn test`
      2. Verify ALL tests pass (existing + new)
    Expected Result: 0 failures across all test files
    Failure Indicators: Any test failure
    Evidence: .sisyphus/evidence/task-9-full-regression.txt
  ```

  **Commit**: YES — commit 9
  - Message: `feat(voice-pack): add voice pack selector UI`
  - Files: FXML files, Controller files, TestFX test files
  - Pre-commit: `cd controller && mvn test`

---

## Final Verification Wave (MANDATORY — after ALL implementation tasks)

> 4 review agents run in PARALLEL. ALL must APPROVE. Present consolidated results to user and get explicit "okay" before completing.

- [x] F1. **Plan Compliance Audit** — `oracle`
  Read the plan end-to-end. For each "Must Have": verify implementation exists (read file, run command). For each "Must NOT Have": search codebase for forbidden patterns — reject with file:line if found. Check evidence files exist in .sisyphus/evidence/. Compare deliverables against plan.
  Output: `Must Have [N/N] | Must NOT Have [N/N] | Tasks [N/N] | VERDICT: APPROVE/REJECT`

- [x] F2. **Code Quality Review** — `unspecified-high`
  Run `mvn compile` + `mvn test`. Review all changed/new files for: unchecked casts, empty catches, raw types, missing null checks on protobuf optional fields, thread safety issues in MountedBehaviorEngine. Check AI slop: excessive comments, over-abstraction, generic names (data/result/item/temp). Verify protobuf generated code NOT committed to git.
  Output: `Build [PASS/FAIL] | Tests [N pass/N fail] | Files [N clean/N issues] | VERDICT`

- [x] F3. **Real Manual QA** — `unspecified-high`
  Start from clean state. Execute EVERY QA scenario from EVERY task — follow exact steps, capture evidence. Test cross-task integration: mount voice pack → click model → verify play_motion_ext sent. Test edge cases: empty Resources dir, corrupt meta.mko, unmounted model clicks. Save to `.sisyphus/evidence/final-qa/`.
  Output: `Scenarios [N/N pass] | Integration [N/N] | Edge Cases [N tested] | VERDICT`

- [x] F4. **Scope Fidelity Check** — `deep`
  For each task: read "What to do", read actual diff (git log/diff). Verify 1:1 — everything in spec was built (no missing), nothing beyond spec was built (no creep). Check "Must NOT do" compliance: no audio code, no lip sync, no .mkai parsing, no eventOverrides. Flag unaccounted changes.
  Output: `Tasks [N/N compliant] | Scope Creep [CLEAN/N issues] | Unaccounted [CLEAN/N files] | VERDICT`

---

## Commit Strategy

| Commit | Message | Files | Pre-commit Check |
|--------|---------|-------|------------------|
| 1 | `feat(voice-pack): add protobuf dependency and bundles.proto schema` | pom.xml, module-info.java, bundles.proto, .gitignore | `mvn compile` |
| 2 | `feat(voice-pack): add voice pack data model records` | VoicePackInfo.java, MountConfig.java | `mvn compile` |
| 3 | `feat(voice-pack): add play_motion_ext renderer command` | CommandHandlers.cpp, LAppModel.hpp/cpp | C++ build |
| 4 | `feat(voice-pack): add VoicePackScanner with tests` | VoicePackScanner.java, VoicePackScannerTest.java | `mvn test` |
| 5 | `feat(voice-pack): add MetaMkoParser with tests` | MetaMkoParser.java, MetaMkoParserTest.java, test fixture | `mvn test` |
| 6 | `feat(voice-pack): add MountConfigManager with tests` | MountConfigManager.java, MountConfigManagerTest.java | `mvn test` |
| 7 | `feat(voice-pack): add MountedBehaviorEngine with tests` | MountedBehaviorEngine.java, MountedBehaviorEngineTest.java | `mvn test` |
| 8 | `feat(voice-pack): integrate voice pack mounting into AppOrchestrator` | AppOrchestrator.java, InteractionHandler.java | `mvn test` |
| 9 | `feat(voice-pack): add voice pack selector UI` | FXML, Controller, TestFX test | `mvn test` |

---

## Success Criteria

### Verification Commands
```bash
cd controller && mvn compile          # Expected: BUILD SUCCESS (protobuf generated)
cd controller && mvn test             # Expected: ALL tests pass (13 existing + new)
cd controller && mvn package          # Expected: fat JAR at build/bin/desktop-pet-controller.jar
cat ~/.config/desktop-pet/mount.json  # Expected: valid JSON with mounts object
```

### Final Checklist
- [ ] All "Must Have" present
- [ ] All "Must NOT Have" absent
- [ ] All existing 13 tests pass
- [ ] All new tests pass
- [ ] `bundles.proto` compiles to Java classes
- [ ] `play_motion_ext` C++ command works
- [ ] Mount config persists across restarts
- [ ] UI shows voice pack selector
