@echo off
setlocal enabledelayedexpansion

set SCRIPT_PATH=%~dp0
set RENDERER_PATH=%SCRIPT_PATH%..

REM Parse command-line arguments
set USE_VULKAN=OFF
set CLEAN_BUILD=0
:parse_args
if "%~1"=="" goto :done_args
if /I "%~1"=="--vulkan" (
    set USE_VULKAN=ON
    shift
    goto :parse_args
)
if /I "%~1"=="--clean" (
    set CLEAN_BUILD=1
    shift
    goto :parse_args
)
if /I "%~1"=="--help" (
    echo Usage: build_mingw.bat [--vulkan] [--clean] [--help]
    echo.
    echo Options:
    echo   --vulkan    Build with Vulkan renderer backend (default: OpenGL)
    echo   --clean     Clean build directory before building
    echo   --help      Show this help message
    endlocal
    exit /b 0
)
echo WARNING: Unknown option '%~1'
shift
goto :parse_args
:done_args

REM Set build path based on backend
if "%USE_VULKAN%"=="ON" (
    set BUILD_PATH=%RENDERER_PATH%\..\build\renderer_vulkan_mingw
    set BACKEND_NAME=Vulkan
) else (
    set BUILD_PATH=%RENDERER_PATH%\..\build\renderer_mingw
    set BACKEND_NAME=OpenGL
)

echo === Desktop Pet Renderer - MinGW Build (%BACKEND_NAME%) ===
echo.

REM Clean build if requested
if %CLEAN_BUILD%==1 (
    echo [0/3] Cleaning build directory...
    if exist "%BUILD_PATH%" (
        rmdir /S /Q "%BUILD_PATH%"
    )
    echo.
)

REM Remove Git's bundled MinGW from PATH to avoid libwinpthread DLL conflict
set CLEANED_PATH=
for %%p in ("%PATH:;=";"%") do (
    echo %%~p | findstr /I /C:"\Git\mingw64\bin" >nul 2>&1
    if errorlevel 1 (
        if defined CLEANED_PATH (
            set "CLEANED_PATH=!CLEANED_PATH!;%%~p"
        ) else (
            set "CLEANED_PATH=%%~p"
        )
    )
)
set "PATH=%CLEANED_PATH%"

echo [1/3] Setting up third-party dependencies...
call "%SCRIPT_PATH%setup_thirdparty.bat"
if %errorlevel% neq 0 (
    echo ERROR: Third-party setup failed.
    exit /b %errorlevel%
)
echo.

echo [2/3] Configuring CMake (MinGW Makefiles, %BACKEND_NAME% backend)...
cmake -S "%RENDERER_PATH%" -B "%BUILD_PATH%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DUSE_VULKAN=%USE_VULKAN%
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed.
    exit /b %errorlevel%
)
echo.

echo [3/3] Building...
cmake --build "%BUILD_PATH%" --config Release -j%NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 (
    echo ERROR: Build failed.
    exit /b %errorlevel%
)

echo.
echo === Build successful! (%BACKEND_NAME%) ===
echo Output: %RENDERER_PATH%\..\build\bin\
endlocal
