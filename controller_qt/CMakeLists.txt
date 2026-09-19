cmake_minimum_required(VERSION 3.22)

project(controller_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6.8 LTS minimum — accepts newer (e.g. 6.10.0). The Qt install location is
# supplied at configure time (CMAKE_PREFIX_PATH via the presets toolchain); do
# NOT hardcode the Qt path here.
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
    Sql
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

# Unified windeployqt POST_BUILD helpers (P1b, §C.3): pet_deploy_qt_test()
# for the test exes (called from tests/CMakeLists.txt) and pet_deploy_qt_app()
# for the main exe below. No-op on non-Windows hosts.
include(cmake/DeployQtTest.cmake)

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

# miniz (P5 §B.4): zip reading/writing for the plugin install pipeline.
# Design doc named Qt's private QZipReader, but the private headers are not
# shipped by the Qt distributions this tree builds against (verified: no
# qzipreader_p.h on the Linux CI host; qt6-base-private-dev absent). miniz
# is the drop-in replacement behind the same ZipArchive isolation TU —
# swapping back to QZipReader later is a one-file change (see
# src/core/ZipArchive.cpp's replaceability note).
# miniz 3.0.2 predates CMake's <3.5 compatibility removal; the policy knob
# is the documented escape hatch (upstream master fixes it; unpin then).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG 3.0.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(miniz)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# ── P2c target layout (docs/refactor/plugin-architecture-and-cmake-migration.md
# §B.1) ─────────────────────────────────────────────────────────────────────
# pet_panel_api  INTERFACE — plugin SDK scaffolding (headers land in P4)
# pet_panel_core STATIC   — ALL production code except main.cpp + the QML
#                          module backing target (deliberate §B.1 decision:
#                          core accepts Qt6::Quick; ui/ lives in core anyway)
# exe shell      desktop-pet-controller-qt — main.cpp only (composition-root
#                          slimming is P3's job, content untouched here)
# Output lands in <root>/build/bin next to the renderer so the WebSocket/port
# + relative resource paths line up with the rest of the app.
set(_bin_output_dir "${PROJECT_SOURCE_DIR}/../build/bin")

# ── pet_panel_api (INTERFACE) ───────────────────────────────────────────────
# Plugin SDK headers (P4, §B.3). INTERFACE on purpose: zero link products,
# never two copies of an interface implementation, shared by the static
# (stage 1) and dynamic (stage 2) plugin models. pet_panel_core implements
# the context/apis against these headers; plugins (plugins/*/) program
# against them exclusively.
add_library(pet_panel_api INTERFACE)
target_sources(pet_panel_api INTERFACE
    src/api/PluginTypes.hpp
    src/api/IPanelPlugin.hpp
    src/api/IPluginContext.hpp
    src/api/IInstanceApi.hpp
    src/api/IVoicePackApi.hpp
    src/api/IDownloadApi.hpp
    src/api/IUiApi.hpp
)
# ABI discipline (§B.3, src/api/README.md): pure-virtual declarations, Qt
# value types + POD only. Qt value types need Qt6::Core's include path —
# INTERFACE so consumers inherit it. Interface headers live in src/ so the
# include form is "api/IPanelPlugin.hpp", matching the core/app tree.
target_include_directories(pet_panel_api INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(pet_panel_api INTERFACE Qt6::Core)

# ── pet_panel_core (STATIC) ─────────────────────────────────────────────────
# Every production TU except main.cpp. Linkage/compile-def/include-dir
# requirements are PUBLIC so the 41 tests (tests/CMakeLists.txt) and the exe
# shell consume them by linking this one library instead of re-listing sources.
qt_add_library(pet_panel_core STATIC
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
    src/ui/VoicePackController.cpp
    src/ui/VoicePackController.hpp
    src/core/InstanceSessionMonitor.cpp
    # SQLite config backend: DatabaseManager owns <configDir>/app.db (schema:
    # panel_config kv, instance_configs, assets, asset_refs). PanelStateManager
    # + InstanceConfigManager persist through it (no JSON files, no migration).
    # AssetManager is the QML bridge for the image library + logo/instance-icon
    # refs. The .hpp files are listed so AUTOMOC processes Q_OBJECT metadata.
    src/core/DatabaseManager.cpp
    src/core/DatabaseManager.hpp
    src/core/AssetManager.cpp
    src/core/AssetManager.hpp
    # Bubble stream (气泡信息流): the shared bubble list model + the QML
    # bridge exposed as the "notificationStream" context property (Main.qml
    # instantiates BubbleStreamWindow). Text source is the voice-pack
    # behavior engine's dialogue sink (design revision: no dialogue-pack
    # system). The .hpp files are listed so AUTOMOC processes their Q_OBJECT
    # metadata.
    src/ui/NotificationStreamModel.cpp
    src/ui/NotificationStreamModel.hpp
    src/ui/NotificationStreamController.cpp
    src/ui/NotificationStreamController.hpp
    # Plugin framework (P4, §B.3/§B.5/§B.6): registry (manifest validation
    # + state machine), host (initialize/shutdown with §A.1 exception
    # isolation + ≤200ms budget), manager (QML bridge model + kv-persisted
    # enable bits), page model (nav rows), per-plugin bridge + context impl,
    # and the P5 capability stubs. The .hpp files are listed so AUTOMOC
    # processes their Q_OBJECT metadata.
    src/core/PluginRegistry.cpp
    src/core/PluginRegistry.hpp
    src/core/PluginHost.cpp
    src/core/PluginHost.hpp
    src/core/PluginManager.cpp
    src/core/PluginManager.hpp
    src/core/PluginPageModel.cpp
    src/core/PluginPageModel.hpp
    src/core/PluginBridge.cpp
    src/core/PluginBridge.hpp
    src/core/PluginContextImpl.cpp
    src/core/PluginContextImpl.hpp
    src/core/PluginCapabilityStubs.cpp
    src/core/PluginCapabilityStubs.hpp
    # P5 §B.4 host download pipeline: the process-wide download chokepoint
    # (one QNetworkAccessManager inside the production reply factory; the
    # factory is the injectable test seam for offline/error paths).
    src/core/DownloadService.cpp
    src/core/DownloadService.hpp
    # P5 commit 2: install pipeline (zip-slip-guarded archive → temp dir →
    # atomic per-file writes → VoicePacks move) + the zip MECHANISM
    # isolation TU (miniz-backed; see ZipArchive.cpp replaceability note).
    src/core/DownloadServiceInstall.cpp
    src/core/ZipArchive.cpp
    src/core/ZipArchive.hpp
)

# Expose src/ so consumers (exe shell + 41 tests + future plugins) can write
# `#include "network/Envelope.hpp"` etc. PUBLIC: this used to be per-target
# target_include_directories in the exe and every test.
target_include_directories(pet_panel_core PUBLIC src)

# QGuiApplication (QtGui) and QQmlApplicationEngine (QtQml/QtQuick) are not
# auto-linked, so link them explicitly. Core/Network are transitive deps of
# Qml/Quick but listed for clarity. PUBLIC so tests linking pet_panel_core
# resolve the same modules their production includes need (previously each
# test re-listed the subset it recompiled).
target_link_libraries(pet_panel_core PUBLIC
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
    # --modules charts flag needed in the windeployqt invocation.
    Qt6::Charts
    # SQLite config backend (app.db) + asset metadata store.
    Qt6::Sql
    # spdlog PUBLIC: production headers (logging/Logging.hpp etc.) include
    # spdlog headers and tests compile TUs that use LOG_*/CapturingSink
    # against them.
    spdlog::spdlog
    # miniz PUBLIC (P5): ZipArchive isolates it in one TU, but the install
    # tests link pet_panel_core and use mz_zip_writer for fixtures.
    miniz
)

# Force the spdlog *active* macros (SPDLOG_DEBUG/INFO/... which LOG_* wrap) to
# emit at debug level by default so LOG_DEBUG is live in dev builds. Release
# builds can override by appending -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO.
# PUBLIC: the 41 tests previously set the same definition per-target.
target_compile_definitions(pet_panel_core PUBLIC
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# Windows-only system-library link for ResourceStatsCollector (todo 17):
# GetProcessMemoryInfo lives in Psapi.lib. MinGW ships libpsapi.a so the
# plain `psapi` link name resolves. No-op on Linux (/proc needs no link).
if(WIN32)
    target_link_libraries(pet_panel_core PRIVATE psapi)
endif()

# QML module — backing target is pet_panel_core (§B.1 decision, 2026-09-19:
# core accepts the Qt6::Quick dependency; ui/ already lives in core so the
# registration point moved by renaming the host target, not by relocating
# code). Main.qml is auto-registered as type "Main" under URI DesktopPet,
# loadable via QQmlApplicationEngine::loadFromModule("DesktopPet", "Main").
# T24: pages/ holds the StackView pages. qt_add_qml_module flattens
# subdirectory paths — each file's basename becomes its registered type name,
# so WelcomePage.qml is usable as `WelcomePage {}` after `import DesktopPet`.
#
# Theme.qml is `pragma Singleton` — on clean builds with Qt 6.10 + QTP0001 NEW,
# the auto-generated qmldir does NOT pick up `pragma Singleton` and registers
# Theme as a plain component (its instance properties — bgColor, setTheme, … —
# are then undefined at runtime). The explicit QT_QML_SINGLETON_TYPE property
# forces the `singleton` keyword in qmldir so Theme works as a singleton.
#
# NOTE (P2c risk, §D P2): attaching the module to a STATIC library changes
# how the generated module plugin reaches the exe — qt_import_qml_plugins on
# the exe links it transitively via pet_panel_core. Runtime verification gate:
# --screenshot full-page run with zero "unknown type"/"is not installed" in
# the logs (compile-time checks cannot catch this class of regression).
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(pet_panel_core
    URI DesktopPet
    VERSION 1.0
        QML_FILES
qml/Theme.qml
qml/Main.qml
qml/components/AppButton.qml
  qml/components/AppComboBox.qml
  qml/components/AppDialog.qml
  qml/components/CreateInstanceDialog.qml
qml/components/AppScrollBar.qml
qml/components/AppSlider.qml
qml/components/Card.qml
qml/components/Chip.qml
qml/components/Segmented.qml
qml/components/SettingRow.qml
qml/components/Sparkline.qml
qml/components/StatusPill.qml
qml/components/ToggleSwitch.qml
qml/pages/WelcomePage.qml
qml/pages/InstanceDetailPage.qml
qml/pages/SettingsPage.qml
qml/pages/MonitorPage.qml
qml/pages/VoicePackPage.qml
qml/pages/AssetPage.qml
qml/BubbleStreamWindow.qml
qml/components/BubbleCard.qml
)

# ── Static plugin pipeline (P4, §B.6 two-phase loading) ────────────────────
# Stage 1: compile-time plugins. plugins/<name>/plugin.json is the unit of
# discovery; this section (1) validates manifests at CONFIGURE time —
# field existence + api_version shape only, string(REGEX)-level; the full
# schema authority is the runtime PluginRegistry — corrupt manifest =
# FATAL_ERROR (fail fast at the earliest possible point, §B.6 v2);
# (2) builds each plugin via its own CMakeLists (defines
# pet_plugin_<safe> and exports its qrc:/ page URL into the parent scope);
# (3) generates static_plugins.cpp — the id→create() factory table + the
# §A.2 abi block (compiler/qt/build_type) generated from THIS build's
# toolchain, never hand-written. Compile-time plugins are abi-EXEMPT at
# runtime (§A.2) — the embedded abi documents the build, ready for stage 2.
set(_plugin_targets "")
set(_plugin_safes "")
file(GLOB _plugin_manifests "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/plugin.json")
# The generated factory TU opens here; rows are appended DIRECTLY inside
# the discovery loop. (Never carry generated code through CMake list
# variables: unquoted expansion splits elements on every ';' and the
# foreach iteration would shred the row — quoted file(APPEND) arguments
# are the only path that preserves semicolons verbatim.)
set(_static_plugins_file "${CMAKE_CURRENT_BINARY_DIR}/generated/static_plugins.cpp")
file(WRITE "${_static_plugins_file}"
"// GENERATED by controller_qt/CMakeLists.txt (P4 static plugin pipeline)
// — DO NOT EDIT. One row per plugins/*/plugin.json; id → create() factory.
#include \"api/IPanelPlugin.hpp\"
#include \"core/PluginRegistry.hpp\"
#include <QString>

")
foreach(_manifest ${_plugin_manifests})
    get_filename_component(_plugdir "${_manifest}" DIRECTORY)
    get_filename_component(_dirname "${_plugdir}" NAME)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" _safe "${_dirname}")
    file(READ "${_manifest}" _raw)
    # (1) configure-time gate: required fields exist, api_version numeric
    #     (string(REGEX) level; full schema authority = runtime Registry).
    foreach(_field id version api_version capabilities)
        string(REGEX MATCH "\"${_field}\"[ \t]*:" _hit "${_raw}")
        if(_hit STREQUAL "")
            message(FATAL_ERROR
                "Plugin manifest ${_manifest} is missing required field "
                "'${_field}'. Configure-time check covers field existence; "
                "full schema validation runs in PluginRegistry at runtime.")
        endif()
    endforeach()
    string(REGEX MATCH "\"api_version\"[ \t]*:[ \t]*\"[0-9]+\\.[0-9]+\"" _av "${_raw}")
    if(_av STREQUAL "")
        message(FATAL_ERROR "Plugin manifest ${_manifest}: 'api_version' must "
            "be a \"<major>.<minor>\" string of non-negative integers.")
    endif()
    # (2) build the plugin; its CMakeLists sets in PARENT_SCOPE:
    #     <SAFE>_QML_URL (qrc:/ URL from its qml module) + optionally
    #     <SAFE>_EXTRA_TARGETS (its qml-module static plugin).
    add_subdirectory("${_plugdir}" "${CMAKE_CURRENT_BINARY_DIR}/plugins/${_dirname}")
    list(APPEND _plugin_targets pet_plugin_${_safe})
    list(APPEND _plugin_safes ${_safe})
    # The plugin's qml module registers its qrc through a generated static
    # plugin (<target>plugin) — the EXE must link it too (P2c mechanism).
    list(APPEND _plugin_targets ${${_safe}_EXTRA_TARGETS})
    # (3) factory declaration + row. The §A.2 abi block is generated from
    #     THIS build's toolchain, never hand-written.
    if(MINGW)
        set(_abi_qt_build "mingw_64")
    elseif(MSVC)
        set(_abi_qt_build "msvc_64")
    else()
        set(_abi_qt_build "linux_gcc_64")
    endif()
    set(_abi_json "{\"compiler\":\"${CMAKE_CXX_COMPILER_ID}\",\"compiler_version\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"qt_version\":\"${Qt6_VERSION}\",\"qt_build\":\"${_abi_qt_build}\",\"build_type\":\"${CMAKE_BUILD_TYPE}\"}")
    file(APPEND "${_static_plugins_file}"
"pet::IPanelPlugin* pet_plugin_create_${_safe}();
")
endforeach()
file(APPEND "${_static_plugins_file}"
"
void pet_register_static_plugins(core::PluginRegistry& registry)
{
")
# Rows in a second pass over the manifests (same order as above): appended
# as single quoted arguments — semicolons in generated code stay intact.
foreach(_manifest ${_plugin_manifests})
    get_filename_component(_plugdir "${_manifest}" DIRECTORY)
    get_filename_component(_dirname "${_plugdir}" NAME)
    string(REGEX REPLACE "[^A-Za-z0-9]" "_" _safe "${_dirname}")
    file(READ "${_manifest}" _raw)
    if(MINGW)
        set(_abi_qt_build "mingw_64")
    elseif(MSVC)
        set(_abi_qt_build "msvc_64")
    else()
        set(_abi_qt_build "linux_gcc_64")
    endif()
    set(_abi_json "{\"compiler\":\"${CMAKE_CXX_COMPILER_ID}\",\"compiler_version\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"qt_version\":\"${Qt6_VERSION}\",\"qt_build\":\"${_abi_qt_build}\",\"build_type\":\"${CMAKE_BUILD_TYPE}\"}")
    file(APPEND "${_static_plugins_file}"
"    registry.addStaticPlugin(QStringLiteral(R\"json(
${_raw})json\"),
        pet_plugin_create_${_safe},
        QStringLiteral(R\"abi(${_abi_json})abi\"),
        QStringLiteral(\"${${_safe}_QML_URL}\"));
")
endforeach()
file(APPEND "${_static_plugins_file}"
"}
")
# Re-run configure when manifests change (CMake only tracks CMakeLists).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_plugin_manifests}")

# ── exe shell ───────────────────────────────────────────────────────────────
# Composition root (P3). M1: the visual-QA modes (--screenshot/--bubble) are
# carved out of main.cpp into src/app/ScreenshotRunner — app-layer code,
# deliberately NOT in pet_panel_core (it drives the QApplication event loop
# and std::exit()s; not library material). M2-M4 continue the carve-out.
qt_add_executable(desktop-pet-controller-qt
    src/main.cpp
    src/app/ScreenshotRunner.cpp
    src/app/PanelApplication.cpp
    src/app/PanelUiBoot.cpp
    src/app/AppFontGuard.cpp
    # P4: CMake-generated static plugin factory table (see the pipeline
    # section above; defines pet_register_static_plugins — exe-side because
    # core cannot reference plugin symbols without a link cycle).
    "${_static_plugins_file}"
)

set_target_properties(desktop-pet-controller-qt PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_bin_output_dir}"
    WIN32_EXECUTABLE ON
)

target_link_libraries(desktop-pet-controller-qt PRIVATE
    pet_panel_core
    # P2c: the QML module's backing target is pet_panel_core, so the
    # generated static module plugin (pet_panel_coreplugin + its _init object
    # library, which force-runs Q_INIT_RESOURCE for :/qt/qml/DesktopPet) must
    # be linked explicitly — with an executable backing target this happened
    # implicitly. Symptom if missing (verified): runtime "No module named
    # \"DesktopPet\" found" with zero compile-time errors.
    pet_panel_coreplugin
    # P4: plugin STATIC libraries (pet_plugin_<dir>); empty list when the
    # tree has no plugins.
    ${_plugin_targets}
)

# windeployqt for the main exe on Windows (P1b): replicates the exact
# post-build deployment invocation of the retired Python build orchestrator
# (see pre-P1b git history) — full
# deployment with --qmldir over the QML sources so the panel ships standalone
# from build/bin/. windeployqt auto-detects Qt6Charts.dll via the exe's import
# table (no --modules flag needed).
pet_deploy_qt_app(desktop-pet-controller-qt)

# CJK font deployment (tofu guard, pairs with installDefaultFontWithCjk() in
# src/main.cpp): the font SINGLE SOURCE OF TRUTH lives in
# renderer/resources/fonts (OFL-licensed). The controller deploys its own copy
# next to its exe — deliberately NOT relying on the renderer's POST_BUILD, so
# build/bin/fonts is populated regardless of which component builds first (no
# cross-component build-order coupling). Runs on every platform: a missing
# system CJK font is not Windows-specific.
add_custom_command(TARGET desktop-pet-controller-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:desktop-pet-controller-qt>/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/../renderer/resources/fonts/NotoSansCJKsc-Regular.otf"
        "$<TARGET_FILE_DIR:desktop-pet-controller-qt>/fonts/NotoSansCJKsc-Regular.otf"
    COMMENT "Deploying Noto Sans CJK SC next to the controller exe")

# enable_testing() is present so QTest targets under tests/ register via
# add_subdirectory(tests) without touching this file again.
enable_testing()
add_subdirectory(tests)
