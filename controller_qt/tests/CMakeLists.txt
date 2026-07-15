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
