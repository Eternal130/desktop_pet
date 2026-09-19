# sdk-template plugin (P4, §B.6). Included by the host's static-plugin
# pipeline (controller_qt/CMakeLists.txt) via add_subdirectory — stage 1
# plugins build INTO the panel executable (§A.3), programming only against
# pet_panel_api.
#
# Stage 2 note: this file + the bundled CMakePresets.json are the third-party
# template (§A.2 — copy the dir, inherit the Qt MinGW toolchain, build a dll
# against the SDK). Until stage 2, the presets are inert to the host build.
cmake_minimum_required(VERSION 3.22)
project(desktop_pet_plugin_sample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.8 COMPONENTS Core Qml Quick REQUIRED)

# The SDK headers (pet_panel_api is INTERFACE: include path + Qt6::Core).
# In-tree the target already exists; standalone builds define it via the
# presets' toolchain + a SDK install (stage 2).
if(NOT TARGET pet_panel_api)
    message(FATAL_ERROR "sdk-template must be built from the host tree "
        "(stage 1) — the plugin pipeline adds this directory via "
        "add_subdirectory with pet_panel_api in scope.")
endif()

# Plugin body — STATIC, linked into the panel exe by the pipeline.
qt_add_library(pet_plugin_sdk_template STATIC
    SamplePlugin.cpp
)

# The plugin's QML module (§B.6 resource layout): page + icons live in the
# qrc of THIS library; the host receives a qrc:/ URL (exported below), never
# a filesystem path.
qt_add_qml_module(pet_plugin_sdk_template
    URI SamplePlugin
    VERSION 1.0
    QML_FILES SamplePage.qml
)

target_link_libraries(pet_plugin_sdk_template PUBLIC
    pet_panel_api
    Qt6::Core
    Qt6::Qml
    Qt6::Quick
)

# ── Parent-scope contract (read by the pipeline loop) ───────────────────────
# QML page URL under the QTP0001 NEW layout (:/qt/qml/<URI>/...).
set(sdk_template_QML_URL "qrc:/qt/qml/SamplePlugin/SamplePage.qml" PARENT_SCOPE)
# The qml module's static plugin registers the qrc — the exe must link it
# (same mechanism as pet_panel_coreplugin, see the P2c note in the host
# CMakeLists).
set(sdk_template_EXTRA_TARGETS pet_plugin_sdk_templateplugin PARENT_SCOPE)
