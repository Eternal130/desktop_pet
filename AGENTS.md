# PROJECT KNOWLEDGE BASE

**Generated:** 2026-09-18
**Branch:** master

## OVERVIEW

Live2D desktop pet application. Qt 6 control panel (`controller_qt/`, C++17/QML) orchestrates a C++ rendering engine via WebSocket (JSON protocol, port 9001). Controller is WS Server; renderer is WS Client.

> Legacy JavaFX control panel (`controller/`, Maven/Java 21) was removed in Sept 2026 — `controller_qt/` is the only control panel.

## STRUCTURE

```
desktop_pet/
├── controller_qt/      # Qt 6 control panel (CMake, C++17) — the control panel
├── renderer/           # Live2D renderer (CMake, C++17, Cubism SDK 5)
├── third_party/        # CubismSdkForNative (Core + Framework + Samples)
├── docs/               # Full architecture docs (Chinese, 30+ .md files)
├── build.py            # Unified build orchestrator (Python 3.6+)
├── BUILD.md            # Build guide
└── build/bin/          # Output: renderer + controller-qt executables + DLLs + resources
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add controller UI page | `controller_qt/qml/pages/` | QML pages: Welcome (dashboard), InstanceDetail (two-column workbench), Monitor (3×2 charts), VoicePack, Settings (anchor nav) |
| Add controller shared QML component | `controller_qt/qml/components/` | StatusPill, SettingRow, SectionCard, Chip; register new files in qt_add_qml_module QML_FILES |
| Add controller business logic | `controller_qt/src/core/` | InstanceSession (+ split TUs), InstanceManager, Scheduler, InteractionHandler, config |
| Add WebSocket protocol message | Both `controller_qt/src/network/Protocol.hpp` and `renderer/src/network/Protocol.hpp` | Must match Envelope format |
| Add renderer command handler | `renderer/src/network/CommandHandlers.cpp` | Register in `RegisterHandlers()` |
| Add renderer event | `renderer/src/network/EventEmitter.cpp` | Controller handles in `controller_qt/src/core/InstanceSession` (Handlers TU) after `WsServer` routing |
| Change config format | `docs/system/configuration.md` + `controller_qt/src/core/InstanceConfigManager.cpp` | Config at `~/.config/desktop-pet/` |
| Add graphics feature | `renderer/src/graphics/` | Implement `IGraphicsBackend` for both OpenGL + Vulkan |
| Protocol spec | `docs/protocol/` | commands.md, events.md, handshake.md |
| Architecture overview | `docs/README.md` | Definitive project doc |
| AI learnings/pitfalls | `.sisyphus/notepads/` | Phase-specific dev notes |
| Add controller monitor feature | `controller_qt/src/ui/MonitorDataModel.cpp` | 60-sample ring buffer for QtCharts sparklines |
| Add controller system integration | `controller_qt/src/system/` | TrayManager, AutoLaunchManager, ResourceStatsCollector |
| Change controller instance config | `controller_qt/src/core/InstanceConfigManager.cpp` | Persists to `~/.config/desktop-pet/instances/{uuid}.json` (atomic write via QSaveFile) |
| Add controller protocol command | `controller_qt/src/network/Protocol.hpp` | 20 typed command factories + envelope helpers |

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
| `VoicePackScanner` | C++ | `controller_qt/src/core/VoicePackScanner.hpp` | Discovers voice packs containing `meta.mko` (takes renderer BASE dir, appends `Resources/VoicePacks` internally) |
| `VoicePackController` | C++ | `controller_qt/src/ui/VoicePackController.hpp` | QML bridge (`voicePacks` ctx prop): pack discovery + metadata for VoicePackPage |
| `MetaMkoParser` | C++ | `controller_qt/src/core/MetaMkoParser.hpp` | Hand-rolled protobuf wire-format reader for `.mko` (no libprotobuf dep) |
| `NotificationStreamController` | C++ | `controller_qt/src/ui/NotificationStreamController.hpp` | QML bridge (`notificationStream` ctx prop): bubble push/dismiss + testBubble; voice-pack dialogue sink is the only text source (no pack system); owns NotificationStreamModel |
| `PanelConfigController` | C++ | `controller_qt/src/core/PanelConfigController.hpp` | QML bridge for 4 PanelConfig behavior fields |
| `HitAreaCacheManager` | C++ | `controller_qt/src/core/HitAreaCacheManager.hpp` | JSON cache of modelName → hitAreas |

## CONVENTIONS (Non-Standard)

- **LApp\* naming**: C++ renderer follows Cubism SDK's `LApp*` prefix convention — NOT standard C++ naming
- **JSON snake_case keys** in config files and WebSocket payloads
- **Protocol Response fields**: `success`, `error_code`, `error_message` at JSON **top level**, NOT inside `payload`
- **Conventional Commits**: `type(scope): description` — scopes: `renderer`, `controller_qt`, `protocol`, `config`
- **Docs in Chinese**: All documentation is Simplified Chinese
- **Renderer = WS Client, Controller = WS Server** (non-intuitive direction)
- **C++ member vars**: `m_camelCase` prefix, `#pragma once` for headers

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

### Known Pitfalls
- **MinGW PATH**: Git's bundled MinGW conflicts with project MinGW — `build.py` filters it; manual CMake must do the same
- **MinGW PATH (Qt controller)**: `controller_qt/` uses Qt-bundled MinGW 13.1.0 (`C:\Qt\Tools\mingw1310_64`), separate from the renderer's MinGW — `build.py qt` filters PATH; manual CMake must prepend Qt's MinGW
- **CMake target_sources**: Adding `.cpp` requires `cmake -S ... -B ...` reconfigure before `cmake --build`
- **Cubism FinishedMotionCallback**: Raw C function pointer — capturing lambdas won't work, use static function + `SetFinishedMotionCustomData(void*)`
- **Qt QTP0001 policy**: QML modules use `:/qt/qml/<URI>/` layout (NEW policy) — incremental builds masked the bug during T1-T27
- **Qt LSP false positives**: LSP server lacks Qt include paths; `python build.py qt` compiles clean — trust CMake, not LSP diagnostics on Qt files
- **FluFrame no implicit size**: FluFrame is Rectangle-based — in GridLayout, rows collapse to 0 without `Layout.preferredHeight`/`implicitHeight` (verified via screenshot QA)
- **Row children + anchors.right**: silently misplaced — use anchored Item for title+trailing-button headers
- **ColumnLayout has no topPadding**: assigning it kills QML page compilation (only Column supports padding)

## COMMANDS

```bash
# Build everything
python build.py                # Interactive
python build.py all            # Both components

# Build individually
python build.py renderer       # C++ renderer (CMake + MinGW/Make)
python build.py qt             # Qt controller (CMake + Qt 6 + Ninja)

# Direct CMake (Windows)
cmake -S renderer -B build/renderer_mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/renderer_mingw --config Release -j

# Tests
cd renderer/build && ctest                              # C++ renderer tests (Google Test)
ctest --test-dir build/controller_qt                    # Qt controller tests (41 QTest binaries)

# Vulkan backend
cmake --build build/renderer_vulkan --config Release -j
```

## NOTES

- `third_party/CubismSdkForNative/` is NOT a git submodule — manually placed
- `set_scale` command is implemented as a `set_layout` scale-axis compatibility alias (responds; preserves offsets; clamps 0.1–5.0)
- The subtitle system (libass, renderer-side) has been REMOVED and replaced by the Qt-controller notification bubble stream — see `docs/system/notification-stream.md`. The 5 subtitle commands are unregistered in the renderer (unknown actions return 5003).
- VulkanBackend.cpp: **Fully implemented** (Instance → Device → Swapchain → Render → Present, 979 lines, Phase 2.1–2.6 complete). Compile-time switch via `-DUSE_VULKAN=ON`; no runtime switching. See `renderer/AGENTS.md`.
- AudioManager.cpp/hpp: Implemented (miniaudio + libvorbis, OGG playback). `play_audio`/`stop_audio`/`set_volume` commands wired (error codes 7001/7002/7003).
- Dual platform: Ubuntu/X11 (original MVP) + Windows/MinGW (current). `build.py` builds both OpenGL and Vulkan variants.
- Build artifacts: `build/bin/desktop-pet-renderer.exe`, `build/bin/desktop-pet-controller-qt(.exe)`
- Renderer CLI args: `--port`, `--instance-id`, `--model`, `--token`, `--x`, `--y`, `--width`, `--height`
- **controller_qt Phase 5-9: COMPLETE** — Qt 6.10 / C++17 / QML control panel. Multi-instance pet management with full protocol coverage (20 commands + 14 events), sidebar roster (`QAbstractListModel`), per-instance config persistence, idle motion scheduler, hit→motion handler, crash-recovery with exponential backoff (max 5 attempts), system tray (QSystemTrayIcon), OS auto-launch (Win registry / Linux `.desktop`), resource monitor (QtCharts sparklines: CPU% + RSS), voice pack discovery + mounting (hand-rolled protobuf reader, no libprotobuf dep), notification bubble stream (voice-pack dialogue only, screen top-right stack — replaced the renderer subtitle system, see `docs/system/notification-stream.md`), layout sync. **41 QTest binaries**, all green. Atomic config writes via QSaveFile. Blueprint §9.5 "永不崩溃" compliance audited (T25). See `controller_qt/README.md` for details. Known limitations (documented, NOT bugs): R4 (Wayland transparent window), R5 (GNOME tray AppIndicator), R6 (Wayland global hotkeys).
- Behavioral blueprint for the panel lives at `docs/controller_qt/architecture-blueprint.md` (framework-agnostic).
