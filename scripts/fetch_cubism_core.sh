#!/usr/bin/env bash
# Fetch Live2D Cubism Core binaries into the CubismNativeSamples submodule.
#
# The git submodule third_party/CubismSdkForNative (Live2D/CubismNativeSamples)
# ships Core documentation only — the prebuilt Live2DCubismCore libraries
# (Core/dll, Core/lib, Core/include) are distributed separately by Live2D
# and must be fetched before building the renderer.
#
# Usage (after cloning / pulling):
#   git submodule update --init --recursive
#   scripts/fetch_cubism_core.sh
#
# Environment overrides:
#   CUBISM_SDK_VERSION  SDK version to fetch (default: 5-r.5-beta.3.1 — MUST
#                       match the submodule tag in .gitmodules)
#   CUBISM_SDK_URL      Full zip URL override (e.g. a local mirror)
#   FORCE=1             Re-extract even if Core is already populated
#
# NOTE: Downloading/using the Cubism SDK implies acceptance of the Live2D
# Proprietary Software License Agreement (see Core/LICENSE.md in the SDK).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="$SCRIPT_DIR/../third_party/CubismSdkForNative"

VERSION="${CUBISM_SDK_VERSION:-5-r.5-beta.3.1}"
URL="${CUBISM_SDK_URL:-https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-${VERSION}.zip}"

if [ ! -d "$SDK_DIR/.git" ] && [ ! -f "$SDK_DIR/.git" ]; then
    echo "ERROR: $SDK_DIR is not initialized." >&2
    echo "Run: git submodule update --init --recursive" >&2
    exit 1
fi

core_populated() {
    [ -d "$SDK_DIR/Core/include" ] && [ -d "$SDK_DIR/Core/dll" ] \
        && [ -n "$(ls -A "$SDK_DIR/Core/dll" 2>/dev/null)" ]
}

if core_populated && [ "${FORCE:-0}" != "1" ]; then
    echo "Cubism Core already populated at $SDK_DIR/Core (FORCE=1 to re-fetch)."
    exit 0
fi

TMPDIR_FETCH="$(mktemp -d "${TMPDIR:-/tmp}/cubism-core.XXXXXX")"
trap 'rm -rf "$TMPDIR_FETCH"' EXIT

echo "Downloading Cubism SDK Core ${VERSION} ..."
echo "  $URL"
curl -fL --retry 3 --connect-timeout 20 -o "$TMPDIR_FETCH/sdk.zip" "$URL"

echo "Extracting Core/ ..."
SCRIPT_DIR="$SCRIPT_DIR" SDK_DIR="$SDK_DIR" TMPDIR_FETCH="$TMPDIR_FETCH" python3 - <<'PYEOF'
import os, shutil, sys, tempfile, zipfile

sdk_dir = os.environ["SDK_DIR"]
tmp = os.environ["TMPDIR_FETCH"]
zip_path = os.path.join(tmp, "sdk.zip")

with zipfile.ZipFile(zip_path) as zf:
    bad = zf.testzip()
    if bad is not None:
        sys.exit(f"ERROR: corrupt zip entry {bad}")
    names = [n for n in zf.namelist() if "/Core/" in n or n.endswith("/Core")]
    if not names:
        sys.exit("ERROR: no Core/ directory found in the downloaded zip")
    extract_root = tempfile.mkdtemp(dir=tmp)
    zf.extractall(extract_root, members=names)

# Locate the extracted Core directory (zip top-level prefix varies by version)
core_src = None
for root, dirs, _files in os.walk(extract_root):
    if os.path.basename(root) == "Core" and os.path.isdir(os.path.join(root, "dll")):
        core_src = root
        break
if core_src is None:
    sys.exit("ERROR: could not locate Core/ with dll/ inside the archive")

core_dst = os.path.join(sdk_dir, "Core")
os.makedirs(core_dst, exist_ok=True)
copied = 0
for entry in os.listdir(core_src):
    src = os.path.join(core_src, entry)
    dst = os.path.join(core_dst, entry)
    if os.path.isdir(src):
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
    else:
        shutil.copy2(src, dst)
        copied += 1
shutil.rmtree(extract_root, ignore_errors=True)
print(f"  Copied into {core_dst} ({copied} files + directories)")
PYEOF

echo "Verifying platform runtimes ..."
MISSING=0
for marker in \
    "Core/dll/linux/x86_64/libLive2DCubismCore.so" \
    "Core/dll/windows/x86_64/Live2DCubismCore.dll" \
    "Core/dll/windows/x86_64/Live2DCubismCore.lib" \
    "Core/include/Live2DCubismCore.h"
do
    if [ ! -f "$SDK_DIR/$marker" ]; then
        echo "  MISSING: $marker" >&2
        MISSING=1
    fi
done
if [ "$MISSING" = "1" ]; then
    echo "ERROR: expected Core files are missing after extraction." >&2
    exit 1
fi

echo "Done. Core ${VERSION} populated at $SDK_DIR/Core"
