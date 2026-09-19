# Protocol fixture tests (QTest).
#
# T3 adds a single test binary, ProtocolFixturesTest, that loads the JSON
# fixtures under protocol/ and asserts the structural invariants every later
# task (T4 Envelope ser/deser, T7-T11 router/handshake/factory/event) relies on.
#
# The fixture directory is baked in at compile time via PROTOCOL_FIXTURES_DIR so
# the test finds the JSON files regardless of where the binary is launched from
# (avoids QFINDTESTDATA deployment complexity — see learnings.md T3 notes).

# ── windeployqt unification (P1b, §C.3) ─────────────────────────────────────
# Every test exe below deploys its Qt runtime via one call to
# pet_deploy_qt_test(<target>) (cmake/DeployQtTest.cmake) instead of the
# 41 copy-pasted if(WIN32) blocks that used to inline the same
# find_program + POST_BUILD logic (152 windeployqt occurrences). The
# function is a no-op on non-Windows hosts.
include(${CMAKE_SOURCE_DIR}/cmake/DeployQtTest.cmake)

# The test exe links Qt6::Test (registered as a REQUIRED component in the parent
# CMakeLists.txt) plus Qt6::Core. QtGui is not needed: ProtocolFixturesTest is a
# pure JSON-parsing test with no GUI, so QTEST_APPLESS_MAIN is used.
qt_add_executable(ProtocolFixturesTest ProtocolFixturesTest.cpp)
target_link_libraries(ProtocolFixturesTest PRIVATE Qt6::Core Qt6::Test)

# Embed the absolute source path of the protocol/ fixture directory so the test
# binary can locate the JSON files from any working directory (incl. ctest).
target_compile_definitions(ProtocolFixturesTest PRIVATE
    PROTOCOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/protocol")

pet_deploy_qt_test(ProtocolFixturesTest)

add_test(NAME ProtocolFixturesTest COMMAND ProtocolFixturesTest)

# Envelope model ser/deser (T4). The test links the production Envelope.cpp
# directly (controller_qt has no shared library yet, so each test binary
# recompiles the .cpp it exercises). PROTOCOL_FIXTURES_DIR is reused so the
# test finds the T3 JSON fixtures from any working directory, including ctest.
qt_add_executable(EnvelopeTest EnvelopeTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(EnvelopeTest PRIVATE Qt6::Core Qt6::Test)
target_include_directories(EnvelopeTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(EnvelopeTest PRIVATE
    PROTOCOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/protocol")

pet_deploy_qt_test(EnvelopeTest)

add_test(NAME EnvelopeTest COMMAND EnvelopeTest)

# Logging infrastructure (T2): spdlog rotating-file logger + qInstallMessageHandler
# bridge. The test recompiles the production Logging.cpp directly (controller_qt
# has no shared library yet, same pattern as EnvelopeTest). spdlog::spdlog comes
# from the FetchContent block in the parent CMakeLists.txt and is visible here.
# Uses QTEST_MAIN (QCoreApplication) because qInfo() needs an app to route
# through the installed message handler.
qt_add_executable(LoggingTest LoggingTest.cpp
    ${CMAKE_SOURCE_DIR}/src/logging/Logging.cpp)
target_link_libraries(LoggingTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(LoggingTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
# Match the main app: LOG_DEBUG (SPDLOG_DEBUG) must be live in the test too.
target_compile_definitions(LoggingTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(LoggingTest)

add_test(NAME LoggingTest COMMAND LoggingTest)

# WsServer (T5): real QWebSocketServer + QWebSocket client loop on 127.0.0.1
# with an OS-assigned port. The test recompiles WsServer.cpp + Envelope.cpp
# directly (controller_qt has no shared lib yet, same pattern as EnvelopeTest).
# Needs Qt6::WebSockets (both server and the in-test QWebSocket client) plus
# Qt6::Network (transitive). Uses QTEST_MAIN (QCoreApplication) — WebSocket I/O
# needs the event loop that QSignalSpy::wait pumps; QTEST_APPLESS_MAIN cannot.
qt_add_executable(WsServerTest WsServerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(WsServerTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(WsServerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(WsServerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(WsServerTest)

add_test(NAME WsServerTest COMMAND WsServerTest)

# PoC integration test (T6): runs the desktop-pet-controller-qt-poc binary as a
# subprocess and asserts the full WS↔renderer round-trip completes (exit 0 +
# "PoC SUCCESS"). The PoC + renderer binaries live in build/bin/, NOT next to
# this test exe, so BIN_OUTPUT_DIR is baked at compile time to locate them
# regardless of CWD (same PROTOCOL_FIXTURES_DIR pattern). The test links only
# Qt6::Core + Qt6::Test: it launches the PoC via QProcess, not via WsServer.
qt_add_executable(PoCIntegrationTest PoCIntegrationTest.cpp)
target_link_libraries(PoCIntegrationTest PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(PoCIntegrationTest PRIVATE
    BIN_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/../build/bin")

pet_deploy_qt_test(PoCIntegrationTest)

add_test(NAME PoCIntegrationTest COMMAND PoCIntegrationTest)
# REQUIRES_RENDERER label lets ctest filter this out of headless/CI runs where
# no display or renderer binary is present (the test also QSKIPs at runtime if
# either binary is absent).
set_tests_properties(PoCIntegrationTest PROPERTIES LABELS "REQUIRES_RENDERER")

# MessageDispatcher (T7): pure-logic router — type-based dispatch (response→
# responseHandler, event→ handler keyed by action, command→ warn), handler
# isolation (try/catch around every handler), and the §7.3 pseudo-response rule
# (stats_state/layout_state route by ACTION, never by id). The test recompiles
# MessageDispatcher.cpp + Envelope.cpp directly (controller_qt has no shared
# lib yet, same pattern as the other tests). Links Qt6::Core + Qt6::Test +
# spdlog (LOG_* macros). QTEST_APPLESS_MAIN — no event loop needed; dispatch()
# is called synchronously as a plain method.
qt_add_executable(MessageDispatcherTest MessageDispatcherTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/MessageDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(MessageDispatcherTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(MessageDispatcherTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MessageDispatcherTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(MessageDispatcherTest)

add_test(NAME MessageDispatcherTest COMMAND MessageDispatcherTest)

# Protocol typed factories (T9): 9 startup-salvo builders + set_hit_areas +
# shutdown, each wrapping createCommand with the exact payload fields from
# interface.md §5. The test recompiles Protocol.cpp + Envelope.cpp directly
# (same pattern as EnvelopeTest/MessageDispatcherTest — no shared lib yet).
# Links Qt6::Core + Qt6::Test + spdlog (the test asserts Envelope invariants;
# spdlog is linked for parity with the other network-layer tests so LOG_* is
# available if Protocol.cpp ever grows instrumentation). QTEST_APPLESS_MAIN —
# no event loop needed; the builders are pure functions.
qt_add_executable(ProtocolFactoryTest ProtocolFactoryTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(ProtocolFactoryTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(ProtocolFactoryTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ProtocolFactoryTest PRIVATE
    PROTOCOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/protocol"
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(ProtocolFactoryTest)

add_test(NAME ProtocolFactoryTest COMMAND ProtocolFactoryTest)

# 3-gate token handshake + connection management (T8). Exercises the full
# handshake (Origin 4001 / instance_id 4000 / token 4002 / good creds /
# connection-replace 1000 / ready-timeout) with a real QWebSocket client against
# a real WsServer on 127.0.0.1 with an OS-assigned port. Recompiles WsServer.cpp
# + Envelope.cpp directly (same pattern as WsServerTest — no shared lib yet).
# Links Qt6::WebSockets + Qt6::Network (server + in-test client) + spdlog (LOG_*).
# QTEST_MAIN — needs the event loop that QSignalSpy::wait pumps.
qt_add_executable(HandshakeTest HandshakeTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(HandshakeTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(HandshakeTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(HandshakeTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(HandshakeTest)

add_test(NAME HandshakeTest COMMAND HandshakeTest)

# PendingRequests (T10): the pending-request table — expectResponse installs a
# per-entry single-shot QTimer (10s default / interface.md §7.2), handleResponse
# completes the matching entry by id (wired from MessageDispatcher's response
# handler), and a missing response times out. The test recompiles
# PendingRequests.cpp + Envelope.cpp directly (same pattern as
# MessageDispatcherTest — no shared lib yet). Links Qt6::Core + Qt6::Test +
# spdlog (LOG_* macros + CapturingSink for the unknown-id WARN assertion).
# QTEST_MAIN (NOT APPLESS) — QTimer requires the QCoreApplication event loop
# that QSignalSpy::wait pumps for the timeout test.
qt_add_executable(PendingRequestsTest PendingRequestsTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/PendingRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(PendingRequestsTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(PendingRequestsTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PendingRequestsTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(PendingRequestsTest)

add_test(NAME PendingRequestsTest COMMAND PendingRequestsTest)

# EventRegistry (T11): thin convenience wrapper over MessageDispatcher exposing
# registerEventHandler / unregisterEventHandler / registerDefaults (default
# log-only handler for ALL 13 protocol events from interface.md §4.1). The test
# recompiles EventRegistry.cpp + MessageDispatcher.cpp + Envelope.cpp directly
# (same pattern as MessageDispatcherTest — no shared lib yet). Links Qt6::Core
# + Qt6::Test + spdlog (LOG_* macros + CapturingSink for the registerDefaults
# assertion). QTEST_APPLESS_MAIN — no event loop needed; dispatch() is called
# synchronously as a plain method through the registry's dispatcher dependency.
qt_add_executable(EventRegistryTest EventRegistryTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/EventRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/network/MessageDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(EventRegistryTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(EventRegistryTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(EventRegistryTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(EventRegistryTest)

add_test(NAME EventRegistryTest COMMAND EventRegistryTest)

# Thread marshaling (T12): WS I/O → Qt main thread. ThreadMarshal.hpp is a
# header-only template (marshalToMain + ASSERT_MAIN_THREAD), so this test
# exercises it via WsServer → MessageDispatcher integration. The test
# recompiles WsServer.cpp + MessageDispatcher.cpp + Envelope.cpp directly
# (same pattern as the other network-layer tests — no shared lib yet).
# Links Qt6::WebSockets + Qt6::Network (the WS integration case spins a real
# QWebSocket client) + spdlog (LOG_* macros used by WsServer/MessageDispatcher).
# QTEST_MAIN (NOT APPLESS) — marshalToMain posts a queued event that only the
# QCoreApplication event loop delivers; QSignalSpy::wait / QTest::qWaitFor pump
# that loop. This is the Wave 2 capstone test (T12, 11th test).
qt_add_executable(ThreadMarshalTest ThreadMarshalTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/MessageDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(ThreadMarshalTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(ThreadMarshalTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ThreadMarshalTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(ThreadMarshalTest)

add_test(NAME ThreadMarshalTest COMMAND ThreadMarshalTest)

# PathResolve (T14): backend-aware renderer path resolver — "vulkan" maps to
# desktop-pet-renderer-vulkan, "opengl"/empty/unknown maps to
# desktop-pet-renderer, with ".exe" appended on Windows (architecture-
# blueprint.md §9.4). Returns std::nullopt + WARN when the file is missing.
# Consumed by T13 ProcessManager and T27 welcome-page env detection.
#
# The test recompiles PathResolve.cpp directly (same pattern as the other
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (LOG_WARN
# macro in PathResolve.cpp routes through spdlog's default logger when
# Logging::init() has not been called — sufficient for the missing-file WARN).
# QTEST_APPLESS_MAIN — QTemporaryDir + QFile::exists() are synchronous, no
# event loop needed; defaultRendererDir() (which needs QCoreApplication) is
# not exercised because its result depends on the test binary's location.
qt_add_executable(PathResolveTest PathResolveTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PathResolve.cpp)
target_link_libraries(PathResolveTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(PathResolveTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PathResolveTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(PathResolveTest)

add_test(NAME PathResolveTest COMMAND PathResolveTest)

# ProcessManager (T13): QProcess wrapper managing the renderer process
# lifecycle — start with CLI args, pump stdout/stderr to spdlog, emit
# exited(code, crashed) on finish. The test has two tiers: unit (CLI args
# construction + initial state, no renderer needed) and integration (launches
# the REAL renderer, asserts `ready` via WsServer, verifies stdout pump + crash
# detection). The integration tier QSKIPs at runtime if the renderer binary is
# absent and carries the REQUIRES_RENDERER label for ctest filtering.
#
# Recompiles ProcessManager.cpp + WsServer.cpp + PathResolve.cpp + Envelope.cpp
# directly (same pattern as the other tests — no shared lib yet). Links
# Qt6::Core + Qt6::Network + Qt6::WebSockets (WsServer integration cases) +
# Qt6::Test + spdlog (LOG_* macros in ProcessManager/PathResolve/WsServer).
# QTEST_MAIN (NOT APPLESS) — QProcess + QSignalSpy::wait need the event loop.
qt_add_executable(ProcessManagerTest ProcessManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ProcessManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PathResolve.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp)
target_link_libraries(ProcessManagerTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(ProcessManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ProcessManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    BIN_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/../build/bin")

pet_deploy_qt_test(ProcessManagerTest)

add_test(NAME ProcessManagerTest COMMAND ProcessManagerTest)
# REQUIRES_RENDERER label lets ctest filter this out of headless/CI runs where
# no display or renderer binary is present (the integration tier also QSKIPs at
# runtime if the renderer binary is absent; the unit tier always runs).
set_tests_properties(ProcessManagerTest PROPERTIES LABELS "REQUIRES_RENDERER")

# StartupSalvo (T16): the 7-command bootstrap salvo + set_hit_areas. Two tiers:
#   - Unit tier (3 slots, no renderer): capturing-sender harness asserts the 7
#     commands fire in order, that set_scale NEVER appears in the stream, and
#     that every salvo action is in the command catalog fixture.
#   - Integration tier (1 slot, REQUIRES_RENDERER): WsServer + ProcessManager +
#     StartupSalvo wired against the real renderer; ready → sendSalvo,
#     model_loaded → sendSetHitAreas, asserts 8 commands captured in order
#     with no set_scale. Tolerates the known renderer teardown crash (T6/T13).
# Recompiles StartupSalvo.cpp + Protocol.cpp + WsServer.cpp + ProcessManager.cpp
# + PathResolve.cpp + Envelope.cpp directly (same pattern as the other tests —
# no shared lib yet). Links Qt6::Core + Qt6::Network + Qt6::WebSockets (server
# + integration tier) + Qt6::Test + spdlog (LOG_* macros in StartupSalvo /
# WsServer / ProcessManager / PathResolve). QTEST_MAIN (NOT APPLESS) — the
# integration tier uses QSignalSpy::wait / QTest::qWait to pump the event loop.
qt_add_executable(StartupSalvoTest StartupSalvoTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StartupSalvo.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ProcessManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PathResolve.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(StartupSalvoTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(StartupSalvoTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(StartupSalvoTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    PROTOCOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/protocol"
    BIN_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/../build/bin")

pet_deploy_qt_test(StartupSalvoTest)

add_test(NAME StartupSalvoTest COMMAND StartupSalvoTest)
# REQUIRES_RENDERER label lets ctest filter this out of headless/CI runs where
# no display or renderer binary is present (the integration tier also QSKIPs at
# runtime if the renderer binary is absent; the unit tier always runs).
set_tests_properties(StartupSalvoTest PROPERTIES LABELS "REQUIRES_RENDERER")

# ConfigDir (T17): cross-platform config directory manager — configDir() /
# instancesDir() / logsDir() accessors + ensureDirectories(basePath) tree
# creator. The accessor paths are home-relative and use '/' on every platform
# (byte-for-byte compatible with the legacy config format under the user's
# home). ensureDirectories accepts an
# injectable base path so the tree-creation behavior is exercised against a
# QTemporaryDir without touching the user's real ~/.config/desktop-pet.
#
# The test recompiles ConfigDir.cpp directly (same pattern as the other tests —
# no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (ConfigDir.cpp
# includes logging/Logging.hpp which references the SPDLOG_* macros — the LOG_WARN
# branch in ensureDirectories routes through spdlog's default logger when
# Logging::init() has not been called). QTEST_APPLESS_MAIN — QTemporaryDir +
# QDir::exists() + QString::contains() are synchronous, no event loop needed.
qt_add_executable(ConfigDirTest ConfigDirTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(ConfigDirTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(ConfigDirTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ConfigDirTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(ConfigDirTest)

add_test(NAME ConfigDirTest COMMAND ConfigDirTest)

# InstanceConfig (T18): the 28-field persisted instance config model — snake_case
# JSON serde (byte-for-byte compatible with the legacy config format).
# instanceConfigToJson / instanceConfigFromJson /
# defaultInstanceConfig. Missing fields merge from struct defaults; unknown
# fields are silently ignored (interface.md §1.5); garbage/empty/mistyped JSON
# returns a default-constructed InstanceConfig without throwing. Consumed by
# T20 (InstanceConfigManager) and T28 (window-state restore); supersedes the
# minimal InstanceConfigLike view that StartupSalvo (T16) used provisionally.
#
# The test recompiles InstanceConfig.cpp directly (same pattern as the other
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (InstanceConfig.cpp includes logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL). QTEST_APPLESS_MAIN
# — QJsonObject manipulation is synchronous, no event loop needed.
qt_add_executable(InstanceConfigTest InstanceConfigTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp)
target_link_libraries(InstanceConfigTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(InstanceConfigTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InstanceConfigTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(InstanceConfigTest)

add_test(NAME InstanceConfigTest COMMAND InstanceConfigTest)

# PanelConfig (T19): the 12-field persisted panel UI-state config model —
# snake_case JSON serde (byte-for-byte compatible with the legacy config
# format). panelConfigToJson / panelConfigFromJson
# / defaultPanelConfig. Missing fields merge from struct defaults; unknown fields
# are silently ignored (interface.md §1.5); garbage/empty/mistyped JSON returns a
# default-constructed PanelConfig without throwing. instance_ids is a JSON array
# of strings ↔ QStringList. Consumed by T21 (PanelStateManager) and T28
# (window-state restore).
#
# The test recompiles PanelConfig.cpp directly (same pattern as the other tests
# — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (PanelConfig.cpp
# includes logging/Logging.hpp which references the SPDLOG_* macros; spdlog is
# statically linked, no extra DLL). QTEST_APPLESS_MAIN — QJsonObject/QJsonArray
# manipulation is synchronous, no event loop needed.
qt_add_executable(PanelConfigTest PanelConfigTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp)
target_link_libraries(PanelConfigTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(PanelConfigTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PanelConfigTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(PanelConfigTest)

add_test(NAME PanelConfigTest COMMAND PanelConfigTest)

# InstanceConfigManager (T20): per-instance JSON CRUD — save (atomic .tmp +
# rename via QSaveFile), load (returns std::nullopt for missing/corrupt),
# deleteInstance (idempotent), loadAll (skips corrupt files with a WARN,
# returns the list sorted by id). Atomic-write guarantee is verified by
# planting a stale .tmp and asserting the real file is unchanged.
#
# The test recompiles InstanceConfigManager.cpp + InstanceConfig.cpp +
# ConfigDir.cpp directly (same pattern as the other core tests — no shared
# lib yet). Links Qt6::Core + Qt6::Test + spdlog (InstanceConfigManager.cpp
# includes logging/Logging.hpp which references the SPDLOG_* macros; spdlog
# is statically linked, no extra DLL). QTEST_APPLESS_MAIN — QSaveFile /
# QFile / QDir::entryList are synchronous, no event loop needed.
qt_add_executable(InstanceConfigManagerTest InstanceConfigManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(InstanceConfigManagerTest PRIVATE
    Qt6::Core Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(InstanceConfigManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InstanceConfigManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(InstanceConfigManagerTest)

add_test(NAME InstanceConfigManagerTest COMMAND InstanceConfigManagerTest)

# PanelStateManager (T21): panel.json load/save + legacy panel-state.json
# migration. Five behaviors: fresh→defaults-written, existing-panel.json-load,
# legacy-migration (panel.json + instances/*.json created, old file → .bak),
# unparseable-legacy→defaults+preserved, save→load round-trip. Recompiles
# PanelStateManager.cpp + PanelConfig.cpp + InstanceConfig.cpp +
# InstanceConfigManager.cpp + ConfigDir.cpp directly (same pattern as the other
# core tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (PanelStateManager.cpp includes logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL). QTEST_APPLESS_MAIN
# — QSaveFile / QFile / QDir are synchronous, no event loop needed.
qt_add_executable(PanelStateManagerTest PanelStateManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelStateManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(PanelStateManagerTest PRIVATE
    Qt6::Core Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(PanelStateManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PanelStateManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(PanelStateManagerTest)

add_test(NAME PanelStateManagerTest COMMAND PanelStateManagerTest)

# RobustnessTest (T22): JSON robustness + atomic-write guarantee. The Wave 4
# capstone test — locks the "degrade gracefully" contract: every config entry
# point (PanelStateManager::load, InstanceConfigManager::load/loadAll,
# instanceConfigFromJson, panelConfigFromJson) must NEVER throw on
# corrupt/truncated/fuzzed input. Three test groups: truncated panel.json →
# defaults, stale .tmp (simulated crash) → real file unchanged, and a 1000-
# iteration fuzz harness (random-byte files + random QJsonObjects) with zero
# exceptions tolerated.
#
# The test recompiles the FULL core config stack: InstanceConfigManager +
# PanelStateManager + PanelConfig + InstanceConfig + ConfigDir (same pattern as
# PanelStateManagerTest — no shared lib yet). Links Qt6::Core + Qt6::Test +
# spdlog (the config managers include logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL). QTEST_APPLESS_MAIN
# — QFile / QDir / QJsonObject are synchronous, no event loop needed.
qt_add_executable(RobustnessTest RobustnessTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelStateManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(RobustnessTest PRIVATE
    Qt6::Core Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(RobustnessTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(RobustnessTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(RobustnessTest)

add_test(NAME RobustnessTest COMMAND RobustnessTest)

# NetworkWiringTest (Phase 5, todo 1): verifies the main.cpp network-stack
# wiring — constructs WsServer + PendingRequests on the stack (mirrors main.cpp),
# asserts listen(0) + serverPort()>0, then pushes a synthetic Envelope through a
# real QWebSocket client and asserts messageReceived fires (QSignalSpy). This is
# the integration test for the Phase-0-4 gap closure: the network stack is now
# wired into the real app entry point.
# Recompiles WsServer.cpp + PendingRequests.cpp + Envelope.cpp directly (same
# pattern as WsServerTest — no shared lib yet). Links Qt6::WebSockets +
# Qt6::Network (server + in-test QWebSocket client) + spdlog (LOG_* in WsServer).
# QTEST_MAIN — WebSocket I/O needs the event loop that QSignalSpy::wait pumps.
qt_add_executable(NetworkWiringTest NetworkWiringTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/PendingRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(NetworkWiringTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(NetworkWiringTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(NetworkWiringTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(NetworkWiringTest)

add_test(NAME NetworkWiringTest COMMAND NetworkWiringTest)

# InstanceSessionTest (Phase 5, todo 2): the per-instance orchestrator. Two
# tiers — unit (no renderer: bogus-path start() → startFailed+error; setters
# persist+emit; stop() clean) and integration [REQUIRES_RENDERER] (real renderer
# lifecycle: start→ready→running→stop→stopped; stop() suppresses the teardown
# crash signal via manuallyStopping). Recompiles InstanceSession.cpp + its full
# transitive dep chain (ProcessManager, PathResolve, WsServer, Protocol, Envelope,
# MessageDispatcher, EventRegistry, InstanceConfig) directly (same pattern as the
# other tests — no shared lib yet). Links Qt6::WebSockets + Qt6::Network (WsServer
# + integration tier) + Qt6::Test + spdlog. QTEST_MAIN — QSignalSpy::wait /
# QTest::qWait pump the event loop that QProcess + WS I/O require.
qt_add_executable(InstanceSessionTest InstanceSessionTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSession.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionSetters.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionHandlers.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionMonitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ProcessManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PathResolve.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/HitAreaCacheManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RestartController.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StartupSalvo.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelInfoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelScanner.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InteractionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MountedBehaviorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MetaMkoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/system/ResourceStatsCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MonitorDataModel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/PendingRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/network/MessageDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/network/EventRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(InstanceSessionTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(InstanceSessionTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InstanceSessionTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    BIN_OUTPUT_DIR="${CMAKE_SOURCE_DIR}/../build/bin")

pet_deploy_qt_test(InstanceSessionTest)

add_test(NAME InstanceSessionTest COMMAND InstanceSessionTest)
set_tests_properties(InstanceSessionTest PROPERTIES LABELS "REQUIRES_RENDERER")

# ModelInfoParserTest (Phase 5, todo 4): pure static .model3.json parser —
# extracts motionGroups (FileReferences.Motions.<group> array lengths),
# expressions (FileReferences.Expressions[].Name), and hitAreas
# (HitAreas[].Name). Missing file / corrupt JSON → std::nullopt (never
# throws). This is the controller-side fallback for the renderer's
# model_loaded event which returns motions/expressions as EMPTY arrays
# (interface.md §B). Consumed by todo 5 (InstanceSession model_loaded →
# set_hit_areas) and todo 6 (InstanceDetailPage UI).
#
# Recompiles ModelInfoParser.cpp directly (same pattern as the other core
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (ModelInfoParser.cpp includes logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL).
# QTEST_APPLESS_MAIN — QFile/QJsonDocument are synchronous, no event loop.
#
# CUBISM_RESOURCES_DIR is baked at compile time so the test finds the real
# Hiyori fixture regardless of CWD (same pattern as PROTOCOL_FIXTURES_DIR /
# BIN_OUTPUT_DIR). The fixture's REAL content (verified at port time):
# Motions.Idle=9, Motions.TapBody=1, no Expressions field, HitAreas=[Body].
qt_add_executable(ModelInfoParserTest ModelInfoParserTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelInfoParser.cpp)
target_link_libraries(ModelInfoParserTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(ModelInfoParserTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ModelInfoParserTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG
    CUBISM_RESOURCES_DIR="${CMAKE_SOURCE_DIR}/../third_party/CubismSdkForNative/Samples/Resources")

pet_deploy_qt_test(ModelInfoParserTest)

add_test(NAME ModelInfoParserTest COMMAND ModelInfoParserTest)

# ModelScannerTest (Phase 5, todo 4): pure static model-directory scanner —
# lists <rendererDir>/Resources/Models/*/ subdirs containing
# <name>.model3.json, sorted case-insensitively. Missing dir / empty input
# → empty QStringList (never throws).
#
# Recompiles ModelScanner.cpp directly (same pattern). Links Qt6::Core +
# Qt6::Test + spdlog (ModelScanner.cpp includes logging/Logging.hpp which
# references the SPDLOG_* macros; spdlog is statically linked).
# QTEST_APPLESS_MAIN — QDir/QFile are synchronous, no event loop.
#
# Test strategy: uses QTemporaryDir to create the EXACT shipped layout
# (<tmp>/Resources/Models/<Name>/<Name>.model3.json). The real Cubism
# Samples dir has a different shape (<root>/Resources/<Model>/ — NO "Models"
# segment), so it cannot be used directly — see the test file's header
# comment for the full rationale.
qt_add_executable(ModelScannerTest ModelScannerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelScanner.cpp)
target_link_libraries(ModelScannerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(ModelScannerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ModelScannerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(ModelScannerTest)

add_test(NAME ModelScannerTest COMMAND ModelScannerTest)

# InstanceManagerTest (Phase 5, todo 3): the sidebar QAbstractListModel. Four
# slots — create+persist (instanceIds in creation order through a panel.json
# round-trip), delete (file gone + panel.json updated + safe no-op on unknown
# uuid + requestDelete→deleteConfirmed), corrupt-degradation (skip a bad
# instance file with WARN, load only the valid one, no crash), route (demux a
# response to the session whose instanceId matches; unknown instanceId → no-op).
#
# Recompiles InstanceManager + InstanceSession + its full transitive dep chain
# (ProcessManager, PathResolve, InstanceConfig/Manager, PanelConfig,
# PanelStateManager for the savePanel wiring in the test, StartupSalvo,
# ModelInfoParser, WsServer, Protocol, PendingRequests, MessageDispatcher,
# EventRegistry, Envelope) directly (same pattern as InstanceSessionTest — no
# shared lib yet). Links Qt6::WebSockets + Qt6::Network (WsServer) + Qt6::Test
# + spdlog. QTEST_MAIN — route() touches InstanceSession/WsServer and QSignalSpy
# over PendingRequests::resolved is safest with a real application. No renderer
# is launched (createInstance never calls start()), so NO REQUIRES_RENDERER label.
qt_add_executable(InstanceManagerTest InstanceManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSession.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionSetters.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionHandlers.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceSessionMonitor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ProcessManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PathResolve.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/HitAreaCacheManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RestartController.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/PanelStateManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/StartupSalvo.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelInfoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ModelScanner.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Scheduler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InteractionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MountedBehaviorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MetaMkoParser.cpp
    ${CMAKE_SOURCE_DIR}/src/system/ResourceStatsCollector.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MonitorDataModel.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/PendingRequests.cpp
    ${CMAKE_SOURCE_DIR}/src/network/MessageDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/network/EventRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(InstanceManagerTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(InstanceManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InstanceManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(InstanceManagerTest)

add_test(NAME InstanceManagerTest COMMAND InstanceManagerTest)

# SchedulerTest (Phase 5, todo 8): QTimer-based idle motion scheduler. Eight
# slots covering the blueprint §8.3 contract — cadence (≥3 triggers in 200ms at
# 50ms interval), pause (flag-only, 0 triggers while paused), resume (triggers
# resume at cadence), triggerNow (immediate fire + cadence reset, fires even
# when paused), updateInterval (new interval takes effect immediately),
# shutdown (no further triggers), emptyIdleMotions (no trigger, no crash),
# exceptionSurvives (throwing callback swallowed + logged, scheduler survives).
#
# The test recompiles Scheduler.cpp directly (same pattern as the other core
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (Scheduler.cpp
# includes logging/Logging.hpp which references the SPDLOG_* macros; spdlog is
# statically linked, no extra DLL). QTEST_MAIN (NOT APPLESS) — QTimer requires
# the QCoreApplication event loop that QTest::qWait pumps for every cadence
# assertion. The Scheduler's QTimer is parented to the Scheduler (stack-local
# per slot) so no cross-slot leakage; each slot calls shutdown() before
# assertions to stop the timer.
qt_add_executable(SchedulerTest SchedulerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Scheduler.cpp)
target_link_libraries(SchedulerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(SchedulerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(SchedulerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(SchedulerTest)

add_test(NAME SchedulerTest COMMAND SchedulerTest)

# InteractionHandlerTest (Phase 5, todo 9): hit→play_motion translator. Six
# slots locking the 3-tier case-folding lookup (interface.md §F): default
# head→TapHead, default body→TapBody, case-fold capital ("head"→"Head" match
# via tier 2), exact-match-wins-over-default precedence, unknown-area no-op,
# empty/missing-payload no-op. Harness is a capturing messageSender
# (QStringList) so assertions are on the serialized JSON's parsed structure.
#
# The test recompiles InteractionHandler.cpp + Envelope.cpp directly (same
# pattern as the other core tests — no shared lib yet). Links Qt6::Core +
# Qt6::Test + spdlog (InteractionHandler.cpp includes logging/Logging.hpp
# which references the SPDLOG_* macros; spdlog is statically linked, no extra
# DLL). QTEST_APPLESS_MAIN — pure logic (QJsonObject manipulation + the
# handler's case-folding lookup), no event loop needed.
qt_add_executable(InteractionHandlerTest InteractionHandlerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InteractionHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp)
target_link_libraries(InteractionHandlerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(InteractionHandlerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InteractionHandlerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(InteractionHandlerTest)

add_test(NAME InteractionHandlerTest COMMAND InteractionHandlerTest)

# WsServerMultiTest (Phase 5, todo 11): multi-instance routing. Five slots lock
# the new multi-connection contract — two concurrent instances route with the
# correct instanceId, sendText(instanceId, ...) reaches only the right socket,
# disconnect on one instance leaves the others untouched, a wrong-token probe
# for an existing instance_id does NOT evict the good connection, and a
# reconnect with the correct token closes the old socket (1000) and replaces it.
#
# Recompiles WsServer.cpp + Envelope.cpp directly (same pattern as WsServerTest
# / HandshakeTest — no shared lib yet). Links Qt6::WebSockets + Qt6::Network
# (server + in-test QWebSocket clients) + spdlog (LOG_* in WsServer).
# QTEST_MAIN (NOT APPLESS) — WsServer + QWebSocket need the QCoreApplication
# event loop that QSignalSpy::wait pumps.
qt_add_executable(WsServerMultiTest WsServerMultiTest.cpp
    ${CMAKE_SOURCE_DIR}/src/network/WsServer.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(WsServerMultiTest PRIVATE
    Qt6::Core Qt6::Network Qt6::WebSockets Qt6::Test spdlog::spdlog)
target_include_directories(WsServerMultiTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(WsServerMultiTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(WsServerMultiTest)

add_test(NAME WsServerMultiTest COMMAND WsServerMultiTest)

# AutoLaunchManager (Phase 5 Wave 7 todo 14): OS auto-launch toggle — Windows
# `reg.exe` add/delete/query on HKCU\...\Run value 'DesktopPet' via QProcess,
# Linux atomic write/delete of ~/.config/autostart/desktop-pet.desktop, macOS
# no-op log. Testability via injected
# suppliers (app-path, autostart-dir, reg-executor, platform) — every Windows
# case captures the (program, args) reg.exe invocation via a fake executor and
# returns a canned exit code; ZERO real registry mutation. Linux cases use
# QTemporaryDir as the autostart dir. 8 slots cover Windows (enable/isEnabled/
# disable/reg-fails) + Linux (enable/disable/isEnabled) + unsupported-platform.
#
# Recompiles AutoLaunchManager.cpp directly (same pattern as the other tests —
# no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog (AutoLaunchManager.cpp
# includes logging/Logging.hpp which references the SPDLOG_* macros; spdlog is
# statically linked, no extra DLL). QTEST_APPLESS_MAIN — QSaveFile / QFile /
# QDir / QString are all synchronous; the production default regExecutor IS
# real QProcess, but tests inject fakes that never spawn anything.
qt_add_executable(AutoLaunchManagerTest AutoLaunchManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/system/AutoLaunchManager.cpp)
target_link_libraries(AutoLaunchManagerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(AutoLaunchManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(AutoLaunchManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(AutoLaunchManagerTest)

add_test(NAME AutoLaunchManagerTest COMMAND AutoLaunchManagerTest)

# RestartControllerTest (Phase 5 Wave 7, todo 12): crash-recovery backoff.
# Three slots lock the §8.2 contract — backoff ladder (5 retries fire at
# increasing delays via QSignalSpy::wait, attempts→5, no gaveUp), gaveUp
# (the 6th scheduleRestart fires gaveUp synchronously, no 6th retry arms),
# resetOnConnect (resetting after 3 attempts makes the next scheduleRestart
# use BACKOFF[0] again, verified by measuring the actual elapsed delay).
#
# Production BACKOFF_MS {2,4,8,16,30}s would make the full ladder take ~60s,
# so every slot installs a fast test ladder via setBackoffForTest
# ({10,20,30,40,50}ms) — the entire backoff sequence completes in ~150ms.
#
# Recompiles RestartController.cpp directly (same pattern as the other core
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (RestartController.cpp includes logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL).
# QTEST_MAIN (NOT APPLESS) — QTimer::singleShot requires the QCoreApplication
# event loop that QSignalSpy::wait pumps; QTEST_APPLESS_MAIN would silently
# skip every retry assertion.
qt_add_executable(RestartControllerTest RestartControllerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/RestartController.cpp)
target_link_libraries(RestartControllerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(RestartControllerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(RestartControllerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(RestartControllerTest)

add_test(NAME RestartControllerTest COMMAND RestartControllerTest)

# TrayManagerTest (Phase 5 Wave 7, todo 13): QSystemTrayIcon wrapper.
# Five slots lock the signal-routing contract:
#   - testQuitOnLastWindowClosedDisabled: ctor side-effect (the blueprint
#     §4.1.6 "禁用框架的关闭即退出默认行为" requirement) — verified true
#     even when no system tray is available (CI/GNOME).
#   - testMenuActivators: the 3 Q_INVOKABLE menu activators each emit their
#     signal exactly once (QSignalSpy). Covers the QML Menu items uniformly.
#   - testActivatedDoubleClickEmitsVisibilityToggled: onActivated(DoubleClick)
#     → visibilityToggled. QSKIPs gracefully when isAvailable()==false (no
#     m_tray to route through).
#   - testActivatedContextEmitsRequestContextMenu: onActivated(Context) →
#     requestContextMenu. Same QSKIP condition.
#   - testActivatedTriggerIgnored: Trigger/MiddleClick/Unknown emit NOTHING
#     (parity with the Java reference).
#
# Recompiles TrayManager.cpp directly (same pattern as the other system/
# tests — no shared lib yet). Links Qt6::Gui (QSystemTrayIcon +
# QGuiApplication::setQuitOnLastWindowClosed) + Qt6::Test + spdlog
# (TrayManager.cpp includes logging/Logging.hpp which references the
# SPDLOG_* macros; spdlog is statically linked, no extra DLL).
#
# CUSTOM main() (NOT QTEST_MAIN): the test instantiates QGuiApplication
# explicitly because QSystemTrayIcon + setQuitOnLastWindowClosed are QtGui
# APIs that require it. QTEST_MAIN would create only QCoreApplication.
#
# Qt6::Widgets linked for the same reason as the main exe (T13 finding):
# QSystemTrayIcon's header lives in QtWidgets on Qt 6.10 despite the docs.
qt_add_executable(TrayManagerTest TrayManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/system/TrayManager.cpp)
target_link_libraries(TrayManagerTest PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test spdlog::spdlog)
target_include_directories(TrayManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(TrayManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(TrayManagerTest)

add_test(NAME TrayManagerTest COMMAND TrayManagerTest)

# ResourceStatsCollectorTest (Phase 5 Wave 8 todo 17): self-resource
# collection via platform APIs (Win32 GetProcessTimes+GetProcessMemoryInfo;
# Linux /proc/self/stat+/proc/self/status parsed from scratch — M5 fix).
# Five slots lock the §4.8.1 contract:
#   - testCollectReturnsValidTimestamp: timestampMs > 0 on the first sample.
#   - testFirstSampleCpuIsZero: cpuPercent == 0.0 with no baseline.
#   - testCpuPercentAfterTwoSamples: 100ms gap → second cpuPercent finite,
#     [0, 100] (all-cores-saturated scale).
#   - testRssBytesPositive: rssBytes > 0 for a real running process. On
#     Linux this doubles as the M5 guard — proves /proc parsing works
#     rather than the renderer's stub-zero path.
#   - testFailureNeverThrows: inject NULL handle (Win) / non-existent /proc
#     path (Linux) → fields zeroed, timestampMs set, no throw escapes.
#
# Recompiles ResourceStatsCollector.cpp directly (same pattern as the other
# system/ tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (ResourceStatsCollector.cpp includes logging/Logging.hpp which references
# the SPDLOG_* macros; spdlog is statically linked, no extra DLL). On
# Windows, `psapi` is also linked (GetProcessMemoryInfo lives in Psapi.lib).
# QTEST_MAIN (NOT APPLESS) — QTest::qWait (used to separate the two CPU%
# samples by a real wall-clock gap) requires the QCoreApplication event loop.
qt_add_executable(ResourceStatsCollectorTest ResourceStatsCollectorTest.cpp
    ${CMAKE_SOURCE_DIR}/src/system/ResourceStatsCollector.cpp)
target_link_libraries(ResourceStatsCollectorTest PRIVATE
    Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(ResourceStatsCollectorTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ResourceStatsCollectorTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
if(WIN32)
    target_link_libraries(ResourceStatsCollectorTest PRIVATE psapi)
endif()

pet_deploy_qt_test(ResourceStatsCollectorTest)

add_test(NAME ResourceStatsCollectorTest COMMAND ResourceStatsCollectorTest)

# VoicePackScannerTest (Phase 5 Wave 8 todo 19): pure static voice-pack
# directory scanner — lists <rendererDir>/Resources/VoicePacks/*/ subdirs
# containing meta.mko, sorted case-insensitively, hidden dirs skipped. Six
# slots: single pack, sorted multiple, filter-no-meta.mko, skip-hidden-dir,
# nonexistent dir → empty, empty dir → empty. Missing dir / empty input →
# empty QStringList (never throws).
#
# Recompiles VoicePackScanner.cpp directly (same pattern as ModelScannerTest).
# Links Qt6::Core + Qt6::Test + spdlog (VoicePackScanner.cpp includes
# logging/Logging.hpp which references the SPDLOG_* macros; spdlog is
# statically linked). QTEST_APPLESS_MAIN — QDir/QFile are synchronous.
#
# Test strategy: QTemporaryDir creates the EXACT shipped layout
# (<tmp>/Resources/VoicePacks/<Pack>/meta.mko). No real voice-pack fixtures
# ship under Resources/VoicePacks in the current build, so synthesized
# fixtures are the only option — same strategy as ModelScannerTest.
qt_add_executable(VoicePackScannerTest VoicePackScannerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/VoicePackScanner.cpp)
target_link_libraries(VoicePackScannerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(VoicePackScannerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(VoicePackScannerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(VoicePackScannerTest)

add_test(NAME VoicePackScannerTest COMMAND VoicePackScannerTest)

# MetaMkoParserTest (Phase 5 Wave 8 todo 19): meta.mko protobuf Bundle parser.
# Six slots lock the §5.5 contract: valid Bundle → populated VoicePackInfo
# (meta + groups + modules), actions bucketed by group code, optional lipSync
# presence (field 5 present → populated, absent → empty), missing file →
# std::nullopt, corrupt bytes → std::nullopt, empty dir → std::nullopt.
#
# Implementation note: the Qt port uses a hand-rolled protobuf wire-format
# reader (see MetaMkoParser.cpp file-level comment), NOT generated protobuf.
# The test therefore synthesizes a valid Bundle via a minimal protobuf
# SERIALIZER in its anonymous namespace (encodeVarint/encodeTag/
# encodeStringField/encodeMessageField) — the serialized bytes are
# byte-for-byte identical to what protoc would emit, so the round-trip
# faithfully proves the reader handles real protobuf wire format.
#
# Recompiles MetaMkoParser.cpp directly (same pattern as ModelInfoParserTest).
# Links Qt6::Core + Qt6::Test + spdlog (MetaMkoParser.cpp includes
# logging/Logging.hpp which references the SPDLOG_* macros; spdlog is
# statically linked). QTEST_APPLESS_MAIN — QFile/QByteArray are synchronous.
qt_add_executable(MetaMkoParserTest MetaMkoParserTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MetaMkoParser.cpp)
target_link_libraries(MetaMkoParserTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(MetaMkoParserTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MetaMkoParserTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(MetaMkoParserTest)

add_test(NAME MetaMkoParserTest COMMAND MetaMkoParserTest)

# MountedBehaviorEngineTest (Phase 5 Wave 8 todo 20): voice-pack behavior engine
# that routes hit events to play_motion_ext when a voice pack is mounted. Six
# slots lock the contract:
#   - testBuildBehaviorCommand: synthetic VoicePackInfo with one head-tap action
#     (motion + audio + lipSync) → returns a play_motion_ext Envelope with
#     absolute paths, group priority, fade in/out seconds, and the action's
#     doc text as BehaviorResult::dialogueText.
#   - testHasGroupForAreaFalse: unknown area → false; case-insensitive lookup
#     (HEAD == head).
#   - testNullVoicePack: default-constructed engine → hasGroupForArea always
#     false, buildBehaviorCommand returns nullopt (the "engine disabled"
#     sentinel that InstanceSession's hit handler relies on for the
#     InteractionHandler fallback).
#   - testEstimateOggDuration: synthesized OGG-shaped blob (\x01vorbis header +
#     sample_rate at +12, final OggS page + granule at +6) → exact duration =
#     granule / sample_rate * 1000. Non-OGG / missing / too-small → fallback
#     kFallbackDurationMs (5000), NOT 0 or throw.
#   - testAudioOnly: action with empty motion + non-empty audio →
#     buildAudioOnlyCommand returns a play_audio Envelope.
#   - testIsIdleMotionPath: idle-group action's resolved motion path is
#     recognized; non-matching path returns false.
#
# Recompiles MountedBehaviorEngine.cpp + Protocol.cpp + Envelope.cpp directly
# (MountedBehaviorEngine calls Protocol::buildPlayMotionExt + buildPlayAudio;
# Protocol calls Envelope's createCommand). Links Qt6::Core + Qt6::Test +
# spdlog (LOG_* macros). QTEST_APPLESS_MAIN — QFile/QByteArray/QDir are
# synchronous, no event loop needed.
qt_add_executable(MountedBehaviorEngineTest MountedBehaviorEngineTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MountedBehaviorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(MountedBehaviorEngineTest PRIVATE
    Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(MountedBehaviorEngineTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MountedBehaviorEngineTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(MountedBehaviorEngineTest)

add_test(NAME MountedBehaviorEngineTest COMMAND MountedBehaviorEngineTest)

# OggDurationHeuristicTest (Phase 5 Wave 8 todo 20): extracted from MountedBehaviorEngineTest
# when that file exceeded the 250 pure-LOC ceiling. The estimateOggDurationMs
# static method is a self-contained binary-format parser (distinct from the
# engine API cluster), so it gets its own test file. Three slots lock the
# heuristic + fallback contract:
#   - testExactDurationFromSynthesizedOgg: 1s/2s blobs → granule/sample_rate*1000.
#   - testFallbackOnNonOggContent: non-OGG bytes → kFallbackDurationMs.
#   - testFallbackOnMissingOrTinyFile: missing / <85 bytes → fallback.
#
# Recompiles MountedBehaviorEngine.cpp only (the static method's only dep is
# Qt6::Core QFile/QByteArray + spdlog LOG_*). No Protocol/Envelope needed
# (those are only pulled in by the engine's instance methods). Links Qt6::Core
# + Qt6::Test + spdlog. QTEST_APPLESS_MAIN — QFile/QByteArray are synchronous.
qt_add_executable(OggDurationHeuristicTest OggDurationHeuristicTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/MountedBehaviorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/network/Envelope.cpp)
target_link_libraries(OggDurationHeuristicTest PRIVATE
    Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(OggDurationHeuristicTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(OggDurationHeuristicTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(OggDurationHeuristicTest)

add_test(NAME OggDurationHeuristicTest COMMAND OggDurationHeuristicTest)

# MonitorDataModelTest (Phase 5 Wave 8 todo 18): resource-monitor ring buffer
# + merge semantics + stale predicate. Six slots lock the data-model
# contract:
#   - testRingBufferCap: 65 controller merges → history.size()==60 (FIFO drop).
#   - testIsStalePredicate: static isStale() boundary cases (never / just-fresh /
#     1ms-over / negative threshold).
#   - testMergeRenderer: Linux-stub stats_state payload (gpu/vram JSON null)
#     parses cleanly, null fields preserved, Q_INVOKABLE getters return -1.0
#     sentinel, GPU/VRAM chart series skip null samples.
#   - testMergePreservesOtherSide: controller merge then renderer merge leaves
#     BOTH halves populated in the latest snapshot (carry-forward rule).
#   - testClearHistory: clears ring + resets lastRendererUpdateMs to 0, fires
#     historyCleared signal exactly once.
#   - testRendererStatsFromJson: Windows full payload / Linux stub / missing
#     keys / partial payload — fromJson never throws, per-field null tolerance.
#
# Recompiles MonitorDataModel.cpp directly (same pattern as the other ui/
# tests — no shared lib yet). Links Qt6::Core + Qt6::Test + spdlog
# (MonitorDataModel.cpp's transitive include chain references the SPDLOG_*
# macros via ResourceStatsCollector.hpp → logging/Logging.hpp; spdlog is
# statically linked, no extra DLL). ResourceStatsCollector.cpp is NOT
# recompiled here — MonitorDataModel only uses the ControllerStats struct
# definition (header-only), it does not call collect(), so no psapi link.
# QTEST_APPLESS_MAIN — QDateTime + QJsonObject are synchronous, no event loop.
qt_add_executable(MonitorDataModelTest MonitorDataModelTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/MonitorDataModel.cpp)
target_link_libraries(MonitorDataModelTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(MonitorDataModelTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(MonitorDataModelTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(MonitorDataModelTest)

add_test(NAME MonitorDataModelTest COMMAND MonitorDataModelTest)

# DatabaseManagerTest (SQLite config backend): schema creation, kv round-trip,
# instance config round-trip, asset insert + duplicate-sha reject, ref
# attach/detach + inUse, closed-db safe no-op. Recompiles DatabaseManager.cpp
# + InstanceConfig.cpp (serde). Links Qt6::Sql + Qt6::Test + spdlog.
# QTEST_APPLESS_MAIN - QSqlDatabase is synchronous.
qt_add_executable(DatabaseManagerTest DatabaseManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp)
target_link_libraries(DatabaseManagerTest PRIVATE
    Qt6::Core Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(DatabaseManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(DatabaseManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(DatabaseManagerTest)

add_test(NAME DatabaseManagerTest COMMAND DatabaseManagerTest)

# AssetManagerTest (image library + logo/instance-icon refs): import pipeline
# (ok / duplicate / invalid), delete blocked when referenced, setLogo /
# resetLogo, setInstanceIcon + instanceIconUrl. Recompiles AssetManager.cpp +
# DatabaseManager.cpp. Links Qt6::Sql + Qt6::Gui (QImage) + Qt6::Test + spdlog.
# QTEST_APPLESS_MAIN - QImage + QFile + QSqlDatabase are synchronous.
qt_add_executable(AssetManagerTest AssetManagerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/core/AssetManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/DatabaseManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(AssetManagerTest PRIVATE
    Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test spdlog::spdlog)
target_include_directories(AssetManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(AssetManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(AssetManagerTest)

add_test(NAME AssetManagerTest COMMAND AssetManagerTest)

# ── Bubble-stream (气泡信息流) tests ────────────────────────────────────────
# QTest binaries locking the notification-stream contract: model
# (prepend/cap/expiry/dismiss/roles) and the QML bridge controller. Same
# recompile-the-.cpp pattern + windeployqt post-build step as every test
# above (no shared lib yet; ctest runs with Qt's bin NOT on PATH).

qt_add_executable(NotificationStreamModelTest NotificationStreamModelTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/NotificationStreamModel.cpp)
target_link_libraries(NotificationStreamModelTest PRIVATE
    Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(NotificationStreamModelTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(NotificationStreamModelTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(NotificationStreamModelTest)

add_test(NAME NotificationStreamModelTest COMMAND NotificationStreamModelTest)

qt_add_executable(NotificationStreamControllerTest NotificationStreamControllerTest.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/NotificationStreamController.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/NotificationStreamModel.cpp)
target_link_libraries(NotificationStreamControllerTest PRIVATE
    Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(NotificationStreamControllerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(NotificationStreamControllerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

pet_deploy_qt_test(NotificationStreamControllerTest)

add_test(NAME NotificationStreamControllerTest COMMAND NotificationStreamControllerTest)
