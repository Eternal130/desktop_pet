# =============================================================================
# System MinGW-w64 toolchain for the renderer (Windows builds only).
#
# Design intent: docs/refactor/plugin-architecture-and-cmake-migration.md
# section C.2 - pin absolute compiler and make-program paths so that PATH
# order can no longer influence the build result. This is the root-cause fix
# that replaces the former PATH-filtering approach (the legacy build script
# removed Git's bundled MinGW from PATH to avoid libwinpthread DLL conflicts),
# and upgrades the "Git's MinGW is poison"
# pitfall from a documentation note to a configure-time hard gate.
#
# Per-machine override: set the RENDERER_MINGW_ROOT environment variable to
# the root of a standalone MinGW-w64 installation (the parent of its bin/
# directory). The default below matches the project's reference machine.
# =============================================================================

if("$ENV{RENDERER_MINGW_ROOT}" STREQUAL "")
    set(RENDERER_MINGW_ROOT "C:/mingw64")
else()
    set(RENDERER_MINGW_ROOT "$ENV{RENDERER_MINGW_ROOT}")
endif()

message(STATUS
    "Renderer MinGW toolchain root: ${RENDERER_MINGW_ROOT} "
    "(override with the RENDERER_MINGW_ROOT environment variable)")

set(RENDERER_MINGW_C_COMPILER   "${RENDERER_MINGW_ROOT}/bin/gcc.exe")
set(RENDERER_MINGW_CXX_COMPILER "${RENDERER_MINGW_ROOT}/bin/g++.exe")
set(RENDERER_MINGW_MAKE_PROGRAM "${RENDERER_MINGW_ROOT}/bin/mingw32-make.exe")

# --- Hard gate 1: reject Git's bundled MinGW ---------------------------------
# Git for Windows ships its own MinGW whose runtime DLLs (libwinpthread)
# conflict with the project MinGW. Any compiler path containing "Git" is
# rejected at configure time, before anything gets built.

foreach(_compiler
        "${RENDERER_MINGW_C_COMPILER}"
        "${RENDERER_MINGW_CXX_COMPILER}")
    if(_compiler MATCHES "Git")
        message(FATAL_ERROR
            "Renderer toolchain points into Git's bundled MinGW: ${_compiler}\n"
            "Git's MinGW is a known poison: its libwinpthread DLL conflicts\n"
            "with the project MinGW (the legacy build script used to filter\n"
            "Set the RENDERER_MINGW_ROOT environment variable to a standalone\n"
            "MinGW-w64 installation (for example C:/mingw64) and configure\n"
            "again.")
    endif()
endforeach()

# --- Hard gate 2: every pinned tool must exist -------------------------------

foreach(_tool
        "${RENDERER_MINGW_C_COMPILER}"
        "${RENDERER_MINGW_CXX_COMPILER}"
        "${RENDERER_MINGW_MAKE_PROGRAM}")
    if(NOT EXISTS "${_tool}")
        message(FATAL_ERROR
            "Renderer MinGW tool not found: ${_tool}\n"
            "Set the RENDERER_MINGW_ROOT environment variable to your\n"
            "MinGW-w64 install root (current value: ${RENDERER_MINGW_ROOT})\n"
            "and configure again.")
    endif()
endforeach()

# --- Pinned toolchain (absolute paths; PATH order becomes irrelevant) --------

set(CMAKE_C_COMPILER   "${RENDERER_MINGW_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${RENDERER_MINGW_CXX_COMPILER}")
set(CMAKE_MAKE_PROGRAM "${RENDERER_MINGW_MAKE_PROGRAM}")
