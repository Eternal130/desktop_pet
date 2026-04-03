#!/usr/bin/env python3
"""Desktop Pet - Cross-platform build script.

Usage:
    python build.py                        # Interactive mode
    python build.py renderer               # Build renderer only
    python build.py controller             # Build controller only
    python build.py renderer controller    # Build both
    python build.py all                    # Build all

Environment requirements documented in BUILD.md.
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

# ── Paths ──────────────────────────────────────────────────────────

PROJECT_ROOT = Path(__file__).resolve().parent
RENDERER_DIR = PROJECT_ROOT / "renderer"
CONTROLLER_DIR = PROJECT_ROOT / "controller"
BUILD_DIR = PROJECT_ROOT / "build"
BIN_DIR = BUILD_DIR / "bin"
THIRD_PARTY_DIR = (
    PROJECT_ROOT
    / "third_party"
    / "CubismSdkForNative"
    / "Samples"
    / "OpenGL"
    / "thirdParty"
)

# ── Constants ──────────────────────────────────────────────────────

GLEW_VERSION = "2.2.0"
GLFW_VERSION = "3.4"
IS_WINDOWS = platform.system() == "Windows"


# ── Output helpers ─────────────────────────────────────────────────

def _supports_color():
    if not hasattr(sys.stdout, "isatty") or not sys.stdout.isatty():
        return False
    if IS_WINDOWS:
        try:
            import ctypes
            kernel32 = ctypes.windll.kernel32
            kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
            return True
        except Exception:
            return False
    return True


_USE_COLOR = _supports_color()


def _colored(text, code):
    if not _USE_COLOR:
        return text
    return f"\033[{code}m{text}\033[0m"


def info(msg):
    print(_colored(f"[INFO] ", "36") + msg)


def success(msg):
    print(_colored(f"[OK]   ", "32") + msg)


def warn(msg):
    print(_colored(f"[WARN] ", "33") + msg)


def error(msg):
    print(_colored(f"[ERR]  ", "31") + msg, file=sys.stderr)


def header(msg):
    bar = "=" * 50
    print()
    print(_colored(bar, "1"))
    print(_colored(f"  {msg}", "1"))
    print(_colored(bar, "1"))


# ── Utility ────────────────────────────────────────────────────────

def run(cmd, cwd=None, env=None):
    display = " ".join(str(c) for c in cmd)
    info(f"$ {display}")
    return subprocess.run(cmd, cwd=cwd, env=env).returncode


def check_tool(name):
    return shutil.which(name) is not None


def download_and_extract(url, dest_dir, strip_prefix, final_name):
    zip_path = dest_dir / "_download.zip"
    info(f"Downloading {url}")

    try:
        urllib.request.urlretrieve(url, str(zip_path))
    except Exception as e:
        error(f"Download failed: {e}")
        if zip_path.exists():
            zip_path.unlink()
        return False

    info("Extracting...")
    try:
        with zipfile.ZipFile(str(zip_path), "r") as zf:
            zf.extractall(str(dest_dir))
    except Exception as e:
        error(f"Extraction failed: {e}")
        if zip_path.exists():
            zip_path.unlink()
        return False

    zip_path.unlink()

    src = dest_dir / strip_prefix
    dst = dest_dir / final_name
    if src.exists() and not dst.exists():
        src.rename(dst)

    return True


# ── Third-party setup ─────────────────────────────────────────────

def setup_thirdparty():
    info("Checking third-party dependencies...")

    if not THIRD_PARTY_DIR.exists():
        error(f"Third-party directory not found: {THIRD_PARTY_DIR}")
        error("Ensure the Cubism SDK is placed under third_party/.")
        return False

    ok = True

    glew_dir = THIRD_PARTY_DIR / "glew"
    if glew_dir.exists():
        info("GLEW already present, skipping.")
    else:
        info(f"Setting up GLEW {GLEW_VERSION}...")
        url = (
            f"https://github.com/nigels-com/glew/releases/download/"
            f"glew-{GLEW_VERSION}/glew-{GLEW_VERSION}.zip"
        )
        if not download_and_extract(url, THIRD_PARTY_DIR, f"glew-{GLEW_VERSION}", "glew"):
            ok = False

    glfw_dir = THIRD_PARTY_DIR / "glfw"
    if glfw_dir.exists():
        info("GLFW already present, skipping.")
    else:
        info(f"Setting up GLFW {GLFW_VERSION}...")
        url = (
            f"https://github.com/glfw/glfw/releases/download/"
            f"{GLFW_VERSION}/glfw-{GLFW_VERSION}.zip"
        )
        if not download_and_extract(url, THIRD_PARTY_DIR, f"glfw-{GLFW_VERSION}", "glfw"):
            ok = False

    if ok:
        success("Third-party dependencies ready.")
    return ok


# ── Build: Renderer ───────────────────────────────────────────────

def _get_renderer_env():
    """Build env for renderer subprocess.

    On Windows, removes Git's bundled MinGW from PATH to avoid
    libwinpthread DLL conflicts with the project's own MinGW.
    """
    env = os.environ.copy()
    if IS_WINDOWS:
        sep = ";"
        path_dirs = env.get("PATH", "").split(sep)
        filtered = [d for d in path_dirs if "\\Git\\mingw64\\bin" not in d]
        env["PATH"] = sep.join(filtered)
    return env


def _build_renderer_variant(use_vulkan: bool, env, generator):
    """Build one renderer variant (OpenGL or Vulkan).

    Returns True on success, False on failure.
    """
    if use_vulkan:
        if IS_WINDOWS:
            build_subdir = BUILD_DIR / "renderer_vulkan_mingw"
        else:
            build_subdir = BUILD_DIR / "renderer_vulkan_build"
        label = "Vulkan"
        cmake_args = ["-DUSE_VULKAN=ON"]
    else:
        if IS_WINDOWS:
            build_subdir = BUILD_DIR / "renderer_mingw"
        else:
            build_subdir = BUILD_DIR / "renderer_build"
        label = "OpenGL"
        cmake_args = []

    info(f"Configuring CMake ({label})...")
    rc = run(
        [
            "cmake",
            "-S", str(RENDERER_DIR),
            "-B", str(build_subdir),
            "-G", generator,
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        ] + cmake_args,
        env=env,
    )
    if rc != 0:
        error(f"CMake configuration failed ({label}).")
        return False

    nproc = os.cpu_count() or 4
    info(f"Building {label} renderer with {nproc} parallel jobs...")
    rc = run(
        [
            "cmake",
            "--build", str(build_subdir),
            "--config", "Release",
            f"-j{nproc}",
        ],
        env=env,
    )
    if rc != 0:
        return False

    suffix = ".exe" if IS_WINDOWS else ""
    if use_vulkan:
        success(f"{label} renderer built → build/bin/desktop-pet-renderer-vulkan{suffix}")
    else:
        success(f"{label} renderer built → build/bin/desktop-pet-renderer{suffix}")
    return True


def build_renderer():
    header("Building: Renderer (C++ / CMake)")

    if not check_tool("cmake"):
        error("cmake not found on PATH. Install CMake and add it to PATH.")
        return False

    if IS_WINDOWS:
        generator = "MinGW Makefiles"
        if not check_tool("mingw32-make"):
            error("mingw32-make not found on PATH. Install MinGW-w64 and add its bin/ to PATH.")
            return False
    else:
        if check_tool("ninja"):
            generator = "Ninja"
        else:
            generator = "Unix Makefiles"

    if not setup_thirdparty():
        return False

    env = _get_renderer_env()

    # Build OpenGL (required)
    if not _build_renderer_variant(use_vulkan=False, env=env, generator=generator):
        error("OpenGL renderer build failed.")
        return False

    # Build Vulkan (optional)
    if not _build_renderer_variant(use_vulkan=True, env=env, generator=generator):
        warn("Vulkan renderer build failed. OpenGL renderer is still available.")

    return True


# ── Build: Controller ─────────────────────────────────────────────

def _find_maven():
    if IS_WINDOWS:
        mvnw = CONTROLLER_DIR / "mvnw.cmd"
        if mvnw.exists():
            return [str(mvnw)]
    else:
        mvnw = CONTROLLER_DIR / "mvnw"
        if mvnw.exists():
            if not os.access(str(mvnw), os.X_OK):
                os.chmod(str(mvnw), 0o755)
            return [str(mvnw)]

    if check_tool("mvn"):
        return ["mvn"]

    return None


def build_controller():
    header("Building: Controller (Java / Maven)")

    mvn = _find_maven()
    if mvn is None:
        error("Maven not found. Install Maven and add to PATH, or ensure mvnw exists in controller/.")
        return False

    info(f"Using: {' '.join(mvn)}")

    rc = run(
        mvn + [
            "clean", "package",
            "-f", str(CONTROLLER_DIR / "pom.xml"),
            "-DskipTests",
            "-q",
        ],
    )
    if rc != 0:
        error("Controller build failed.")
        return False

    success("Controller built → build/bin/desktop-pet-controller.jar")
    return True


# ── Interactive mode ───────────────────────────────────────────────

TARGETS = [
    ("renderer",   "C++ 渲染引擎 (desktop-pet-renderer)"),
    ("controller", "Java 控制面板 (desktop-pet-controller.jar)"),
]


def interactive_select():
    print()
    print(_colored("Desktop Pet Build Script", "1"))
    print("=" * 40)
    print()
    for i, (name, desc) in enumerate(TARGETS, 1):
        print(f"  [{i}] {name:12s} - {desc}")
    print(f"  [{len(TARGETS) + 1}] {'all':12s} - 全部构建")
    print()

    while True:
        try:
            raw = input("请选择构建目标 (例: 1,2 或 1 2): ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(0)

        if not raw:
            continue

        parts = raw.replace(",", " ").split()
        selected = []
        valid = True

        for p in parts:
            p = p.strip()
            if p.isdigit():
                idx = int(p)
                if idx == len(TARGETS) + 1:
                    return [name for name, _ in TARGETS]
                if 1 <= idx <= len(TARGETS):
                    selected.append(TARGETS[idx - 1][0])
                else:
                    error(f"Invalid option: {p}")
                    valid = False
                    break
            elif p in ("renderer", "controller"):
                selected.append(p)
            elif p == "all":
                return [name for name, _ in TARGETS]
            else:
                error(f"Invalid option: {p}")
                valid = False
                break

        if valid and selected:
            return list(dict.fromkeys(selected))


# ── Main ───────────────────────────────────────────────────────────

BUILDERS = {
    "renderer": build_renderer,
    "controller": build_controller,
}


def main():
    parser = argparse.ArgumentParser(
        description="Desktop Pet - Cross-platform build script",
        epilog="Run without arguments for interactive mode.",
    )
    parser.add_argument(
        "targets",
        nargs="*",
        choices=["renderer", "controller", "all"],
        metavar="TARGET",
        help="renderer | controller | all",
    )
    args = parser.parse_args()

    if args.targets:
        targets = args.targets
        if "all" in targets:
            targets = [name for name, _ in TARGETS]
    else:
        targets = interactive_select()

    targets = list(dict.fromkeys(targets))

    results = {}
    for target in targets:
        results[target] = BUILDERS[target]()

    header("Build Summary")
    all_ok = True
    for target, ok in results.items():
        status = _colored("SUCCESS", "32") if ok else _colored("FAILED", "31")
        print(f"  {target:20s} {status}")
        if not ok:
            all_ok = False

    print()
    info(f"Output directory: {BIN_DIR}")

    if not all_ok:
        sys.exit(1)


if __name__ == "__main__":
    main()
