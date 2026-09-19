# -----------------------------------------------------------------------------
# DeployQtTest.cmake — unified windeployqt POST_BUILD helpers (P1b, §C.3)
#
# Replaces the 41 copy-pasted per-test windeployqt blocks that used to live
# inline in tests/CMakeLists.txt (152 occurrences of "windeployqt") with
# one-line calls:
#
#     pet_deploy_qt_test(ProtocolFixturesTest)
#
# and gives the main panel exe the post-build deployment step previously run
# by the retired Python build orchestrator:
#
#     pet_deploy_qt_app(desktop-pet-controller-qt)
#
# Behavior contract (matches the previous inline blocks):
#   - WIN32 only; the functions are complete no-ops on other hosts.
#   - windeployqt is located relative to the Qt in use (Qt6_DIR =
#     <qt>/lib/cmake/Qt6 → <qt>/bin). The lookup runs once per configure.
#   - A missing windeployqt is NOT a hard error — a single STATUS message
#     notes that tests may fail to run under ctest on Windows (the inline
#     blocks warned per-block; the tolerance is kept, the noise is not).
#   - windeployqt itself is idempotent (re-running just re-copies the same
#     Qt DLLs next to the exe), so POST_BUILD re-runs on incremental
#     rebuilds are safe. Calling a pet_deploy_qt_* function twice on the
#     same target is likewise a no-op (guarded via a target property).
#
# Design contract: docs/refactor/plugin-architecture-and-cmake-migration.md
# §C.3 (windeployqt 统一函数).
# -----------------------------------------------------------------------------

include_guard(GLOBAL)

# One-shot windeployqt lookup shared by both wrappers below. Sets the
# PET_WINDEPLOYQT_EXE cache variable; prints a single STATUS note when the
# tool cannot be found.
function(_pet_find_windeployqt)
    if(PET_WINDEPLOYQT_SEARCHED)
        return()
    endif()
    set(PET_WINDEPLOYQT_SEARCHED TRUE CACHE INTERNAL "windeployqt lookup attempted")

    # Qt6_DIR = <qt>/lib/cmake/Qt6 → <qt>/bin holds windeployqt(.exe)
    get_filename_component(_pet_qt_bin_dir "${Qt6_DIR}/../../../bin" ABSOLUTE)
    find_program(PET_WINDEPLOYQT_EXE
        NAMES windeployqt
        HINTS "${_pet_qt_bin_dir}"
        NO_DEFAULT_PATH)
    if(NOT PET_WINDEPLOYQT_EXE)
        message(STATUS
            "pet_deploy_qt: windeployqt not found under ${_pet_qt_bin_dir}; "
            "test/app executables may fail to run under ctest on Windows.")
    endif()
endfunction()

# Internal: shared guard + POST_BUILD registration.
#   _pet_target      — target to attach the step to
#   _pet_comment     — COMMENT string
#   ARGN             — windeployqt arguments (flags + exe path last)
function(_pet_deploy_qt_impl _pet_target _pet_comment)
    if(NOT WIN32)
        return()
    endif()
    get_target_property(_pet_deployed "${_pet_target}" PET_QT_DEPLOY_DONE)
    if(_pet_deployed)
        message(STATUS
            "pet_deploy_qt: ${_pet_target} already has a windeployqt step "
            "(idempotent no-op)")
        return()
    endif()
    _pet_find_windeployqt()
    if(NOT PET_WINDEPLOYQT_EXE)
        return()
    endif()
    set_target_properties("${_pet_target}" PROPERTIES PET_QT_DEPLOY_DONE TRUE)
    add_custom_command(TARGET "${_pet_target}" POST_BUILD
        COMMAND "${PET_WINDEPLOYQT_EXE}" ${ARGN}
        COMMENT "${_pet_comment}")
endfunction()

# Test flavor — exact replica of the flags the 41 inline blocks used:
# trimmed deployment (tests need no translations / D3D compiler / software
# OpenGL / QML import scan).
function(pet_deploy_qt_test target)
    _pet_deploy_qt_impl("${target}"
        "Running windeployqt for ${target}"
        --no-translations
        --no-system-d3d-compiler
        --no-opengl-sw
        --no-quick-import
        "$<TARGET_FILE:${target}>")
endfunction()

# App flavor — exact replica of the retired orchestrator's post-build
# invocation for the main exe: full deployment with --qmldir pointing at the QML
# sources so the QML plugin DLLs ship next to the standalone panel.
function(pet_deploy_qt_app target)
    _pet_deploy_qt_impl("${target}"
        "Running windeployqt for ${target}"
        --qmldir "${PROJECT_SOURCE_DIR}/qml"
        "$<TARGET_FILE:${target}>")
endfunction()
