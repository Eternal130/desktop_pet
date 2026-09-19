#!/usr/bin/env bash
# packaging/linux/package.sh — Linux release packaging (tag-driven).
#
# Usage:
#     bash packaging/linux/package.sh <repo-root> <tag> <out-dir>
#
# Produces two artifacts from <repo-root>/build/bin:
#   <out-dir>/DesktopPet-<tag>-linux-x86_64.tar.gz  — portable bundle
#   <out-dir>/DesktopPet-<tag>-x86_64.AppImage      — AppImage (linuxdeploy)
#
# Exclusions (never ship): renderer_tests / renderer_tests.exe (GoogleTest),
# the retired PoC exe, *.pdb / *.lib. Everything else ships — including the
# Qt runtime as resolved by linuxdeploy's dependency deployment (system Qt
# locally, install-qt-action Qt in CI).
set -euo pipefail

REPO_ROOT=${1:?usage: package.sh <repo-root> <tag> <out-dir>}
TAG=${2:?usage: package.sh <repo-root> <tag> <out-dir>}
OUT_DIR=${3:?usage: package.sh <repo-root> <tag> <out-dir>}
BIN_DIR="$REPO_ROOT/build/bin"

if [ ! -x "$BIN_DIR/desktop-pet-controller-qt" ]; then
    echo "ERROR: $BIN_DIR/desktop-pet-controller-qt missing — build first" >&2
    exit 1
fi
mkdir -p "$OUT_DIR"

# ── (a) Portable tar.gz ─────────────────────────────────────────────────────
PORTABLE_STEM="DesktopPet-$TAG-linux-x86_64"
tar -czf "$OUT_DIR/$PORTABLE_STEM.tar.gz" -C "$REPO_ROOT/build" \
    --exclude='renderer_tests' --exclude='renderer_tests.exe' \
    --exclude='desktop-pet-controller-qt-poc*' \
    --exclude='*.pdb' --exclude='*.lib' \
    --transform "s|^bin|$PORTABLE_STEM|" bin
echo "packed: $OUT_DIR/$PORTABLE_STEM.tar.gz"

# ── (b) AppImage ────────────────────────────────────────────────────────────
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/AppDir"
mkdir -p "$APPDIR/usr/bin" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps"

# FLAT layout on purpose: everything from build/bin goes into usr/bin —
# PathResolve assumes appDir == rendererDir (Resources/ etc. as siblings of
# the executables), which is exactly this shape.
(cd "$BIN_DIR" && find . -type f \
    ! -name 'renderer_tests' ! -name 'renderer_tests.exe' \
    ! -name 'desktop-pet-controller-qt-poc*' \
    ! -name '*.pdb' ! -name '*.lib' \
    -print0 | while IFS= read -r -d '' f; do
        mkdir -p "$APPDIR/usr/bin/$(dirname "$f")"
        cp "$f" "$APPDIR/usr/bin/$f"
    done)

# AppDir .desktop = repo base file + Icon key. linuxdeploy REQUIRES an icon;
# the repo file omits Icon because there are no assets yet (known gap —
# BUILD.md 发布 section).
DESKTOP="$APPDIR/usr/share/applications/com.desktoppet.DesktopPet.desktop"
sed '/^Icon=/d' "$REPO_ROOT/packaging/linux/com.desktoppet.DesktopPet.desktop" \
    > "$DESKTOP"
printf 'Icon=DesktopPet\n' >> "$DESKTOP"

# PLACEHOLDER icon — 16x16 transparent PNG, inlined as base64 to keep this
# script self-contained. (linuxdeploy VALIDATES icon resolutions and rejects
# anything smaller than 8x8, so a literal 1x1 is not usable here.)
# 待替换为正式素材（补素材后改为拷贝真实 png 并同步 installer.iss 的
# SetupIconFile / .desktop 的 Icon 字段）。
printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAEklEQVR42mNgGAWjYBSMAggAAAQQAAGvRYgsAAAAAElFTkSuQmCC' \
    | base64 -d > "$APPDIR/usr/share/icons/hicolor/256x256/apps/DesktopPet.png"

# linuxdeploy (cached in /tmp). --appimage-extract-and-run: GitHub runners
# and minimal VMs have no FUSE.
LINUXDEPLOY=/tmp/linuxdeploy-x86_64.AppImage
if [ ! -x "$LINUXDEPLOY" ]; then
    curl -fL --retry 3 -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi
ARCH=x86_64 OUTPUT="$OUT_DIR/DesktopPet-$TAG-x86_64.AppImage" \
    "$LINUXDEPLOY" --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --desktop-file "$DESKTOP" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/DesktopPet.png" \
    --output appimage
echo "packed: $OUT_DIR/DesktopPet-$TAG-x86_64.AppImage"
