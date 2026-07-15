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
    Test
    REQUIRED
)

qt_standard_project_setup()

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

# Executable. Output lands in <root>/build/bin next to the renderer so the
# WebSocket/port + relative resource paths line up with the rest of the app.
set(_bin_output_dir "${PROJECT_SOURCE_DIR}/../build/bin")
qt_add_executable(desktop-pet-controller-qt
    src/main.cpp
    src/network/Envelope.cpp
    src/network/WsServer.cpp
    src/network/MessageDispatcher.cpp
    src/logging/Logging.cpp
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
    Qt6::Network
    Qt6::WebSockets
    Qt6::Qml
    Qt6::Quick
    spdlog::spdlog
)

# Force the spdlog *active* macros (SPDLOG_DEBUG/INFO/... which LOG_* wrap) to
# emit at debug level by default so LOG_DEBUG is live in dev builds. Release
# builds can override by appending -DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO.
target_compile_definitions(desktop-pet-controller-qt PRIVATE
    SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)

# QML module. Main.qml is auto-registered as type "Main" under URI DesktopPet,
# loadable via QQmlApplicationEngine::loadFromModule("DesktopPet", "Main").
qt_add_qml_module(desktop-pet-controller-qt
    URI DesktopPet
    VERSION 1.0
    QML_FILES
        qml/Main.qml
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
