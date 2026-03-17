@echo off
setlocal enabledelayedexpansion

set SCRIPT_PATH=%~dp0
set RENDERER_PATH=%SCRIPT_PATH%..
set BUILD_PATH=%RENDERER_PATH%\..\build\renderer_mingw

echo === Desktop Pet Renderer - MinGW Build ===
echo.

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

echo [2/3] Configuring CMake (MinGW Makefiles)...
cmake -S "%RENDERER_PATH%" -B "%BUILD_PATH%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
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
echo === Build successful! ===
echo Output: %RENDERER_PATH%\..\build\bin\
endlocal
