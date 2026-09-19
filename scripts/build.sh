#!/usr/bin/env bash
# ============================================================================
# build.sh - thin forwarder: full build via CMake presets (Linux).
# Replaces the retired Python interactive build menu.
# Boundary (docs/refactor §C.4): shell only forwards; build logic lives in CMake.
# Prerequisites: git submodule update --init --recursive && cmake -P scripts/Bootstrap.cmake
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

(cd controller_qt && cmake --workflow --preset linux-release)
(cd renderer && cmake --workflow --preset linux-gl-release)
