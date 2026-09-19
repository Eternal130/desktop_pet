# ============================================================================
# NOTE (P1b): This script is now OPTIONAL dev-environment convenience only.
# The official build entry points are the per-component CMakePresets.json
# (cmake --workflow --preset ...) plus scripts/build.bat — see BUILD.md.
# The toolchains themselves are pinned by the toolchain files
# (controller_qt/cmake/qt-mingw-qt.cmake, renderer/cmake/toolchain-mingw-renderer.cmake,
# env overrides QT_MINGW_ROOT / QT_NINJA / QT_PREFIX_PATH / RENDERER_MINGW_ROOT),
# so sourcing this script is no longer required for building.
# ============================================================================
# setup-dev-env.ps1 - Qt 6 + Slint Dev Environment Activation (session only)
# ============================================================================
# Usage:
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
#   . .\scripts\setup-dev-env.ps1
# ============================================================================

param(
    [string]$QtRoot = "C:\Qt"
)

$ErrorActionPreference = "Stop"

# Auto-detect Qt version (pick latest)
$qtVersionDir = Get-ChildItem $QtRoot -Directory | Where-Object { $_.Name -match "^\d+\.\d+\.\d+$" } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $qtVersionDir) {
    Write-Error "No Qt version found under $QtRoot"
    return
}
$QtVersion = $qtVersionDir.Name

# Auto-detect compiler kit
$qtKitDir = Get-ChildItem "$QtRoot\$QtVersion" -Directory | Where-Object { $_.Name -match "mingw|msvc" } | Select-Object -First 1
if (-not $qtKitDir) {
    Write-Error "No compiler kit found under $QtRoot\$QtVersion"
    return
}
$QtKit = $qtKitDir.Name

Write-Host "Qt Version: $QtVersion ($QtKit)" -ForegroundColor Cyan

# Detect Qt-bundled MinGW
$qtMingwDir = $null
$mingwCandidate = Get-ChildItem "$QtRoot\Tools" -Directory -Filter "mingw*" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($mingwCandidate) {
    $qtMingwDir = $mingwCandidate.FullName
    Write-Host "MinGW (Qt bundled): $($mingwCandidate.Name)" -ForegroundColor Cyan
}

# Build PATH additions
$pathsToAdd = @("$QtRoot\$QtVersion\$QtKit\bin")
if ($qtMingwDir) { $pathsToAdd += "$qtMingwDir\bin" }
if (Test-Path "$QtRoot\Tools\Ninja") { $pathsToAdd += "$QtRoot\Tools\Ninja" }
$qtCmakeDir = "$QtRoot\Tools\CMake_64\bin"
if (Test-Path $qtCmakeDir) { $pathsToAdd += $qtCmakeDir }
$cargoBin = "$env:USERPROFILE\.cargo\bin"
if ((Test-Path $cargoBin) -and ($env:PATH -notlike "*$cargoBin*")) { $pathsToAdd += $cargoBin }

# Add to PATH (idempotent)
foreach ($p in $pathsToAdd) {
    if (Test-Path $p) {
        if ($env:PATH -notlike "*$p*") { $env:PATH = "$p;$env:PATH" }
    } else {
        Write-Warning "Path not found, skipped: $p"
    }
}

# Set CMAKE_PREFIX_PATH
$env:CMAKE_PREFIX_PATH = "$QtRoot\$QtVersion\$QtKit"

# Verify
Write-Host ""
Write-Host "=== Dev Environment Activated ===" -ForegroundColor Green
$qmakeVer = & qmake --version 2>&1 | Select-Object -First 1
Write-Host "  qmake:  $qmakeVer"
$gccVer = & g++ --version 2>&1 | Select-Object -First 1
Write-Host "  g++:    $gccVer"
$cmakeVer = & cmake --version 2>&1 | Select-Object -First 1
Write-Host "  cmake:  $cmakeVer"
$ninjaVer = & ninja --version 2>&1
Write-Host "  ninja:  $ninjaVer"
$rustVer = & rustc --version 2>&1
Write-Host "  rustc:  $rustVer"
$slintLsp = Get-Command slint-lsp -ErrorAction SilentlyContinue
if ($slintLsp) {
    Write-Host "  slint-lsp: $($slintLsp.Source)" -ForegroundColor Green
} else {
    Write-Host "  slint-lsp: NOT installed (run: cargo install slint-lsp)" -ForegroundColor Yellow
}
Write-Host ""
Write-Host "CMAKE_PREFIX_PATH = $env:CMAKE_PREFIX_PATH" -ForegroundColor DarkGray
Write-Host ""
Write-Host "Note: session-only. Re-source in new terminals." -ForegroundColor DarkYellow
