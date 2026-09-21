# PROJECT KNOWLEDGE BASE

**Generated:** 2026-09-18
**Branch:** master

## OVERVIEW

Live2D desktop pet application. Qt 6 control panel (`controller_qt/`, C++17/QML) orchestrates a C++ rendering engine via WebSocket (JSON protocol, port 9001). Controller is WS Server; renderer is WS Client.

> Legacy JavaFX control panel (`controller/`, Maven/Java 21) was removed in Sept 2026 — `controller_qt/` is the only control panel.

## STRUCTURE

```
desktop_pet/
├── controller_qt/      # Qt 6 control panel (CMake, C++17) — pet_panel_core(STATIC)+pet_panel_api(INTERFACE) libs, exe shell = main.cpp + src/app/ (PanelApplication composition root); CMakePresets.json + cmake/qt-mingw-qt.cmake
│   ├── src/api/        # Plugin SDK v1.4 headers (see WHERE TO LOOK below)
│   ├── src/ui/         # QML bridges incl. RosterApiModel + InstanceControlBridge (host write bridges)
│   └── scripts/        # api_boundary_gate.sh (API boundary gate, ctest-enforced)
├── renderer/           # Live2D renderer (CMake, C++17, Cubism SDK 5) — CMakePresets.json (win presets reuse controller_qt's qt-mingw-qt.cmake, P1c unified toolchain)
├── third_party/        # CubismSdkForNative git submodule (Framework + Samples; Core via scripts/Bootstrap.cmake)
├── docs/               # Full architecture docs (Chinese, 30+ .md files)
├── scripts/            # Bootstrap.cmake (deps fetch) + build.sh/build.bat (thin forwarders) + setup-dev-env.ps1 (optional)
├── BUILD.md            # Build guide (CMake presets)
└── build/bin/          # Output: renderer + controller-qt executables + DLLs + resources
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add controller UI page | `controller_qt/qml/pages/` | QML pages: Welcome (dashboard), InstanceDetail (two-column workbench), Monitor (3×2 charts), VoicePack, ModelLibrary (model library, 58/42 dual-column), Settings (anchor nav) |
| Add controller shared QML component | `controller_qt/qml/components/` | StatusPill, SettingRow, SectionCard, Chip; register new files in qt_add_qml_module QML_FILES |
| Add controller business logic | `controller_qt/src/core/` | InstanceSession (+ split TUs), InstanceManager, Scheduler, InteractionHandler, config |
| Add WebSocket protocol message | Both `controller_qt/src/network/Protocol.hpp` and `renderer/src/network/Protocol.hpp` | Must match Envelope format |
| Add renderer command handler | `renderer/src/network/CommandHandlers.cpp` | Register in `RegisterHandlers()` |
| Add renderer event | `renderer/src/network/EventEmitter.cpp` | Controller handles in `controller_qt/src/core/InstanceSession` (Handlers TU) after `WsServer` routing |
| Change config format | `docs/system/configuration.md` + `controller_qt/src/core/InstanceConfigManager.cpp` | Config dir via `ConfigDir`/QStandardPaths: Linux `~/.config/desktop-pet/` (unchanged), Windows `%APPDATA%\desktop-pet\`; data (packs/downloads) `%LOCALAPPDATA%\desktop-pet\` |
| Add graphics feature | `renderer/src/graphics/` | Implement `IGraphicsBackend` for both OpenGL + Vulkan |
| Protocol spec | `docs/protocol/` | commands.md, events.md, handshake.md |
| Architecture overview | `docs/README.md` | Definitive project doc |
| AI learnings/pitfalls | `.sisyphus/notepads/` | Phase-specific dev notes |
| Add controller monitor feature | `controller_qt/src/ui/MonitorDataModel.cpp` | 60-sample ring buffer for QtCharts sparklines |
| Add controller system integration | `controller_qt/src/system/` | TrayManager, AutoLaunchManager, ResourceStatsCollector |
| Change controller instance config | `controller_qt/src/core/InstanceConfigManager.cpp` | Persists to `<configDir>/instances/{uuid}.json` (Linux `~/.config/desktop-pet/`, Windows `%APPDATA%\desktop-pet\`) (atomic write via QSaveFile) |
| Add controller protocol command | `controller_qt/src/network/Protocol.hpp` | 20 typed command factories + envelope helpers |
| Add plugin SDK API family | `controller_qt/src/api/` + `src/core/PluginContextImpl.cpp` | SDK v1.4 (kApiMinor=4). Existing families: IInstanceApi (read roster + observers), IInstanceControlApi (`pet.instance_control` lifecycle writes, capability-gated), ITuningApi (`pet.instance_tuning`, 9 tuning ops), IModelApi (`pet.model` read-only scan cache), ISettingsApi (`pet.settings` 4 whitelisted writes), IMonitorApi (`pet.monitor` read-only monitor stream, per-subscriber throttled), IVoicePackApi (pack discovery + IPackListObserver), IDownloadApi (host download chokepoint), IUiApi (pages + bubbles). New family recipe = new header (IExtApi subclass + apiId/version constants) + Impl in PluginContextImpl + PluginHost setter + PanelApplication singleton + queryApi route |
| Add/extend a QML page write to core | `controller_qt/qml/pages/` via `instanceControl` / `rosterModel` bridges | Host WRITES go through the API facade bridges (never direct session/manager calls — the gate bites); READS stay on live objects (`instance` property, `monitorModel()`, `motionGroupNames`) |
| Check/extend API boundary exceptions | `controller_qt/scripts/api_boundary_gate.sh` | Allowlist = file+line-level exemptions with per-entry justification; ctest enforces (`ApiBoundaryGateTest`, self-bite `ApiBoundaryGateSelfTest`) — extending it requires a reviewed reason |

## CODE MAP — Key Classes

| Symbol | Lang | Location | Role |
|--------|------|----------|------|
| `LAppDelegate` | C++ | `renderer/src/LAppDelegate.hpp` | Engine singleton: lifecycle, main loop, window |
| `LAppLive2DManager` | C++ | `renderer/src/LAppLive2DManager.hpp` | Model load/switch/release |
| `AudioManager` | C++ | `renderer/src/AudioManager.hpp` | miniaudio + libvorbis OGG playback (Phase 3b renderer side) |
| `WebSocketClient` | C++ | `renderer/src/network/WebSocketClient.hpp` | IXWebSocket client wrapper |
| `MessageHandler` | C++ | `renderer/src/network/MessageHandler.hpp` | Message routing by action |
| `InstanceSession` | C++ | `controller_qt/src/core/InstanceSession.hpp` | Per-pet orchestrator (split across 5 TUs: .cpp/Setters/Commands/Handlers/Monitor); owns ProcessManager + MessageDispatcher + EventRegistry + Scheduler + InteractionHandler + RestartController + MonitorDataModel |
| `InstanceManager` | C++ | `controller_qt/src/core/InstanceManager.hpp` | QAbstractListModel sidebar roster, demuxes WsServer messages by instanceId, persistence via savePanel callback |
| `WsServer` | C++ | `controller_qt/src/network/WsServer.hpp` | QWebSocketServer on 127.0.0.1:9001, 3-gate token handshake, multi-instance routing |
| `Scheduler` | C++ | `controller_qt/src/core/Scheduler.hpp` | QTimer idle motion cadence |
| `InteractionHandler` | C++ | `controller_qt/src/core/InteractionHandler.hpp` | hit → play_motion (3-tier case-folding lookup) |
| `MountedBehaviorEngine` | C++ | `controller_qt/src/core/MountedBehaviorEngine.hpp` | Voice-pack behavior engine (priority over InteractionHandler when a pack is mounted) |
| `RestartController` | C++ | `controller_qt/src/core/RestartController.hpp` | Exponential-backoff crash recovery (max 5 attempts) |
| `TrayManager` | C++ | `controller_qt/src/system/TrayManager.hpp` | QSystemTrayIcon wrapper (QtGui only — no QMenu, QML popup) |
| `AutoLaunchManager` | C++ | `controller_qt/src/system/AutoLaunchManager.hpp` | Win `reg.exe` via QProcess / Linux `.desktop` (injectable suppliers for testing) |
| `ResourceStatsCollector` | C++ | `controller_qt/src/system/ResourceStatsCollector.hpp` | Win `GetProcessTimes`+`GetProcessMemoryInfo` / Linux `/proc/self/*`; never-throws contract |
| `MonitorDataModel` | C++ | `controller_qt/src/ui/MonitorDataModel.hpp` | Copy-on-write ring buffer (cap=60) + mergeController/mergeRenderer for QtCharts |
| `VoicePackScanner` | C++ | `controller_qt/src/core/VoicePackScanner.hpp` | Discovers voice packs containing `meta.mko` (takes renderer BASE dir, appends `Resources/VoicePacks` internally). **Returns ABSOLUTE paths (not bare dir names)** — P4's listPacks assumed bare names and was caught by the P5 e2e gate |
| `VoicePackController` | C++ | `controller_qt/src/ui/VoicePackController.hpp` | QML bridge (`voicePacks` ctx prop): pack discovery + metadata for VoicePackPage |
| `ModelController` | C++ | `controller_qt/src/ui/ModelController.hpp` | QML bridge (`modelLibrary` ctx prop): model scan (wraps `ModelScanner`) + cached metadata via `ModelInfoParser` (motion groups/expressions/hit areas) + openModelDir; revision-counter refresh pattern like VoicePackController |
| `MetaMkoParser` | C++ | `controller_qt/src/core/MetaMkoParser.hpp` | Hand-rolled protobuf wire-format reader for `.mko` (no libprotobuf dep) |
| `NotificationStreamController` | C++ | `controller_qt/src/ui/NotificationStreamController.hpp` | QML bridge (`notificationStream` ctx prop): bubble push/dismiss + testBubble; voice-pack dialogue sink is the only text source (no pack system); owns NotificationStreamModel |
| `PanelConfigController` | C++ | `controller_qt/src/core/PanelConfigController.hpp` | QML bridge for 5 PanelConfig fields (4 behavior fields + `defaultModelName`); field writes forward through the shared `ISettingsApi` since S6 |
| `HitAreaCacheManager` | C++ | `controller_qt/src/core/HitAreaCacheManager.hpp` | JSON cache of modelName → hitAreas |
| `RosterApiModel` | C++ | `controller_qt/src/ui/RosterApiModel.hpp` | Sidebar roster READ model over shared `pet::IInstanceApi` + write bridge (`createInstance`/`deleteInstance` → shared `pet::IInstanceControlApi`); role bytes match InstanceManager so QML delegates bind unchanged |
| `InstanceControlBridge` | C++ | `controller_qt/src/ui/InstanceControlBridge.hpp` | QML write bridge (`instanceControl` ctx prop): tuning + lifecycle Q_INVOKABLEs over the shared ITuningApi/IInstanceControlApi (api/ headers only, no core/ includes; host bridge deliberately NOT capability-gated) |
| `InstanceControlApiImpl` | C++ | `controller_qt/src/core/PluginContextImpl.hpp` | Shared `pet::IInstanceControlApi` behind `queryApi("pet.instance_control")` — create/remove/start/stop/restart/loadModel over S4-semantics InstanceManager/InstanceSession |
| `TuningApiImpl` | C++ | `controller_qt/src/core/PluginContextImpl.hpp` | Shared `pet::ITuningApi` behind `queryApi("pet.instance_tuning")` — 9 tuning ops; mount resolves pack DIRECTORY NAMES via the host's own scan (paths never interpreted) |
| `ModelApiImpl` | C++ | `controller_qt/src/core/PluginContextImpl.hpp` | Shared `pet::IModelApi` behind `queryApi("pet.model")` — ONE model scan cache serving both the plugin family and ModelController |
| `SettingsApiImpl` | C++ | `controller_qt/src/core/PluginContextImpl.hpp` | Shared `pet::ISettingsApi` behind `queryApi("pet.settings")` — 4 typed writes over the SAME load-modify-save path as the panel's settings page |
| `MonitorApiImpl` | C++ | `controller_qt/src/core/PluginContextImpl.hpp` | Shared `pet::IMonitorApi` behind `queryApi("pet.monitor")` — projects each session's MonitorDataModel ring to flat `MonitorSample` PODs (-1 sentinels); subscribe fans out snapshotAppended with per-subscriber minIntervalMs merge throttling |

## CONVENTIONS (Non-Standard)

- **LApp\* naming**: C++ renderer follows Cubism SDK's `LApp*` prefix convention — NOT standard C++ naming
- **JSON snake_case keys** in config files and WebSocket payloads
- **Protocol Response fields**: `success`, `error_code`, `error_message` at JSON **top level**, NOT inside `payload`
- **Conventional Commits**: `type(scope): description` — scopes: `renderer`, `controller_qt`, `protocol`, `config`
- **Docs in Chinese**: All documentation is Simplified Chinese
- **Renderer = WS Client, Controller = WS Server** (non-intuitive direction)
- **C++ member vars**: `m_camelCase` prefix, `#pragma once` for headers
- **API boundary discipline (S7, ctest-enforced)**: host WRITES to core (session/manager write methods) go through the API facade — QML uses the `instanceControl`/`rosterModel` bridges, src/ui uses injected `pet::` API pointers; READS stay on live objects (session Q_PROPERTYs, `monitorModel()`). Enforced by `controller_qt/scripts/api_boundary_gate.sh` (`ApiBoundaryGateTest` + self-bite `ApiBoundaryGateSelfTest`); allowlist entries are file+line-level with reviewed justifications. **Gate pattern-table maintenance rule**: adding a write Q_INVOKABLE or WRITE-bearing Q_PROPERTY to InstanceSession/InstanceManager must extend the gate's pattern table (plus a self-test probe) in the same change, or the gate goes blind to the new write surface
- **queryApi family extension pattern**: new API family = new `src/api/` header (IExtApi subclass + `k<Family>ApiId`/`k<Family>ApiVersion` constants) + `*ApiImpl` in PluginContextImpl + PluginHost `set<Family>Api` injection (+ per-host fallback) + PanelApplication shared singleton + queryApi route (read families: ungated, null → nullptr; write families: capability + live `plugin_write_enabled` gate, null → stub). Bump `kApiMinor`

## ANTI-PATTERNS (THIS PROJECT)

### Forbidden in C++ Renderer
- **NO GL/GLES calls from WebSocket callback thread** — only `glfwPostEmptyEvent()` is thread-safe
- **NO spdlog** — use `LAppPal::PrintLogLn` exclusively
- **NO config files** on renderer side — all config managed by the Qt controller
- **NO custom reconnect logic** — IXWebSocket handles reconnection
- **NO abstract interfaces** for network layer — concrete classes only

### Forbidden in Qt Controller
- **NO blocking calls on the GUI thread** — long work goes to workers/timers
- **NO `QProcess::execute`-style blocking process calls** in UI paths — use `AutoLaunchManager`-style async QProcess
- **NO raw file writes for persisted config** — atomic writes via `QSaveFile` only
- **NO uncaught exceptions across the QML boundary** — never-throws contract on system collectors (Blueprint §9.5 "永不崩溃")
- **NO direct core write calls from QML / src/ui host bridges** — writes go through `instanceControl`/`rosterModel` (QML) or injected `pet::` API pointers (src/ui); `api_boundary_gate.sh` fails the build's test suite on violations (reads on live objects remain legal)

### Known Pitfalls
- **单 MinGW (unified, P1c)**: renderer and controller both compile with Qt-bundled mingw1310_64 via `controller_qt/cmake/qt-mingw-qt.cmake` (`QT_MINGW_ROOT`/`QT_NINJA`/`QT_PREFIX_PATH` overrides) — configure-time hard validation rejects any non-mingw1310_64 compiler (incl. Git's MinGW); PATH order no longer matters
- **CMake target_sources**: Adding `.cpp` requires `cmake -S ... -B ...` reconfigure before `cmake --build`
- **Cubism FinishedMotionCallback**: Raw C function pointer — capturing lambdas won't work, use static function + `SetFinishedMotionCustomData(void*)`
- **Qt QTP0001 policy**: QML modules use `:/qt/qml/<URI>/` layout (NEW policy) — incremental builds masked the bug during T1-T27
- **Qt LSP false positives**: LSP server lacks Qt include paths; the controller_qt preset build compiles clean — trust CMake, not LSP diagnostics on Qt files
- **Static-lib QML module (P2c)**: the DesktopPet QML module's backing target lives in pet_panel_core (STATIC) — any executable loading that QML must ALSO link `pet_panel_coreplugin`, else runtime-only `No module named "DesktopPet"` with zero compile symptoms
- **FluFrame no implicit size**: FluFrame is Rectangle-based — in GridLayout, rows collapse to 0 without `Layout.preferredHeight`/`implicitHeight` (verified via screenshot QA)
- **Row children + anchors.right**: silently misplaced — use anchored Item for title+trailing-button headers
- **ColumnLayout has no topPadding**: assigning it kills QML page compilation (only Column supports padding)

## COMMANDS

```bash
# Bootstrap (fresh clone / after submodule changes)
git submodule update --init --recursive
cmake -P scripts/Bootstrap.cmake     # Cubism Core + GLEW + GLFW (idempotent)

# Build everything (thin forwarders over the per-component workflows)
scripts/build.sh                     # Linux
scripts\build.bat                    # Windows

# Build individually (CMake presets; see BUILD.md for the full table)
cd controller_qt && cmake --workflow --preset linux-release    # Windows: win-release
cd renderer && cmake --workflow --preset linux-gl-release      # Windows: win-gl-release; Vulkan: *-vk-release

# Tests
cd controller_qt && ctest --preset linux-qt-release            # Qt controller tests (58 QTest/gate binaries)
cd renderer && ctest --preset linux-gl-release                 # C++ renderer tests (Google Test)
```

## NOTES

- `third_party/CubismSdkForNative/` is a git submodule of Live2D/CubismNativeSamples pinned to tag `5-r.5-beta.3.1` (nested `Framework` submodule pinned automatically). Core binaries are NOT in the repo — bootstrap: `git submodule update --init --recursive` then `cmake -P scripts/Bootstrap.cmake`; the renderer configure aborts with these hints if Core is missing
- `set_scale` command is implemented as a `set_layout` scale-axis compatibility alias (responds; preserves offsets; clamps 0.1–5.0)
- The subtitle system (libass, renderer-side) has been REMOVED and replaced by the Qt-controller notification bubble stream — see `docs/system/notification-stream.md`. The 5 subtitle commands are unregistered in the renderer (unknown actions return 5003).
- VulkanBackend.cpp: **Fully implemented** (Instance → Device → Swapchain → Render → Present, 979 lines, Phase 2.1–2.6 complete). Compile-time switch via `-DUSE_VULKAN=ON`; no runtime switching. See `renderer/AGENTS.md`.
- AudioManager.cpp/hpp: Implemented (miniaudio + libvorbis, OGG playback). `play_audio`/`stop_audio`/`set_volume` commands wired (error codes 7001/7002/7003).
- Dual platform: Ubuntu/X11 (original MVP) + Windows/MinGW (current). CMake presets build both OpenGL and Vulkan variants.
- Build artifacts: `build/bin/desktop-pet-renderer.exe`, `build/bin/desktop-pet-controller-qt(.exe)`
- Renderer CLI args: `--port`, `--instance-id`, `--model`, `--token`, `--x`, `--y`, `--width`, `--height`
- **controller_qt Phase 5-9: COMPLETE** — Qt 6.10 / C++17 / QML control panel. Multi-instance pet management with full protocol coverage (20 commands + 14 events), sidebar roster (`QAbstractListModel`), per-instance config persistence, idle motion scheduler, hit→motion handler, crash-recovery with exponential backoff (max 5 attempts), system tray (QSystemTrayIcon), OS auto-launch (Win registry / Linux `.desktop`), resource monitor (QtCharts sparklines: CPU% + RSS), voice pack discovery + mounting (hand-rolled protobuf reader, no libprotobuf dep), notification bubble stream (voice-pack dialogue only, screen top-right stack — replaced the renderer subtitle system, see `docs/system/notification-stream.md`), model library page (ModelController scan/metadata, per-instance model switch via AppComboBox with model_load_failed rollback, default model for new instances in panel config `default_model_name`), layout sync. **58 QTest/gate binaries**, all green. Atomic config writes via QSaveFile. Blueprint §9.5 "永不崩溃" compliance audited (T25). See `controller_qt/README.md` for details. Known limitations (documented, NOT bugs): R4 (Wayland transparent window), R5 (GNOME tray AppIndicator), R6 (Wayland global hotkeys).
- **API-first migration S1-S7: COMPLETE** — plugin SDK at **v1.4** (`src/api/`, 9 interface families: instance read/control/tuning/model/settings/monitor/voicepack/download/ui). All host writes dogfood the same vtables plugins use (sidebar + detail page + create flows + settings + pack mount + model library + welcome lifecycle via `RosterApiModel`/`InstanceControlBridge`/injected APIs); reads stay on live objects. InstanceSession split-up is deliberately DEFERRED (triggers documented in docs/refactor revision v4). Boundary enforced by `api_boundary_gate.sh` under ctest. Version history + family×capability×switch matrix: `controller_qt/src/api/README.md`.
- Behavioral blueprint for the panel lives at `docs/controller_qt/architecture-blueprint.md` (framework-agnostic).
