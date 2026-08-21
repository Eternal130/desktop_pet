cmake_minimum_required(VERSION 3.22)

project(controller_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.8 LTS minimum — accepts newer (e.g. 6.10.0). The Qt install location is
# supplied at configure time via -DCMAKE_PREFIX_PATH by build.py; do NOT hardcode
# the Qt path here.
find_package(Qt6 6.8 COMPONENTS
    Core
    Gui
    Widgets
    Network
    WebSockets
    Qml
    Quick
    QuickControls2
    Concurrent
    Charts
    Test
    REQUIRED
)

qt_standard_project_setup()

# QTP0001 NEW puts QML modules under the ':/qt/qml/<URI>/' resource prefix — the
# path QQmlApplicationEngine::loadFromModule expects on Qt 6.5+. Without this,
# clean builds (where CMake reconfigures from scratch) register the module under
# ':/<URI>/' and loadFromModule fails at runtime with "Module ... contains no
# type named ...". Incremental builds (T1-T27) masked this because they never
# reconfigured. This is the fix the T1 learnings flagged as "safe (matches
# loadFromModule)". QTP0004 stays default/OLD so qmldir stays auto-generated.
qt_policy(SET QTP0001 NEW)

# spdlog (header option: bundled fmt). Fetched at configure time, so the first
# configure requires network access; subsequent configures reuse build/_deps/.
# Used by src/logging/Logging.cpp (rotating file sink + Qt message bridge).
include(FetchContent)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.15.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# FluentUI QML component library (Fluent Design for Qt). No release tag
# supports Qt 6.10 (latest 1.7.7 = 2024-07), so we pin a main-branch commit.
# Static-lib mode: types registered via FluentUI::registerTypes(&engine) in
# main.cpp + Q_IMPORT_QML_PLUGIN(FluentUIPlugin) — single-exe deploy intact.
find_package(Qt6 6.8 COMPONENTS ShaderTools QuickControls2 Svg REQUIRED)
set(FLUENTUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLUENTUI_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    FluentUI
    GIT_REPOSITORY https://github.com/zhuzichu520/FluentUI.git
    GIT_TAG        7e33a2f672d18239ef49ab960075b1137a1d18e7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(FluentUI)

# Executable. Output lands in <root>/build/bin next to the renderer so the
# WebSocket/port + relative resource paths line up with the rest of the app.
set(_bin_output_dir "${PROJECT_SOURCE_DIR}/../build/bin")
qt_add_executable(desktop-pet-controller-qt
    src/main.cpp
    src/network/Envelope.cpp
    src/network/Protocol.cpp
    src/network/WsServer.cpp
    src/network/MessageDispatcher.cpp
    src/network/PendingRequests.cpp
    src/network/EventRegistry.cpp
    src/logging/Logging.cpp
    src/core/PathResolve.cpp
    src/core/ProcessManager.cpp
    src/core/StartupSalvo.cpp
    src/core/ConfigDir.cpp
    src/core/InstanceConfig.cpp
    src/core/InstanceConfigManager.cpp
    src/core/PanelConfig.cpp
    src/core/PanelStateManager.cpp
    # Phase 5 todo 4: pure static .model3.json parser + model-directory
    # scanner. Consumed by todo 5 (InstanceSession lifecycle: on
    # model_loaded, parseModelInfo → send set_hit_areas with the parsed
    # hitAreas) and todo 6 (InstanceDetailPage model ComboBox populated by
    # scanAvailableModels). Header-only deps: Qt6::Core (QFile/QJson/QDir)
    # already linked below; no new Qt modules needed.
    src/core/ModelInfoParser.cpp
    src/core/ModelScanner.cpp
    # Phase 5 todo 2: per-instance orchestrator. Owns ProcessManager +
    # MessageDispatcher + EventRegistry (M2 — NOT shared from main.cpp). The
    # .hpp is listed so AUTOMOC processes its Q_OBJECT metadata.
    src/core/InstanceSession.cpp
    src/core/InstanceSession.hpp
    src/core/InstanceSessionSetters.cpp
    # Phase 5 todo 6: Q_INVOKABLE model-metadata accessors + play_motion /
    # set_expression triggers consumed by InstanceDetailPage.qml. Extracted to
    # its own TU to keep InstanceSession.cpp under the 250 pure-LOC ceiling
    # (same split discipline as InstanceSessionSetters.cpp).
    src/core/InstanceSessionCommands.cpp
    # Phase 5 todo 8: QTimer-based idle motion scheduler. Single QTimer on the
    # main thread (D3) drives the cadence; recursive single-shot re-arm
    # replicates Java's scheduleAtFixedRate. Standalone for todo 8 (tested
    # independently via SchedulerTest); todo 10 owns a Scheduler inside
    # InstanceSession and starts it on model_loaded. The .hpp is listed so
    # AUTOMOC processes its Q_OBJECT + signals metadata.
    src/core/Scheduler.cpp
    src/core/Scheduler.hpp
    # Phase 5 todo 9: hit→play_motion translator. QObject port of Java's
    # InteractionHandler. 3-tier case-folding lookup (exact → capitalized →
    # DEFAULT_MAPPINGS lowercased) over an optional per-model ModelConfig.
    # Standalone for todo 9 (tested independently); todo 10 wires this into
    # InstanceSession's hit event handler (after MountedBehaviorEngine in
    # Phase 8). The .hpp is listed so AUTOMOC processes its Q_OBJECT metadata.
    src/core/InteractionHandler.cpp
    src/core/InteractionHandler.hpp
    # Phase 5 todo 10: Wave 6 capstone — HitAreaCacheManager (JSON cache of
    # modelName→hitAreas at <configDir>/hit_area_cache.json, ported from Java)
    # + InstanceSessionHandlers (event-handler bodies for hit/drag_end/
    # motion_finished + startIdleScheduler, extracted to keep InstanceSession.cpp
    # under the 250 pure-LOC ceiling). Both .hpp files are listed so AUTOMOC
    # processes their Q_OBJECT metadata.
    src/core/HitAreaCacheManager.cpp
    src/core/HitAreaCacheManager.hpp
    src/core/InstanceSessionHandlers.cpp
    # Phase 5 Wave 7 todo 12: crash-recovery exponential-backoff scheduler.
    # QObject owning the per-instance restart-attempt count + the BACKOFF_MS
    # ladder; port of Java MainWindowController::scheduleRestart. QTimer::
    # singleShot schedules retry() on the Qt main thread (no extra thread).
    # Owned by InstanceSession — one RestartController per pet instance, same
    # ownership pattern as Scheduler / InteractionHandler. The .hpp is listed
    # so AUTOMOC processes its Q_OBJECT + signals metadata.
    src/core/RestartController.cpp
    src/core/RestartController.hpp
    # Phase 5 todo 3: QAbstractListModel sidebar roster. Owns the
    # QList<InstanceSession*> and demuxes WsServer messages by instanceId (M2).
    # Loads on construction in instanceIds order (m4 fix — NOT loadAll's sort).
    # Persistence is injected via a savePanel callback (m5 fix — decouples from
    # PanelStateManager for testability). The .hpp is listed so AUTOMOC
    # processes its Q_OBJECT + Q_ENUM metadata.
    src/core/InstanceManager.cpp
    src/core/InstanceManager.hpp
    # T27 welcome-page environment detection. The .hpp is listed so AUTOMOC
    # reliably processes its Q_OBJECT metadata. Registered as a context
    # property in main.cpp (not QML_ELEMENT — see EnvironmentChecker.hpp).
    src/core/EnvironmentChecker.cpp
    src/core/EnvironmentChecker.hpp
    # T28 window-state restore bridge: Q_INVOKABLE saveWindowState called from
    # Main.qml. Same context-property pattern as EnvironmentChecker (.hpp listed
    # for AUTOMOC, no QML_ELEMENT).
    src/core/WindowStateSaver.cpp
    src/core/WindowStateSaver.hpp
    # Phase 5 Wave 7 todo 15: QML bridge for PanelConfig's 4 behavior fields
    # (closeAction, confirmOnExit, startMinimized, autoLaunchSystem). Exposes
    # them as Q_PROPERTY (READ+WRITE+NOTIFY) so SettingsPage binds two-way;
    # each setter does load-modify-save via PanelStateManager so the other 8
    # fields survive unchanged. Registered as the 'panelConfig' context
    # property in main.cpp. The .hpp is listed so AUTOMOC processes Q_OBJECT.
    src/core/PanelConfigController.cpp
    src/core/PanelConfigController.hpp
    # Phase 5 Wave 7 todo 14: OS auto-launch manager (Windows reg.exe via
    # QProcess / Linux ~/.config/autostart/desktop-pet.desktop / macOS no-op).
    # Port of Java util/AutoLaunchManager.java. Injectable suppliers (app-path,
    # autostart-dir, reg-executor, platform) make it testable without touching
    # the real registry. Exposed as the 'autoLaunch' context property by
    # todo 15 (closeAction). First file in src/system/ — the subdir exists for
    # OS-integration classes (TrayManager also lands here in todo 13).
    src/system/AutoLaunchManager.cpp
    src/system/AutoLaunchManager.hpp
    # Phase 5 Wave 7 todo 13: QSystemTrayIcon wrapper. QtGui-only (works with
    # QGuiApplication — NO QtWidgets link, NO QMenu). Constructor calls
    # QGuiApplication::setQuitOnLastWindowClosed(false). Right-click →
    # requestContextMenu → QML Menu pops (Main.qml); DoubleClick →
    # visibilityToggled. Icon loaded from :/icons/tray-icon.png with a
    # generated blue-circle fallback when no .qrc exists (current state).
    # Exposed as the 'trayManager' context property by main.cpp. The .hpp is
    # listed so AUTOMOC processes its Q_OBJECT + signals metadata.
    src/system/TrayManager.cpp
    src/system/TrayManager.hpp
    # Phase 5 Wave 8 todo 17: self-resource collection via platform APIs
    # (replaces Java OSHI). Windows — GetProcessTimes (CPU%, two-sample diff)
    # + GetProcessMemoryInfo (RSS WorkingSetSize); Linux — /proc/self/stat
    # (CPU ticks) + /proc/self/status (VmRSS) parsed from scratch (M5 fix —
    # the renderer's ProcessStatsCollector Linux half is a STUB returning
    # zeros, NOT a reference). ControllerStats{cpuPercent, rssBytes,
    # timestampMs} drops the JVM-heap fields (N/A for Qt). Never-throws
    # contract (§4.8.1) — failed samples zero cpuPercent/rssBytes but always
    # set timestampMs. Consumed by todo 18 (MonitorPage 2s QTimer poll).
    # psapi (GetProcessMemoryInfo) linked below on Windows.
    src/system/ResourceStatsCollector.cpp
    src/system/ResourceStatsCollector.hpp
    # Phase 5 Wave 8 todo 19: voice pack protobuf integration. Two pure static
    # utilities + one data-model header, all header-only-deps (Qt6::Core only,
    # already linked below). No new Qt modules.
    #
    # src/protobuf/bundles.proto is the verbatim schema reference (copied from
    # controller/src/main/protobuf/bundles.proto). It is NOT compiled — the Qt
    # port uses a hand-rolled protobuf wire-format reader in MetaMkoParser.cpp
    # instead of FetchContent'ing libprotobuf + protoc codegen. See
    # MetaMkoParser.cpp's file-level comment for the full rationale (short:
    # the .mko schema is 5 messages of scalar fields; a ~150 LOC reader is
    # cleaner than pulling in protobuf+abseil for an optional Phase 8 feature;
    # the task spec explicitly allows this fallback). bundles.proto is kept
    # verbatim so a future task can swap in real protobuf via FetchContent +
    # protobuf_generate_cpp without touching the schema — the parseMetaMko
    # → std::optional<VoicePackInfo> public API stays identical.
    src/core/VoicePackScanner.cpp
    src/core/MetaMkoParser.cpp
    # Phase 5 Wave 8 todo 20: MountedBehaviorEngine — the voice-pack behavior
    # engine that has PRIORITY over InteractionHandler for hit events when a
    # voice pack is mounted. Default-constructed member of InstanceSession
    # (= no voice pack → hasGroupForArea always false → falls back to
    # InteractionHandler). Todo 21 swaps in a populated VoicePackInfo when
    # the user mounts a pack via the UI. Port of Java's MountedBehaviorEngine
    # (246 LOC); estimateOggDurationMs is a ~50 LOC heuristic that parses the
    # OGG container directly (no libvorbis dependency — see the .cpp file-level
    # comment for the rationale). Header-only-deps: Qt6::Core + Protocol.hpp
    # (already linked below).
    src/core/MountedBehaviorEngine.cpp
    # Phase 5 Wave 8 todo 18: resource-monitor data model + InstanceSession
    # monitor wiring. MonitorDataModel is the Qt port of Java's
    # ui/MonitorDataModel.java (copy-on-write ring buffer cap=60 +
    # mergeController/mergeRenderer/isStale). InstanceSessionMonitor.cpp holds
    # the 2s QTimer tick + stats_state event handler bodies extracted from
    # InstanceSession.cpp (same split discipline as Setters/Commands/Handlers).
    # Qt6::Charts (linked below) is the QML ChartView/LineSeries/ValueAxis
    # module consumed by MonitorPage.qml's 6 sparkline charts.
    src/ui/MonitorDataModel.cpp
    src/ui/MonitorDataModel.hpp
    src/core/InstanceSessionMonitor.cpp
)

set_target_properties(desktop-pet-controller-qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_bin_output_dir}"
    WIN32_EXECUTABLE ON
)

# Expose src/ so T4+ sources can write `#include "network/Envelope.hpp"` etc.
target_include_directories(desktop-pet-controller-qt PRIVATE src)

# QGuiApplication (QtGui) and QQmlApplicationEngine (QtQml/QtQuick) are not
# auto-linked by qt_add_executable, so link them explicitly. Core/Network are
# transitive deps of Qml/Quick but listed for clarity.
target_link_libraries(desktop-pet-controller-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    # T13 finding: QSystemTrayIcon is PHYSICALLY in QtWidgets on Qt 6.10
    # (qsystemtrayicon.h includes qtwidgetsglobal.h) despite the Qt docs
    # implying it moved to QtGui. Linking QtWidgets is REQUIRED to use
    # QSystemTrayIcon. M3 spirit still respected: we use QGuiApplication
    # (NOT QApplication), instantiate NO QWidget subclasses, and use NO
    # QMenu — the QML Menu (Main.qml) is the tray popup. QtWidgets here is
    # purely a header/symbol dependency for QSystemTrayIcon.
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Qml
    Qt6::Quick
    # Wave 8 todo 18: QtCharts QML module (ChartView, LineSeries, ValueAxis)
    # consumed by MonitorPage.qml's 6 live resource-monitor sparklines.
    # windeployqt auto-detects Qt6Charts.dll via the exe's import table — no
    # --modules charts flag needed in build.py's windeployqt invocation.
    Qt6::Charts
    spdlog::spdlog
    # FluentUI (static lib mode; PUBLIC deps Widgets/PrintSupport come
    # transitively). `fluentuiplugin` carries the Q_IMPORT_QML_PLUGIN symbol
    # referenced by main.cpp's static import.
    fluentui
    fluentuiplugin
)

# Force the spdlog *active* macros (SPDLOG_DEBUG/INFO/... which LOG_* wrap) to
# emit at debug level by default so LOG_DEBUG is live in dev builds. Release
# builds can override by appending -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO.
target_compile_definitions(desktop-pet-controller-qt PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# Static-lib FluentUI target doesn't export its include dir; FluentUI.h lives
# in the FetchContent src/ tree.
target_include_directories(desktop-pet-controller-qt PRIVATE
    ${fluentui_SOURCE_DIR}/src)

# Windows-only system-library link for ResourceStatsCollector (todo 17):
# GetProcessMemoryInfo lives in Psapi.lib. MinGW ships libpsapi.a so the
# plain `psapi` link name resolves. No-op on Linux (/proc needs no link).
if(WIN32)
    target_link_libraries(desktop-pet-controller-qt PRIVATE psapi)
endif()

# QML module. Main.qml is auto-registered as type "Main" under URI DesktopPet,
# loadable via QQmlApplicationEngine::loadFromModule("DesktopPet", "Main").
# T24: pages/ holds the 4 StackView placeholder pages. qt_add_qml_module
# flattens subdirectory paths — each file's basename becomes its registered
# type name, so WelcomePage.qml is usable as `WelcomePage {}` after
# `import DesktopPet`.
#
# Theme.qml is `pragma Singleton` — on clean builds with Qt 6.10 + QTP0001 NEW,
# the auto-generated qmldir does NOT pick up `pragma Singleton` and registers
# Theme as a plain component (its instance properties — bgColor, setTheme, … —
# are then undefined at runtime). The explicit QT_QML_SINGLETON_TYPE property
# forces the `singleton` keyword in qmldir so Theme works as a singleton.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(desktop-pet-controller-qt
    URI DesktopPet
    VERSION 1.0
    QML_FILES
        qml/Theme.qml
        qml/Main.qml
        qml/TitleBar.qml
        qml/ResizeHandles.qml
        qml/Sidebar.qml
qml/pages/WelcomePage.qml
qml/pages/InstanceDetailPage.qml
qml/pages/SettingsPage.qml
qml/pages/MonitorPage.qml
qml/pages/AppCheckBox.qml
)

# ── Phase 0 PoC (task T6) ───────────────────────────────────────────────────
# A SEPARATE console executable with its own main(). It drives the real C++
# renderer through the full WS round-trip (ready → load_model → model_loaded →
# shutdown → exit 0) as a Wave 1 milestone proof. QCoreApplication (NOT
# QGuiApplication): the PoC has no GUI; the renderer opens its own GLFW window.
qt_add_executable(desktop-pet-controller-qt-poc
    src/poc_main.cpp
    src/network/Envelope.cpp
    src/network/WsServer.cpp
    src/logging/Logging.cpp
)
target_link_libraries(desktop-pet-controller-qt-poc PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    spdlog::spdlog
)
target_include_directories(desktop-pet-controller-qt-poc PRIVATE src)
target_compile_definitions(desktop-pet-controller-qt-poc PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
set_target_properties(desktop-pet-controller-qt-poc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_bin_output_dir}"
    WIN32_EXECUTABLE OFF  # console app — stdout must reach the terminal
)

# windeployqt for the PoC binary (Qt6Core.dll etc. next to the exe). build.py's
# build_qt() runs windeployqt only on the main app; the PoC gets its own deploy
# step here so it runs standalone from build/bin/.
if(WIN32)
    get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(QT_WINDEPLOYQT_EXE_POC
        NAMES windeployqt
        HINTS "${_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(QT_WINDEPLOYQT_EXE_POC)
        add_custom_command(TARGET desktop-pet-controller-qt-poc POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE_POC}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:desktop-pet-controller-qt-poc>"
            COMMENT "Running windeployqt for desktop-pet-controller-qt-poc")
    else()
        message(WARNING "windeployqt not found under ${_qt_bin_dir}; "
                        "the PoC binary may not run standalone on Windows.")
    endif()
endif()

# enable_testing() is present so QTest targets under tests/ register via
# add_subdirectory(tests) without touching this file again.
enable_testing()
add_subdirectory(tests)
