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

# Executable. Output lands in <root>/build/bin next to the renderer so the
# WebSocket/port + relative resource paths line up with the rest of the app.
set(_bin_output_dir "${PROJECT_SOURCE_DIR}/../build/bin")
qt_add_executable(desktop-pet-controller-qt
    src/main.cpp
    src/network/Envelope.cpp
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
    Qt6::Qml
    Qt6::Quick
)

# QML module. Main.qml is auto-registered as type "Main" under URI DesktopPet,
# loadable via QQmlApplicationEngine::loadFromModule("DesktopPet", "Main").
qt_add_qml_module(desktop-pet-controller-qt
    URI DesktopPet
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

# enable_testing() is present so QTest targets under tests/ register via
# add_subdirectory(tests) without touching this file again.
enable_testing()
add_subdirectory(tests)
