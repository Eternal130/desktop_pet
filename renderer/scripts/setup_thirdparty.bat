@echo off

set GLEW_VERSION=2.2.0
set GLFW_VERSION=3.4

set SCRIPT_PATH=%~dp0
set THIRD_PARTY_PATH=%SCRIPT_PATH%..\..\third_party\CubismSdkForNative\Samples\OpenGL\thirdParty

cd %THIRD_PARTY_PATH%

if exist glew (
    echo GLEW already exists, skipping.
) else (
    echo - Setup GLEW %GLEW_VERSION%
    echo Downloading...
    curl -fsSL -o glew.zip ^
      "https://github.com/nigels-com/glew/releases/download/glew-%GLEW_VERSION%/glew-%GLEW_VERSION%.zip"
    if %errorlevel% neq 0 pause & exit /b %errorlevel%
    echo Extracting...
    powershell "$progressPreference = 'silentlyContinue'; expand-archive -force glew.zip ."
    if %errorlevel% neq 0 pause & exit /b %errorlevel%
    ren glew-%GLEW_VERSION% glew
    del glew.zip
    echo GLEW setup complete.
)

echo.

if exist glfw (
    echo GLFW already exists, skipping.
) else (
    echo - Setup GLFW %GLFW_VERSION%
    echo Downloading...
    curl -fsSL -o glfw.zip ^
      "https://github.com/glfw/glfw/releases/download/%GLFW_VERSION%/glfw-%GLFW_VERSION%.zip"
    if %errorlevel% neq 0 pause & exit /b %errorlevel%
    echo Extracting...
    powershell "$progressPreference = 'silentlyContinue'; expand-archive -force glfw.zip ."
    if %errorlevel% neq 0 pause & exit /b %errorlevel%
    ren glfw-%GLFW_VERSION% glfw
    del glfw.zip
    echo GLFW setup complete.
)

echo.
echo Third-party setup done.
