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
