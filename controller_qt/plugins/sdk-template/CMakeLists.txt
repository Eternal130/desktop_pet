# sdk-template plugin (P4, §B.6). Included by the host's static-plugin
# pipeline (controller_qt/CMakeLists.txt) via add_subdirectory — stage 1
# plugins build INTO the panel executable (§A.3), programming only against
# pet_panel_api.
#
# Stage 2 (host channel landed 2026-10-16, SDK v1.12): copy this directory →
# configure the copy with PET_BUILD_SDK_TEMPLATE_DYNAMIC=ON → build → drop
# the resulting .so/.dll into <exeDir>/plugins/ or <dataDir>/plugins/
# (Linux ~/.local/share/desktop-pet/plugins/, Windows
# %LOCALAPPDATA%\desktop-pet\plugins\) → restart the panel (restart-to-
# apply, §A.4 — no hot load). The bundled CMakePresets.json remains the
# standalone third-party entry (inherit the Qt MinGW toolchain, §C.2).
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

# ── Dynamic (drop-in) variant — stage 2 host channel (2026-10-16) ───────────
# Copy this directory → set PET_BUILD_SDK_TEMPLATE_DYNAMIC=ON → build →
# drop the resulting .so/.dll into <exeDir>/plugins/ or <dataDir>/plugins/
# → restart the panel. The host's DynamicPluginSource discovers the file at
# boot and gates it on iid + api_version + id uniqueness + the abi block
# (fail-closed) before any library code runs.
#
# Deliberately NOT exported to the host exe (no PARENT_SCOPE here): the
# static pipeline above keeps building the in-tree copy; the MODULE below
# is the drop-in artifact only — the two never coexist in one process.
#
# MANIFEST GENERATION (P2-2, 2026-10-16 review round): the dynamic variant
# configures plugin.json.in into <binary-dir>/plugin_dyn.json with the
# §A.2 abi block injected from THIS build's toolchain — the template
# produces a load-ready drop-in out of the box (a same-repo build passes
# its own host's abi gate by construction). SamplePluginFactory.hpp's
# Q_PLUGIN_METADATA(FILE "plugin_dyn.json") resolves through the target's
# include path (moc checks the header's own directory FIRST — plugin_dyn.json
# deliberately does NOT exist there, so the binary-dir copy is the only
# candidate). The checked-in plugin.json stays abi-less: it feeds the
# STATIC pipeline, which is abi-exempt by §A.2.
#
# The abi expression is recomputed HERE because add_subdirectory opens a
# new variable scope — the parent's _abi_json does not propagate down.
# LOCKSTEP: keep field order + spelling identical to the single
# computation at the top of controller_qt/CMakeLists.txt's plugin
# pipeline section (P2-1).
option(PET_BUILD_SDK_TEMPLATE_DYNAMIC "Build the dynamic (drop-in .so/.dll) variant" OFF)
if(PET_BUILD_SDK_TEMPLATE_DYNAMIC)
    if(MINGW)
        set(_sdk_abi_qt_build "mingw_64")
    elseif(MSVC)
        set(_sdk_abi_qt_build "msvc_64")
    else()
        set(_sdk_abi_qt_build "linux_gcc_64")
    endif()
    set(_sdk_abi_json "{\"compiler\":\"${CMAKE_CXX_COMPILER_ID}\",\"compiler_version\":\"${CMAKE_CXX_COMPILER_VERSION}\",\"qt_version\":\"${Qt6_VERSION}\",\"qt_build\":\"${_sdk_abi_qt_build}\",\"build_type\":\"${CMAKE_BUILD_TYPE}\"}")
    # The .in template references @ABI_JSON@ — bind the computed block to
    # that EXACT name (a name mismatch substitutes an EMPTY string, which
    # makes plugin_dyn.json unparseable; moc then WARNS "Declaration will
    # be ignored" and silently drops Q_PLUGIN_METADATA — the build stays
    # green but the artifact ships without .qtmetadata. The guard below
    # makes that failure mode FATAL at configure time instead.)
    set(ABI_JSON "${_sdk_abi_json}")
    if(NOT ABI_JSON)
        message(FATAL_ERROR "sdk-template: ABI_JSON computed empty — "
            "plugin_dyn.json would be invalid JSON.")
    endif()
    configure_file(plugin.json.in "${CMAKE_CURRENT_BINARY_DIR}/plugin_dyn.json" @ONLY)
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/plugin_dyn.json" _dyn_manifest_check)
    string(FIND "${_dyn_manifest_check}" "\"abi\": {" _dyn_abi_pos)
    if(_dyn_abi_pos LESS 0)
        message(FATAL_ERROR "sdk-template: plugin_dyn.json has no abi object "
            "after substitution — refusing to build a silently-broken "
            "drop-in artifact (moc would ignore Q_PLUGIN_METADATA).")
    endif()

    qt_add_library(pet_plugin_sdk_template_dyn MODULE
        SamplePlugin.cpp
        SamplePluginFactory.hpp # listed so AUTOMOC processes Q_OBJECT
        SamplePluginFactory.cpp
    )
    # moc resolves Q_PLUGIN_METADATA(FILE "plugin_dyn.json") through this
    # include path (the file lives only in the binary dir — see the
    # manifest-generation note above).
    target_include_directories(pet_plugin_sdk_template_dyn PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}")
    # Own copy of the page's RESOURCES (kept out of the static variant's qml
    # module). RESOURCE STRATEGY (deliberate deviation from "same
    # qt_add_qml_module as the static variant"): qt_add_qml_module on a
    # MODULE target adds a GENERATED QML-module plugin class carrying its own
    # Q_PLUGIN_METADATA — a SECOND plugin root inside the same binary,
    # competing with SamplePluginFactory for QPluginLoader's root instance.
    # The page only needs to be reachable at the qrc URL the manifest names
    # (its imports — QtQuick + the host's own "DesktopPet" module — resolve
    # in the HOST engine at load time), so a plain resource with the QTP0001
    # layout (:/qt/qml/<URI>/...) is sufficient and has no plugin root at
    # all. The FILES entry must stay RELATIVE (Qt resources reject absolute
    # source paths); both variants embedding the same SamplePage.qml is fine
    # — resources are per-target and the two artifacts never coexist in one
    # process.
    qt_add_resources(pet_plugin_sdk_template_dyn "sampleplugin_qml"
        PREFIX "/qt/qml/SamplePlugin"
        FILES SamplePage.qml
    )
    target_link_libraries(pet_plugin_sdk_template_dyn PRIVATE
        pet_panel_api
        Qt6::Core
        Qt6::Qml
        Qt6::Quick
    )
    message(STATUS "sdk-template dynamic artifact: "
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_MODULE_PREFIX}pet_plugin_sdk_template_dyn"
        "${CMAKE_SHARED_MODULE_SUFFIX} (abi-injected, drop-in ready)")
endif()
