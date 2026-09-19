# -----------------------------------------------------------------------------
# qt-mingw-qt.cmake — Qt-bundled MinGW 13.1.0 toolchain for the Qt controller
#
# Referenced only by the Windows configure presets in CMakePresets.json
# (win-qt-release / win-qt-debug). Pins absolute compiler/make/Qt-prefix paths
# so PATH order can no longer decide which toolchain links the panel — the
# root-cause fix for the AGENTS.md "MinGW PATH (Qt controller)" pitfall that
# was previously mitigated by PATH filtering in the retired Python build
# orchestrator.
#
# Environment overrides (each checked before its hardcoded default):
#   QT_MINGW_ROOT    MinGW root       (default: C:/Qt/Tools/mingw1310_64)
#   QT_NINJA         ninja executable (default: C:/Qt/Tools/Ninja/ninja.exe)
#   QT_PREFIX_PATH   Qt install root  (default: C:/Qt/6.10.0/mingw_64)
#
# Design contract: docs/refactor/plugin-architecture-and-cmake-migration.md
# §C.2 (dual-MinGW: toolchain file pins absolute paths, not PATH filtering).
# -----------------------------------------------------------------------------

# --- MinGW 13.1.0 root (env QT_MINGW_ROOT overrides the default) --------------
set(QT_MINGW_ROOT_DEFAULT "C:/Qt/Tools/mingw1310_64")

if(DEFINED ENV{QT_MINGW_ROOT} AND NOT "$ENV{QT_MINGW_ROOT}" STREQUAL "")
    set(QT_MINGW_ROOT "$ENV{QT_MINGW_ROOT}")
    message(STATUS "qt-mingw-qt: QT_MINGW_ROOT env override -> ${QT_MINGW_ROOT}")
else()
    set(QT_MINGW_ROOT "${QT_MINGW_ROOT_DEFAULT}")
    message(STATUS "qt-mingw-qt: QT_MINGW_ROOT not set, using default: ${QT_MINGW_ROOT}")
endif()

# Normalize to forward slashes — env-provided values on Windows runners can
# mix separators (e.g. "D:\a\...\Tools/mingw1310_64"); CMake wants canonical
# forward-slash paths (the Ninja generator in particular chokes opaquely on
# mixed forms inside its build-tool invocations).
string(REPLACE "\\" "/" QT_MINGW_ROOT "${QT_MINGW_ROOT}")

set(CMAKE_C_COMPILER   "${QT_MINGW_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${QT_MINGW_ROOT}/bin/g++.exe")

# --- Ninja (Qt-bundled; env QT_NINJA overrides the default) --------------------
if(DEFINED ENV{QT_NINJA} AND NOT "$ENV{QT_NINJA}" STREQUAL "")
    set(QT_NINJA_EXE "$ENV{QT_NINJA}")
    message(STATUS "qt-mingw-qt: QT_NINJA env override -> ${QT_NINJA_EXE}")
else()
    set(QT_NINJA_EXE "C:/Qt/Tools/Ninja/ninja.exe")
endif()

string(REPLACE "\\" "/" QT_NINJA_EXE "${QT_NINJA_EXE}")

if(NOT EXISTS "${QT_NINJA_EXE}")
    message(FATAL_ERROR
        "qt-mingw-qt: CMAKE_MAKE_PROGRAM not found: ${QT_NINJA_EXE}\n"
        "The Qt controller uses the Ninja generator, which requires Qt's "
        "bundled Ninja. Install it via the Qt Maintenance Tool "
        "(Additional Libraries > Ninja) or point the QT_NINJA environment "
        "variable at your ninja executable.")
endif()
# CACHE + FORCE is load-bearing: CMAKE_MAKE_PROGRAM crosses the try_compile
# boundary (compiler ABI detection sub-build) ONLY via the parent's CACHE —
# a normal variable set here never reaches it, the sub-build then uses an
# explicit EMPTY build tool (which also suppresses find_program fallback, so
# a PATH-based ninja never gets discovered). vcpkg/Qt toolchains all pin it
# this way.
set(CMAKE_MAKE_PROGRAM "${QT_NINJA_EXE}" CACHE FILEPATH
    "Ninja build tool (pinned by qt-mingw-qt toolchain)" FORCE)

# --- Qt prefix path (env QT_PREFIX_PATH overrides the default) -----------------
if(DEFINED ENV{QT_PREFIX_PATH} AND NOT "$ENV{QT_PREFIX_PATH}" STREQUAL "")
    set(QT_PREFIX_PATH_RESOLVED "$ENV{QT_PREFIX_PATH}")
    message(STATUS "qt-mingw-qt: QT_PREFIX_PATH env override -> ${QT_PREFIX_PATH_RESOLVED}")
else()
    set(QT_PREFIX_PATH_RESOLVED "C:/Qt/6.10.0/mingw_64")
endif()
string(REPLACE "\\" "/" QT_PREFIX_PATH_RESOLVED "${QT_PREFIX_PATH_RESOLVED}")
# CACHE + FORCE for the same try_compile propagation reason as
# CMAKE_MAKE_PROGRAM above (future check_cxx_source_compiles probes that
# include Qt headers would otherwise run without the prefix in sub-builds).
set(CMAKE_PREFIX_PATH "${QT_PREFIX_PATH_RESOLVED}" CACHE PATH
    "Qt install prefix (pinned by qt-mingw-qt toolchain)" FORCE)

# Force the env-derived toolchain variables into try_compile sub-builds
# (compiler ABI detection): without this, the sub-CMake may neither re-run
# this toolchain nor receive CMAKE_MAKE_PROGRAM, and — with no ninja on
# PATH — fails with an empty build-tool command. Registered here so local
# builds benefit even when the runner-style PATH fallback is absent.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     QT_MINGW_ROOT QT_NINJA_EXE QT_PREFIX_PATH_RESOLVED)

# --- Configure-time hard validation: MinGW ABI lock ----------------------------
# The official Qt 6.10.0 mingw_64 binaries are only binary-compatible with the
# MinGW 13.1.0 runtime they were built against (Qt's bundled mingw1310_64).
# Linking the controller with any other MinGW — typically Git's bundled MinGW
# leaking in through PATH, or a different GCC version — produces mixed-runtime
# binaries. This is exactly the AGENTS.md known pitfall "MinGW PATH (Qt
# controller)"; this check promotes that documentation convention into a
# build-time gate (docs/refactor §C.2 ①②③).
if(NOT CMAKE_CXX_COMPILER MATCHES "mingw1310_64")
    message(FATAL_ERROR
        "qt-mingw-qt: CMAKE_CXX_COMPILER '${CMAKE_CXX_COMPILER}' does not "
        "point at Qt's bundled MinGW 13.1.0 (mingw1310_64).\n"
        "The Qt controller MUST compile and link with the MinGW runtime that "
        "matches the official Qt 6.10.0 mingw_64 build. A different MinGW "
        "(e.g. Git's bundled MinGW mixed in via PATH, or another GCC version) "
        "yields mismatched runtimes and link/ABI failures — see AGENTS.md, "
        "known pitfall 'MinGW PATH (Qt controller)'.\n"
        "If your Qt MinGW is installed elsewhere, set the QT_MINGW_ROOT "
        "environment variable to the mingw1310_64 root, e.g. "
        "QT_MINGW_ROOT=D:/Qt/Tools/mingw1310_64")
endif()
