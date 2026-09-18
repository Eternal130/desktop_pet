@echo off
REM Fetch Live2D Cubism Core binaries into the CubismNativeSamples submodule.
REM
REM The git submodule third_party\CubismSdkForNative (Live2D/CubismNativeSamples)
REM ships Core documentation only - the prebuilt Live2DCubismCore libraries
REM (Core\dll, Core\lib, Core\include) are distributed separately by Live2D
REM and must be fetched before building the renderer.
REM
REM Usage (after cloning / pulling):
REM   git submodule update --init --recursive
REM   scripts\fetch_cubism_core.bat
REM
REM Environment overrides:
REM   CUBISM_SDK_VERSION  SDK version to fetch (default: 5-r.5-beta.3.1 - MUST
REM                       match the submodule tag in .gitmodules)
REM   CUBISM_SDK_URL      Full zip URL override (e.g. a local mirror)
REM   FORCE=1             Re-extract even if Core is already populated
REM
REM NOTE: Downloading/using the Cubism SDK implies acceptance of the Live2D
REM Proprietary Software License Agreement (see Core\LICENSE.md in the SDK).
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "SDK_DIR=%SCRIPT_DIR%..\third_party\CubismSdkForNative"

if not exist "%SDK_DIR%\.git" (
    if not exist "%SDK_DIR%\Core\README.md" (
        echo ERROR: %SDK_DIR% is not initialized.
        echo Run: git submodule update --init --recursive
        exit /b 1
    )
)

if "%CUBISM_SDK_VERSION%"=="" set "CUBISM_SDK_VERSION=5-r.5-beta.3.1"
if "%CUBISM_SDK_URL%"=="" (
    set "CUBISM_SDK_URL=https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-%CUBISM_SDK_VERSION%.zip"
)

if exist "%SDK_DIR%\Core\dll\windows\x86_64\Live2DCubismCore.dll" (
    if not "%FORCE%"=="1" (
        echo Cubism Core already populated at %SDK_DIR%\Core ^(FORCE=1 to re-fetch^).
        exit /b 0
    )
)

set "TMP_ZIP=%TEMP%\cubism-core-%CUBISM_SDK_VERSION%.zip"
echo Downloading Cubism SDK Core %CUBISM_SDK_VERSION% ...
echo   %CUBISM_SDK_URL%
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; $ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%CUBISM_SDK_URL%' -OutFile '%TMP_ZIP%'"
if errorlevel 1 (
    echo ERROR: download failed.
    exit /b 1
)

echo Extracting Core\ ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference='Stop';" ^
    "$sdk = (Resolve-Path '%SDK_DIR%').Path;" ^
    "Add-Type -AssemblyName System.IO.Compression.FileSystem;" ^
    "$zip = [IO.Compression.ZipFile]::OpenRead('%TMP_ZIP%');" ^
    "try {" ^
    "  $coreEntries = $zip.Entries | Where-Object { $_.FullName -match '/Core/' };" ^
    "  if (-not $coreEntries) { throw 'no Core/ directory found in the downloaded zip' };" ^
    "  foreach ($e in $coreEntries) {" ^
    "    $rel = [regex]::Match($e.FullName, '^(.*?/)?Core/(.*)$').Groups[2].Value;" ^
    "    if (-not $rel) { continue };" ^
    "    $dst = Join-Path (Join-Path $sdk 'Core') $rel;" ^
    "    $dir = Split-Path $dst -Parent;" ^
    "    if (-not (Test-Path $dir)) { [IO.Directory]::CreateDirectory($dir) | Out-Null };" ^
    "    if ($rel.EndsWith('/')) { continue };" ^
    "    [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dst, $true);" ^
    "  }" ^
    "} finally { $zip.Dispose() }" ^
    "if (Test-Path '%TMP_ZIP%') { Remove-Item '%TMP_ZIP%' -Force }"
if errorlevel 1 (
    echo ERROR: extraction failed.
    exit /b 1
)

set "MISSING=0"
for %%M in (
    "Core\dll\windows\x86_64\Live2DCubismCore.dll"
    "Core\dll\windows\x86_64\Live2DCubismCore.lib"
    "Core\dll\linux\x86_64\libLive2DCubismCore.so"
    "Core\include\Live2DCubismCore.h"
) do (
    if not exist "%SDK_DIR%\%%~M" (
        echo   MISSING: %%~M
        set "MISSING=1"
    )
)
if "%MISSING%"=="1" (
    echo ERROR: expected Core files are missing after extraction.
    exit /b 1
)

echo Done. Core %CUBISM_SDK_VERSION% populated at %SDK_DIR%\Core
endlocal
