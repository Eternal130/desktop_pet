@echo off
REM ============================================================================
REM build.bat - thin forwarder: full build via CMake presets (Windows).
REM Replaces the retired Python interactive build menu.
REM Boundary (docs/refactor section C.4): shell only forwards; build logic
REM lives in CMake.
REM Prerequisites: git submodule update --init --recursive ^&^& cmake -P scripts/Bootstrap.cmake
REM ============================================================================
setlocal
cd /d "%~dp0.."

cd controller_qt
cmake --workflow --preset win-release
if errorlevel 1 exit /b 1

cd ..\renderer
cmake --workflow --preset win-gl-release
if errorlevel 1 exit /b 1

endlocal
