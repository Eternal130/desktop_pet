# =============================================================================
# scripts/Bootstrap.cmake - one-shot dependency bootstrap (double-platform)
#
# Replaces scripts/fetch_cubism_core.sh|.bat (Cubism Core) and the GLEW/GLFW
# download logic of the former Python build orchestrator. Run in script mode:
#
#     cmake -P scripts/Bootstrap.cmake          (from the repository root)
#
# What it does (all steps idempotent - existing artifacts are skipped):
#   1. Cubism Core  : downloads the official CubismSdkForNative zip and
#                     extracts only Core/ into third_party/CubismSdkForNative/
#                     (the submodule ships Core documentation only).
#   2. GLEW 2.2.0   : downloads and extracts to Samples/OpenGL/thirdParty/glew
#   3. GLFW 3.4     : downloads and extracts to Samples/OpenGL/thirdParty/glfw
#
# Environment overrides:
#   CUBISM_SDK_VERSION  SDK version to fetch (default: 5-r.5-beta.3.1 - MUST
#                       match the submodule tag in .gitmodules)
#   CUBISM_SDK_URL      Full zip URL override (e.g. a local mirror)
#   FORCE=1             Re-fetch Cubism Core even if already populated
#                       (GLEW/GLFW keep their skip-if-present rule)
#   PET_REPO_ROOT       Repository root override (default: parent of this
#                       script's directory) - handy for scratch-directory tests
#
# Boundary (docs/refactor/plugin-architecture-and-cmake-migration.md section
# C.4): CMake never mutates git state - submodule init stays a manual
# `git submodule update --init --recursive`.
#
# NOTE: Downloading/using the Cubism SDK implies acceptance of the Live2D
# Proprietary Software License Agreement (see Core/LICENSE.md in the SDK).
# =============================================================================

cmake_minimum_required(VERSION 3.20)

# --- Repository root ----------------------------------------------------------
if(DEFINED ENV{PET_REPO_ROOT} AND NOT "$ENV{PET_REPO_ROOT}" STREQUAL "")
    set(_repo_root "$ENV{PET_REPO_ROOT}")
else()
    set(_repo_root "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
cmake_path(ABSOLUTE_PATH _repo_root NORMALIZE OUTPUT_VARIABLE BS_REPO_ROOT)

set(BS_SDK_DIR "${BS_REPO_ROOT}/third_party/CubismSdkForNative")
set(BS_THIRD_PARTY_DIR "${BS_SDK_DIR}/Samples/OpenGL/thirdParty")
message(STATUS "Bootstrap: repository root = ${BS_REPO_ROOT}")

# --- Download with retry ------------------------------------------------------
# file(DOWNLOAD) + STATUS check + 3 attempts, mirroring curl's --retry 3 in the
# retired fetch_cubism_core.sh.
function(bs_download url dest)
    set(_attempt 0)
    set(_msg "unknown error")
    while(_attempt LESS 3)
        math(EXPR _attempt "${_attempt} + 1")
        message(STATUS "Downloading (attempt ${_attempt}/3): ${url}")
        file(DOWNLOAD "${url}" "${dest}" STATUS _status TLS_VERIFY ON SHOW_PROGRESS)
        list(GET _status 0 _code)
        if(_code EQUAL 0)
            return()
        endif()
        list(GET _status 1 _msg)
        message(STATUS "  attempt ${_attempt}/3 failed: ${_msg}")
        file(REMOVE "${dest}")
    endwhile()
    message(FATAL_ERROR
        "Download failed after 3 attempts: ${url}\n"
        "Last error: ${_msg}\n"
        "Check the network (or point the relevant *_URL environment variable "
        "at a mirror) and re-run:\n"
        "  cmake -P scripts/Bootstrap.cmake")
endfunction()

# --- GLEW / GLFW style dependency: download zip, extract, rename --------------
# Layout parity with the retired Python logic: the zip's top-level
# <strip_prefix>/ directory is renamed to <final_name> directly inside
# Samples/OpenGL/thirdParty/.
function(bs_fetch_dependency url strip_prefix final_name)
    if(EXISTS "${BS_THIRD_PARTY_DIR}/${final_name}")
        message(STATUS "${final_name}: already present, skipping")
        return()
    endif()
    file(MAKE_DIRECTORY "${BS_THIRD_PARTY_DIR}")
    set(_zip "${BS_THIRD_PARTY_DIR}/_download.zip")
    bs_download("${url}" "${_zip}")
    message(STATUS "Extracting ${_zip} -> ${BS_THIRD_PARTY_DIR}")
    file(ARCHIVE_EXTRACT INPUT "${_zip}" DESTINATION "${BS_THIRD_PARTY_DIR}")
    file(REMOVE "${_zip}")
    if(EXISTS "${BS_THIRD_PARTY_DIR}/${strip_prefix}" AND NOT EXISTS "${BS_THIRD_PARTY_DIR}/${final_name}")
        file(RENAME "${BS_THIRD_PARTY_DIR}/${strip_prefix}" "${BS_THIRD_PARTY_DIR}/${final_name}")
    endif()
    if(NOT EXISTS "${BS_THIRD_PARTY_DIR}/${final_name}")
        message(FATAL_ERROR
            "Extraction of ${url} did not produce the expected directory:\n"
            "  ${BS_THIRD_PARTY_DIR}/${final_name}")
    endif()
    message(STATUS "${final_name}: ready at ${BS_THIRD_PARTY_DIR}/${final_name}")
endfunction()

# --- Cubism Core --------------------------------------------------------------
function(bs_fetch_cubism_core)
    if(DEFINED ENV{CUBISM_SDK_VERSION} AND NOT "$ENV{CUBISM_SDK_VERSION}" STREQUAL "")
        set(_version "$ENV{CUBISM_SDK_VERSION}")
    else()
        set(_version "5-r.5-beta.3.1")
    endif()
    if(DEFINED ENV{CUBISM_SDK_URL} AND NOT "$ENV{CUBISM_SDK_URL}" STREQUAL "")
        set(_url "$ENV{CUBISM_SDK_URL}")
    else()
        set(_url "https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-${_version}.zip")
    endif()
    set(_force FALSE)
    if("$ENV{FORCE}" STREQUAL "1")
        set(_force TRUE)
    endif()

    # The submodule must be initialized (a .git file/dir, or the Core docs
    # README for non-submodule checkouts).
    if(NOT EXISTS "${BS_SDK_DIR}/.git" AND NOT EXISTS "${BS_SDK_DIR}/Core/README.md")
        message(FATAL_ERROR
            "Cubism SDK submodule not initialized: ${BS_SDK_DIR}\n"
            "Run: git submodule update --init --recursive")
    endif()

    # Populated = Core/include AND Core/dll exist, with dll/ non-empty.
    set(_populated FALSE)
    if(IS_DIRECTORY "${BS_SDK_DIR}/Core/include" AND IS_DIRECTORY "${BS_SDK_DIR}/Core/dll")
        file(GLOB _dll_entries "${BS_SDK_DIR}/Core/dll/*")
        if(_dll_entries)
            set(_populated TRUE)
        endif()
    endif()
    if(_populated AND NOT _force)
        message(STATUS "Cubism Core already populated at ${BS_SDK_DIR}/Core (FORCE=1 to re-fetch), skipping")
        return()
    endif()
    if(_force)
        message(STATUS "FORCE=1: re-fetching Cubism Core")
    endif()

    set(_tmp "${BS_REPO_ROOT}/build/bootstrap-tmp/cubism-core")
    file(REMOVE_RECURSE "${_tmp}")
    file(MAKE_DIRECTORY "${_tmp}/extract")

    bs_download("${_url}" "${_tmp}/sdk.zip")

    # Extract only Core/ (the zip's top-level is CubismSdkForNative-<version>/).
    # NOTE: patterns must each match something — libarchive treats an unmatched
    # pattern as an error ("Not found in archive") — so keep this to the one
    # shape the official zips actually use.
    message(STATUS "Extracting Core/ ...")
    file(ARCHIVE_EXTRACT INPUT "${_tmp}/sdk.zip" DESTINATION "${_tmp}/extract"
         PATTERNS "*/Core/*")

    # Locate the extracted Core directory: root-level first (official zips
    # are <prefix>/Core, i.e. one level down), one extra level as fallback.
    set(_core_src "")
    if(IS_DIRECTORY "${_tmp}/extract/Core" AND IS_DIRECTORY "${_tmp}/extract/Core/dll")
        set(_core_src "${_tmp}/extract/Core")
    endif()
    file(GLOB _lvl1 LIST_DIRECTORIES true "${_tmp}/extract/*")
    file(GLOB _lvl2 LIST_DIRECTORIES true "${_tmp}/extract/*/*")
    foreach(_dir IN LISTS _lvl1 _lvl2)
        if(_core_src STREQUAL "" AND IS_DIRECTORY "${_dir}/Core" AND IS_DIRECTORY "${_dir}/Core/dll")
            set(_core_src "${_dir}/Core")
        endif()
    endforeach()
    if(_core_src STREQUAL "")
        message(FATAL_ERROR "Could not locate Core/ with dll/ inside the downloaded archive")
    endif()

    message(STATUS "Copying into ${BS_SDK_DIR}/Core")
    file(COPY "${_core_src}/" DESTINATION "${BS_SDK_DIR}/Core")
    file(REMOVE_RECURSE "${BS_REPO_ROOT}/build/bootstrap-tmp")

    # Platform runtime checklist (same 4 markers as the retired fetch scripts).
    set(_missing "")
    foreach(_marker
            "Core/dll/linux/x86_64/libLive2DCubismCore.so"
            "Core/dll/windows/x86_64/Live2DCubismCore.dll"
            "Core/dll/windows/x86_64/Live2DCubismCore.lib"
            "Core/include/Live2DCubismCore.h")
        if(NOT EXISTS "${BS_SDK_DIR}/${_marker}")
            string(APPEND _missing "\n  MISSING: ${_marker}")
        endif()
    endforeach()
    if(NOT _missing STREQUAL "")
        message(FATAL_ERROR "Expected Cubism Core files are missing after extraction:${_missing}")
    endif()
    message(STATUS "Cubism Core ${_version} populated at ${BS_SDK_DIR}/Core")
endfunction()

# --- Main ---------------------------------------------------------------------
bs_fetch_cubism_core()
bs_fetch_dependency(
    "https://github.com/nigels-com/glew/releases/download/glew-2.2.0/glew-2.2.0.zip"
    "glew-2.2.0" "glew")
bs_fetch_dependency(
    "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip"
    "glfw-3.4" "glfw")
message(STATUS "Bootstrap complete.")
