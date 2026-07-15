# Protocol fixture tests (QTest).
#
# T3 adds a single test binary, ProtocolFixturesTest, that loads the JSON
# fixtures under protocol/ and asserts the structural invariants every later
# task (T4 Envelope ser/deser, T7-T11 router/handshake/factory/event) relies on.
#
# The fixture directory is baked in at compile time via PROTOCOL_FIXTURES_DIR so
# the test finds the JSON files regardless of where the binary is launched from
# (avoids QFINDTESTDATA deployment complexity — see learnings.md T3 notes).

# The test exe links Qt6::Test (registered as a REQUIRED component in the parent
# CMakeLists.txt) plus Qt6::Core. QtGui is not needed: ProtocolFixturesTest is a
# pure JSON-parsing test with no GUI, so QTEST_APPLESS_MAIN is used.
qt_add_executable(ProtocolFixturesTest ProtocolFixturesTest.cpp)
target_link_libraries(ProtocolFixturesTest PRIVATE Qt6::Core Qt6::Test)

# Embed the absolute source path of the protocol/ fixture directory so the test
# binary can locate the JSON files from any working directory (incl. ctest).
target_compile_definitions(ProtocolFixturesTest PRIVATE
    PROTOCOL_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/protocol")

# On Windows the test exe needs the Qt DLLs (Qt6Core.dll, Qt6Test.dll) next to
# it to run standalone under ctest — _get_qt_env() does not put Qt's bin on
# PATH. windeployqt mirrors what build.py does for the main app exe.
if(WIN32)
    # Qt6_DIR = <qt>/lib/cmake/Qt6 → <qt>/bin holds windeployqt.exe
    get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(QT_WINDEPLOYQT_EXE
        NAMES windeployqt
        HINTS "${_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET ProtocolFixturesTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:ProtocolFixturesTest>"
            COMMENT "Running windeployqt for ProtocolFixturesTest")
    else()
        message(WARNING "windeployqt not found under ${_qt_bin_dir}; "
                        "ProtocolFixturesTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as ProtocolFixturesTest:
# ctest launches the exe with Qt's bin NOT on PATH, so the DLLs must sit next
# to it). QT_WINDEPLOYQT_EXE is found and cached by the ProtocolFixturesTest
# block above, which configure-time-runs first; fall back to a fresh lookup
# if that ordering ever changes.
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET EnvelopeTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:EnvelopeTest>"
            COMMENT "Running windeployqt for EnvelopeTest")
    else()
        message(WARNING "windeployqt not found; EnvelopeTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH). QT_WINDEPLOYQT_EXE is found+cached
# by the ProtocolFixturesTest block above; fall back to a fresh lookup if the
# ordering ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET LoggingTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:LoggingTest>"
            COMMENT "Running windeployqt for LoggingTest")
    else()
        message(WARNING "windeployqt not found; LoggingTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so the WebSockets/Network DLLs must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the ProtocolFixturesTest
# block above; fall back to a fresh lookup if the ordering ever changes.
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET WsServerTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:WsServerTest>"
            COMMENT "Running windeployqt for WsServerTest")
    else()
        message(WARNING "windeployqt not found; WsServerTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (ctest launches with Qt's bin NOT on
# PATH, so Qt6Core.dll/Qt6Test.dll must sit next to it). QT_WINDEPLOYQT_EXE is
# found+cached by the ProtocolFixturesTest block above; fall back to a fresh
# lookup if ordering ever changes.
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET PoCIntegrationTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:PoCIntegrationTest>"
            COMMENT "Running windeployqt for PoCIntegrationTest")
    else()
        message(WARNING "windeployqt not found; PoCIntegrationTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must sit
# next to it). QT_WINDEPLOYQT_EXE is found+cached by the ProtocolFixturesTest
# block above; fall back to a fresh lookup if ordering ever changes.
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET MessageDispatcherTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:MessageDispatcherTest>"
            COMMENT "Running windeployqt for MessageDispatcherTest")
    else()
        message(WARNING "windeployqt not found; MessageDispatcherTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET ProtocolFactoryTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:ProtocolFactoryTest>"
            COMMENT "Running windeployqt for ProtocolFactoryTest")
    else()
        message(WARNING "windeployqt not found; ProtocolFactoryTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (ctest launches with Qt's bin NOT on
# PATH, so the WebSockets/Network DLLs must sit next to it). QT_WINDEPLOYQT_EXE
# is found+cached by the ProtocolFixturesTest block above; fall back to a fresh
# lookup if ordering ever changes.
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET HandshakeTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:HandshakeTest>"
            COMMENT "Running windeployqt for HandshakeTest")
    else()
        message(WARNING "windeployqt not found; HandshakeTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET PendingRequestsTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:PendingRequestsTest>"
            COMMENT "Running windeployqt for PendingRequestsTest")
    else()
        message(WARNING "windeployqt not found; PendingRequestsTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET EventRegistryTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:EventRegistryTest>"
            COMMENT "Running windeployqt for EventRegistryTest")
    else()
        message(WARNING "windeployqt not found; EventRegistryTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so the WebSockets/Network DLLs must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET ThreadMarshalTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:ThreadMarshalTest>"
            COMMENT "Running windeployqt for ThreadMarshalTest")
    else()
        message(WARNING "windeployqt not found; ThreadMarshalTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET PathResolveTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:PathResolveTest>"
            COMMENT "Running windeployqt for PathResolveTest")
    else()
        message(WARNING "windeployqt not found; PathResolveTest may fail to run under ctest on Windows.")
    endif()
endif()

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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so the Core/Network/WebSockets/Test
# DLLs must sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET ProcessManagerTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:ProcessManagerTest>"
            COMMENT "Running windeployqt for ProcessManagerTest")
    else()
        message(WARNING "windeployqt not found; ProcessManagerTest may fail to run under ctest on Windows.")
    endif()
endif()

add_test(NAME ProcessManagerTest COMMAND ProcessManagerTest)
# REQUIRES_RENDERER label lets ctest filter this out of headless/CI runs where
# no display or renderer binary is present (the integration tier also QSKIPs at
# runtime if the renderer binary is absent; the unit tier always runs).
set_tests_properties(ProcessManagerTest PROPERTIES LABELS "REQUIRES_RENDERER")

# StartupSalvo (T16): the 9-command bootstrap salvo + set_hit_areas. Two tiers:
#   - Unit tier (4 slots, no renderer): capturing-sender harness asserts the 9
#     commands fire in order, that set_scale NEVER appears in the stream, that
#     set_subtitle_style carries all 16 named style fields, and that every
#     salvo action is in the command_all_25.json catalog.
#   - Integration tier (1 slot, REQUIRES_RENDERER): WsServer + ProcessManager +
#     StartupSalvo wired against the real renderer; ready → sendSalvo,
#     model_loaded → sendSetHitAreas, asserts 10 commands captured in order
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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so the Core/Network/WebSockets/Test
# DLLs must sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET StartupSalvoTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:StartupSalvoTest>"
            COMMENT "Running windeployqt for StartupSalvoTest")
    else()
        message(WARNING "windeployqt not found; StartupSalvoTest may fail to run under ctest on Windows.")
    endif()
endif()

add_test(NAME StartupSalvoTest COMMAND StartupSalvoTest)
# REQUIRES_RENDERER label lets ctest filter this out of headless/CI runs where
# no display or renderer binary is present (the integration tier also QSKIPs at
# runtime if the renderer binary is absent; the unit tier always runs).
set_tests_properties(StartupSalvoTest PROPERTIES LABELS "REQUIRES_RENDERER")

# ConfigDir (T17): cross-platform config directory manager — configDir() /
# instancesDir() / logsDir() accessors + ensureDirectories(basePath) tree
# creator. The accessor paths are home-relative and use '/' on every platform
# (byte-for-byte compatible with the JavaFX controller's
# `user.home + "/.config/desktop-pet/"`). ensureDirectories accepts an
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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET ConfigDirTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:ConfigDirTest>"
            COMMENT "Running windeployqt for ConfigDirTest")
    else()
        message(WARNING "windeployqt not found; ConfigDirTest may fail to run under ctest on Windows.")
    endif()
endif()

add_test(NAME ConfigDirTest COMMAND ConfigDirTest)

# InstanceConfig (T18): the 28-field persisted instance config model — snake_case
# JSON serde for JavaFX interop. instanceConfigToJson / instanceConfigFromJson /
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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET InstanceConfigTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:InstanceConfigTest>"
            COMMENT "Running windeployqt for InstanceConfigTest")
    else()
        message(WARNING "windeployqt not found; InstanceConfigTest may fail to run under ctest on Windows.")
    endif()
endif()

add_test(NAME InstanceConfigTest COMMAND InstanceConfigTest)

# PanelConfig (T19): the 12-field persisted panel UI-state config model —
# snake_case JSON serde for JavaFX interop. panelConfigToJson / panelConfigFromJson
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

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must sit
# next to it). QT_WINDEPLOYQT_EXE is found+cached by the ProtocolFixturesTest
# block above; fall back to a fresh lookup if ordering ever changes. spdlog is
# statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET PanelConfigTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:PanelConfigTest>"
            COMMENT "Running windeployqt for PanelConfigTest")
    else()
        message(WARNING "windeployqt not found; PanelConfigTest may fail to run under ctest on Windows.")
    endif()
endif()

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
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(InstanceConfigManagerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(InstanceConfigManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(InstanceConfigManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET InstanceConfigManagerTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:InstanceConfigManagerTest>"
            COMMENT "Running windeployqt for InstanceConfigManagerTest")
    else()
        message(WARNING "windeployqt not found; InstanceConfigManagerTest may fail to run under ctest on Windows.")
    endif()
endif()

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
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(PanelStateManagerTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(PanelStateManagerTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(PanelStateManagerTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET PanelStateManagerTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:PanelStateManagerTest>"
            COMMENT "Running windeployqt for PanelStateManagerTest")
    else()
        message(WARNING "windeployqt not found; PanelStateManagerTest may fail to run under ctest on Windows.")
    endif()
endif()

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
    ${CMAKE_SOURCE_DIR}/src/core/PanelConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/InstanceConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/core/ConfigDir.cpp)
target_link_libraries(RobustnessTest PRIVATE Qt6::Core Qt6::Test spdlog::spdlog)
target_include_directories(RobustnessTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(RobustnessTest PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# windeployqt for the test exe on Windows (same need as the other test exes:
# ctest launches with Qt's bin NOT on PATH, so Qt6Core.dll/Qt6Test.dll must
# sit next to it). QT_WINDEPLOYQT_EXE is found+cached by the
# ProtocolFixturesTest block above; fall back to a fresh lookup if ordering
# ever changes. spdlog is statically linked (no extra DLL needed).
if(WIN32)
    if(NOT QT_WINDEPLOYQT_EXE)
        get_filename_component(_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
        find_program(QT_WINDEPLOYQT_EXE
            NAMES windeployqt
            HINTS "${_qt_bin_dir}"
            NO_DEFAULT_PATH)
    endif()
    if(QT_WINDEPLOYQT_EXE)
        add_custom_command(TARGET RobustnessTest POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXE}"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    --no-quick-import
                    "$<TARGET_FILE:RobustnessTest>"
            COMMENT "Running windeployqt for RobustnessTest")
    else()
        message(WARNING "windeployqt not found; RobustnessTest may fail to run under ctest on Windows.")
    endif()
endif()

add_test(NAME RobustnessTest COMMAND RobustnessTest)
