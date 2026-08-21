# controller_qt

Qt 6 / C++17 / QML control panel for the desktop pet — the C++ successor to the
JavaFX `controller/`. A multi-instance desktop pet manager interoperable with
the existing C++ `renderer/` (Live2D Cubism 5) over the same JSON-over-WebSocket
protocol (controller = WS server, renderer = WS client).

> **Branch note (`feat/qt-fluentui-rewrite`):** the UI is rewritten on the
> **FluentUI QML component library** (zhuzichu520/FluentUI, main-branch commit,
> static-linked via FetchContent). Window shell = `FluWindow` + `FluAppBar` +
> `FluNavigationView` (left nav: Home / Instance / Monitor / Settings); pages
> use FluFrame / FluButton / FluToggleSwitch / FluText; Monitor charts stay on
> QtCharts (FluentUI's bundled charts are QCustomPlot = GPL — deliberately
> avoided). Accent unified: `FluTheme.primaryColor = Theme.accentColor`
> (indigo #5b5bd6). The legacy `feat/qt-controller-foundation` branch keeps the
> custom hand-drawn shell for comparison.
>
> **FluentUI-specific build notes:**
> - First configure fetches FluentUI (network required); later builds reuse the
>   FetchContent cache.
> - `build.py qt` additionally copies `Qt5Compat/GraphicalEffects` (FluAcrylic
>   dep) and `Qt6ShaderTools.dll` next to the exe — windeployqt cannot detect
>   these QML-internal imports.
> - Known limitation: with the static FluentUI plugin, the `--screenshot`
>   test-only path exits via `std::exit` to bypass a teardown heap corruption;
>   the production close path is unaffected (verified EXIT=0).

> **Scope:** This directory is purely additive. It does **not** touch
> `controller/` (JavaFX — still builds independently) or `renderer/` (C++ Live2D
> engine). The renderer keeps its own Google Test; controller_qt uses **QTest
> only**.

---

## Features (Phase 5–9)

| Area | Capability |
|:---|:---|
| **Instance management** | CRUD on pet instances, multi-instance concurrent operation, sidebar roster (`QAbstractListModel`), per-instance config persistence (`~/.config/desktop-pet/instances/*.json`) |
| **Runtime behavior** | Idle motion scheduler (QTimer cadence), hit→motion handler with 3-tier case-folding lookup, drag-persist on instance config, crash-recovery with exponential backoff |
| **System integration** | System tray (`QSystemTrayIcon` + QML menu popup), OS auto-launch (Win registry / Linux `.desktop`), close-action policy (close / minimize-to-tray / confirm) |
| **Resource monitor** | Live CPU% + RSS sparklines via QtCharts (6 lines, 2s poll), `GetProcessTimes`/`GetProcessMemoryInfo` on Win, `/proc/self/*` on Linux |
| **Voice packs** | Discovery + mounting, hand-rolled protobuf reader (no libprotobuf dep), priority behavior engine over the default InteractionHandler when a pack is mounted |
| **Subtitle system** | 16 named style fields, `set_subtitle_style` wired into the startup salvo |
| **Layout sync** | Per-instance position/size persisted, `layout_state` event handling |
| **Protocol coverage** | **25 commands** (9-command startup salvo + `set_hit_areas` + 15 runtime setters/triggers) and **14 events** routed by action |

---

## Build

```bash
# from project root
python build.py qt            # Qt controller only
python build.py all           # Qt controller + renderer + JavaFX controller
```

Output: `build/bin/desktop-pet-controller-qt.exe` (Windows) /
`build/bin/desktop-pet-controller-qt` (Linux), next to the renderer.

### Prerequisites

| Tool | Version | Notes |
|:---|:---|:---|
| **Qt** | 6.8 LTS minimum (developed on **6.10.0**) | Components required: `Core`, `Gui`, `Widgets`, `Network`, `WebSockets`, `Qml`, `Quick`, `QuickControls2`, `Concurrent`, `Charts`, `Test` |
| **MinGW** (Windows) | **13.1.0** (Qt-bundled) | `C:\Qt\Tools\mingw1310_64` — see [MinGW toolchain isolation](#critical--mingw-toolchain-isolation-windows) |
| **Ninja** | any recent (Qt-bundled `C:\Qt\Tools\ninja`) | Preferred generator |
| **CMake** | 3.22+ | On PATH |
| GCC / Clang (Linux) | C++17 capable | System toolchain |

The `qt` target in `build.py`:
1. Configures CMake into `build/controller_qt` with `CMAKE_PREFIX_PATH` pointing
   at the Qt install.
2. Builds with Qt's bundled toolchain (Windows) or the system toolchain (Linux).
3. (Windows) Runs `windeployqt --qmldir controller_qt/qml` to copy Qt DLLs /
   plugins / QML imports next to the exe so it runs standalone from `build/bin/`.
4. Fetches `spdlog v1.15.0` via CMake `FetchContent` (first configure needs
   network access; subsequent configures reuse `build/_deps/`).

Run the binary directly:

```bash
./build/bin/desktop-pet-controller-qt.exe   # launches the controller window
```

---

## Packaging (Windows)

`windeployqt` auto-deploys the Qt runtime next to the exe in `build/bin/`.
After `python build.py qt`, the bin directory contains everything the Qt
controller needs to run standalone — **no Qt installation required on the target
machine.**

The auto-detect (`--qmldir controller_qt/qml`, no explicit `--modules` flag)
covers every Qt module consumed by Phase 5–9:

| Bucket | Deployed artifacts |
|:---|:---|
| **Qt6 DLLs** | `Qt6Core`, `Qt6Gui`, `Qt6Network`, `Qt6WebSockets` (WsServer), `Qt6Widgets` (TrayManager — `QSystemTrayIcon` physically lives in QtWidgets on Qt 6.10), `Qt6Charts` + `Qt6ChartsQml` (MonitorPage), `Qt6Qml`, `Qt6Quick`, `Qt6QuickControls2`, `Qt6QuickLayouts`, `Qt6QuickShapes`, `Qt6QuickTemplates2`, `Qt6OpenGL`, `Qt6Svg`, plus transitive deps |
| **MinGW runtime** | `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll` |
| **Platforms** | `platforms/qwindows.dll` |
| **QML plugins** | `qml/QtCharts/` (qtchartsqml2 — for MonitorPage sparklines), `qml/QtQuick/{Controls,Layouts,Templates,Window,Shapes,Effects}/`, `qml/QtQml/{Models,WorkerScript,StateMachine,XmlListModel}/`, `qml/Qt/` (Quick templates) |
| **Translations** | `translations/qt_*.qm` |

> **Note:** `Qt6Test.dll` is **NOT** in the main `build/bin/` — the main app
> does not link `Qt6::Test`. Each of the 38 test binaries gets its own
> `windeployqt` post-build step (declared in `tests/CMakeLists.txt`) so ctest
> can run them in isolation.

The exe is ready to ship from `build/bin/`. NSIS / Inno Setup installer
generation is deferred to a later task — out of scope for Phase 5–9.

---

## Testing

```bash
ctest --test-dir build/controller_qt            # run all 38 QTest binaries
ctest --test-dir build/controller_qt -j8        # parallel
ctest --test-dir build/controller_qt -L REQUIRES_RENDERER   # integration tier only
ctest --test-dir build/controller_qt -E 'REQUIRES_RENDERER' # unit tier only
```

**38 QTest binaries** under `tests/` cover every module: network (Envelope,
WsServer, MessageDispatcher, Handshake, PendingRequests, EventRegistry,
ThreadMarshal, ProtocolFactory, WsServerMulti), core (InstanceSession,
InstanceManager, Scheduler, InteractionHandler, RestartController,
HitAreaCacheManager, ModelInfoParser, ModelScanner, MountedBehaviorEngine,
VoicePackScanner, MetaMkoParser, SubtitlePresets, OggDurationHeuristic,
MonitorDataModel, NetworkWiring), system (AutoLaunchManager, TrayManager,
ResourceStatsCollector), config (InstanceConfig, PanelConfig,
InstanceConfigManager, PanelStateManager, ConfigDir, Robustness, PathResolve),
infrastructure (Logging, PoCIntegration, ProcessManager, StartupSalvo,
ProtocolFixtures).

Each test exe links `Qt6::Test` and gets its own `windeployqt` post-build step
so ctest can launch it standalone (Qt's `bin/` is NOT on `PATH` under ctest).

**Known flakes:**
- `SchedulerTest` parallel timing — passes in isolation (`ctest -R SchedulerTest`).
- `WsServerMultiTest::testTwoInstancesRouteCorrectly` — intermittent
  `sendTextMessage returned 0` race under parallel load; passes in isolation.

Integration-tier tests carry the `REQUIRES_RENDERER` ctest label and `QSKIP` at
runtime if the renderer binary is absent, so the unit tier always runs cleanly
in headless/CI.

---

## CRITICAL — MinGW toolchain isolation (Windows)

There are **two distinct MinGW runtimes** in play on this project. They must
never be mixed.

| Consumer | MinGW | Path |
|:---|:---|:---|
| `renderer/` (C++ Live2D engine) | project / system MinGW | on `PATH` (filtered) |
| **`controller_qt/` (this project)** | **Qt-bundled MinGW 13.1.0** | **`C:\Qt\Tools\mingw1310_64`** |

Why this matters:

- The renderer and the Qt controller are **separate executables**. Each process
  loads its own app-local MinGW runtime DLLs (`libgcc_s_*`, `libstdc++-*`,
  `libwinpthread-*`). There is **no cross-process ABI mixing** — they are never
  linked into the same binary.
- But at **build time**, `cmake --build` resolves the compiler/linker and the
  `libstdc++`/runtime DLLs from `PATH`. If Git's bundled MinGW
  (`...\Git\mingw64\bin`) or the system MinGW shadows Qt's MinGW, the Qt
  controller gets linked against the wrong `libstdc++` and either fails to link
  or crashes at startup with a runtime-DLL mismatch.

`build.py`'s `qt` target (`_get_qt_env()`) guarantees isolation by:

1. **Prepending** `C:\Qt\Tools\mingw1310_64\bin` to `PATH` (Qt's MinGW wins).
2. **Filtering out** any `\Git\mingw64\bin` from `PATH` (Git's MinGW is removed
   entirely — mirrors the renderer's `_get_renderer_env()`).
3. Prepending Qt's Ninja (`C:\Qt\Tools\ninja`) so the build uses a single,
   unambiguous generator.

> This is the Qt-side analogue of the project-wide pitfall documented in the
> root `AGENTS.md` ("MinGW PATH": Git's bundled MinGW conflicts with the
> project's MinGW) and constraint **R7 (MinGW version conflict)** in
> `docs/controller/development-plan.md`. If you build `controller_qt` manually
> with bare `cmake`, you **must** replicate this PATH filtering yourself — Qt's
  MinGW must be found first.

---

## Architecture

```
                    ┌────────────────────────────────────────────┐
                    │              main.cpp (entry)              │
                    │  QGuiApplication + QQmlApplicationEngine   │
                    │  context properties: instanceManager,      │
                    │    trayManager, autoLaunch, panelConfig,   │
                    │    environmentChecker, windowStateSaver    │
                    └─────────────┬──────────────────────────────┘
                                  │ owns
                                  ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          InstanceManager                                    │
│           (QAbstractListModel — sidebar roster, demuxes WsServer             │
│            messages by instanceId, persistence via injected callback)        │
└─────────────┬───────────────────────────────────────────────────────────────┘
              │ one InstanceSession per pet
              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          InstanceSession                                    │
│           (per-pet orchestrator; split across 4 TUs to stay under            │
│            the 250 pure-LOC ceiling: .cpp / Setters / Commands /             │
│            Handlers / Monitor)                                               │
│                                                                              │
│   owns: ProcessManager ──────► renderer subprocess (QProcess)                │
│         MessageDispatcher ───► routes Envelopes (response/event/command)     │
│         EventRegistry ───────► 14-event default log-only + per-instance      │
│         PendingRequests ─────► 10s timeout table keyed by command id         │
│         Scheduler ───────────► QTimer idle motion cadence                    │
│         InteractionHandler ──► hit → play_motion (3-tier lookup)             │
│         HitAreaCacheManager ► JSON cache of modelName → hitAreas             │
│         MountedBehaviorEngine voice-pack behavior (priority over             │
│                               InteractionHandler when a pack is mounted)     │
│         RestartController ───► exponential-backoff crash recovery            │
│         MonitorDataModel ────► 60-sample ring buffer for QtCharts            │
└─────────────────────────────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                            WsServer                                         │
│       (QWebSocketServer on 127.0.0.1:9001, 3-gate token handshake,          │
│        multi-instance routing by instanceId, connection-replace on           │
│        reconnect — renderer is the WS CLIENT, controller is the SERVER)      │
└─────────────────────────────────────────────────────────────────────────────┘
              │ JSON Envelopes
              ▼
         renderer/<desktop-pet-renderer(.exe|-vulkan.exe)>
```

### Per-module notes

| Class | Role | Location |
|:---|:---|:---|
| `Protocol` | 25 typed command factories + envelope helpers | `src/network/Protocol.hpp` |
| `ResourceStatsCollector` | Win `GetProcessTimes`+`GetProcessMemoryInfo` / Linux `/proc/self/*`; never-throws contract | `src/system/ResourceStatsCollector.hpp` |
| `TrayManager` | `QSystemTrayIcon` wrapper (QGuiApplication — no `QApplication`, no `QMenu`; the QML `Menu` is the popup) | `src/system/TrayManager.hpp` |
| `AutoLaunchManager` | Win `reg.exe` via `QProcess` / Linux `~/.config/autostart/desktop-pet.desktop`; injectable suppliers for testing | `src/system/AutoLaunchManager.hpp` |
| `MonitorDataModel` | Copy-on-write ring buffer (cap=60) + `mergeController`/`mergeRenderer`/`isStale` | `src/ui/MonitorDataModel.hpp` |
| `MetaMkoParser` | Hand-rolled protobuf wire-format reader for `.mko` voice-pack metadata (no libprotobuf dep — see `.cpp` file-level comment) | `src/core/MetaMkoParser.hpp` |
| `PanelConfigController` | QML bridge for the 4 behavior `PanelConfig` fields (`closeAction`, `confirmOnExit`, `startMinimized`, `autoLaunchSystem`) | `src/core/PanelConfigController.hpp` |

### QML pages

| Page | Role |
|:---|:---|
| `Main.qml` | Frameless window, custom titlebar, 8-dir resize, tray menu popup, StackView host |
| `Sidebar.qml` | Instance roster (bound to `InstanceManager`), add/remove buttons |
| `WelcomePage.qml` | First-run environment detection (Qt/renderer/config-dir checks) |
| `InstanceDetailPage.qml` | Per-instance controls: model ComboBox, motion/expression triggers, position/scale setters |
| `MonitorPage.qml` | 6 QtCharts sparklines (controller CPU/RSS + renderer CPU/RSS + layout stats) |
| `SettingsPage.qml` | Panel-wide config: close action, confirm-on-exit, start-minimized, auto-launch |

### Notable design choices

- **M2 architecture**: each `InstanceSession` owns its own
  `MessageDispatcher` + `EventRegistry` — they are NOT shared singletons from
  `main.cpp`. This isolates per-pet state and makes the orchestrator testable
  without the full app.
- **TU split discipline**: `InstanceSession`'s bodies are spread across
  `InstanceSession.cpp` / `InstanceSessionSetters.cpp` /
  `InstanceSessionCommands.cpp` / `InstanceSessionHandlers.cpp` /
  `InstanceSessionMonitor.cpp` to keep each TU under the 250 pure-LOC ceiling.
- **Hand-rolled protobuf**: `MetaMkoParser.cpp` reads the `.mko` wire format
  directly (~150 LOC) instead of pulling in libprotobuf + abseil via
  `FetchContent`. The schema reference (`src/protobuf/bundles.proto`) is kept
  verbatim so a future task can swap in real protobuf without touching the
  public API. See the `.cpp` file-level comment for the full rationale.
- **`QTP0001 NEW` policy**: puts QML modules under `:/qt/qml/<URI>/` so
  `loadFromModule` works on clean builds (incremental builds masked the bug
  during T1–T27 — see `CMakeLists.txt` comment).

---

## Layout

```
controller_qt/
├── CMakeLists.txt              # Qt6 find_package, qt_add_executable, qt_add_qml_module
├── README.md                   # this file
├── src/
│   ├── main.cpp                # QGuiApplication + QQmlApplicationEngine entry
│   ├── poc_main.cpp            # Phase-0 PoC: drives renderer through full WS round-trip
│   ├── network/                # Envelope, Protocol (25 commands), WsServer, MessageDispatcher,
│   │                           #   PendingRequests, EventRegistry, ThreadMarshal
│   ├── core/                   # InstanceSession (+ 4 split TUs), InstanceManager, Scheduler,
│   │                           #   InteractionHandler, HitAreaCacheManager, RestartController,
│   │                           #   MountedBehaviorEngine, VoicePackScanner, MetaMkoParser,
│   │                           #   ModelInfoParser, ModelScanner, SubtitlePresets, PanelConfigController,
│   │                           #   ProcessManager, StartupSalvo, InstanceConfig(+Manager),
│   │                           #   PanelConfig(+StateManager), ConfigDir, PathResolve,
│   │                           #   EnvironmentChecker, WindowStateSaver
│   ├── system/                 # AutoLaunchManager, TrayManager, ResourceStatsCollector
│   ├── ui/                     # MonitorDataModel (QtCharts data source)
│   ├── logging/                # Logging.cpp (spdlog rotating-file + Qt message bridge)
│   └── protobuf/               # bundles.proto (schema reference — NOT compiled)
├── qml/
│   ├── Main.qml, Sidebar.qml, TitleBar.qml, ResizeHandles.qml, Theme.qml (singleton)
│   └── pages/                  # WelcomePage, InstanceDetailPage, MonitorPage, SettingsPage
└── tests/                      # 38 QTest binaries (see Testing section)
```

---

## Protocol interoperability

controller_qt speaks the **same JSON-over-WebSocket protocol** as the JavaFX
`controller/` — see `docs/protocol/` for the spec (`commands.md`, `events.md`,
`handshake.md`). Either controller can drive the same renderer binary; the
protocol envelope format is byte-compatible.

Direction is non-intuitive: the **renderer is the WS client**, the
**controller is the WS server** (listener on `127.0.0.1:9001`).

---

## See also

- `docs/controller/development-plan.md` — full phase plan (Phases 0–9)
- `docs/protocol/` — JSON protocol specification
- `docs/system/configuration.md` — config file format and locations
- Root `AGENTS.md` — project-wide conventions and pitfalls
- `.omo/notepads/controller-qt-phase5-9/learnings.md` — per-task dev notes
  (T1–T23 findings, gitignored — local only)
