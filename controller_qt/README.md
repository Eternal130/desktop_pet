# controller_qt

Qt 6 / C++17 / QML control panel for the desktop pet — the C++ successor to the
JavaFX `controller/`. This is the foundation scaffold (task T1):
**CMake project + empty QML window + `build.py qt` target**.

> **Scope:** This directory is purely additive. It does **not** touch
> `controller/` (JavaFX) or `renderer/` (C++). The renderer keeps its own
> Google Test; controller_qt uses **QTest only**.

---

## Build

```bash
# from project root
python build.py qt
```

Output: `build/bin/desktop-pet-controller-qt.exe` (Windows) /
`build/bin/desktop-pet-controller-qt` (Linux), next to the renderer.

The `qt` target in `build.py`:
1. Configures CMake into `build/controller_qt` with `CMAKE_PREFIX_PATH` pointing
   at the Qt install.
2. Builds with Qt's bundled toolchain (Windows) or the system toolchain (Linux).
3. (Windows) Runs `windeployqt --qmldir controller_qt/qml` to copy Qt DLLs /
   plugins / QML imports next to the exe so it runs standalone from `build/bin/`.

Run the binary directly:

```bash
./build/bin/desktop-pet-controller-qt.exe   # shows "Desktop Pet (Qt)" window
```

---

## CRITICAL — MinGW toolchain isolation (Windows)

There are **two distinct MinGW runtimes** in play on this project. They must
never be mixed.

| Consumer | MinGW | Path |
|:---|:---|:---|
| `renderer/` (C++ Live2D engine) | project / system MinGW | on `PATH` (filtered) |
| **`controller_qt/` (this project)** | **Qt-bundled MinGW 13.1.0** | **`C:\Qt\Tools\mingw1310_64`** |

Why this matters:

- The renderer and the Qt controller are **separate executables**. Each process
  loads its own app-local MinGW runtime DLLs (`libgcc_s_*`, `libstdc++-*`,
  `libwinpthread-*`). There is **no cross-process ABI mixing** — they are never
  linked into the same binary.
- But at **build time**, `cmake --build` resolves the compiler/linker and the
  `libstdc++`/runtime DLLs from `PATH`. If Git's bundled MinGW
  (`...\Git\mingw64\bin`) or the system MinGW shadows Qt's MinGW, the Qt
  controller gets linked against the wrong `libstdc++` and either fails to link
  or crashes at startup with a runtime-DLL mismatch.

`build.py`'s `qt` target (`_get_qt_env()`) guarantees isolation by:

1. **Prepending** `C:\Qt\Tools\mingw1310_64\bin` to `PATH` (Qt's MinGW wins).
2. **Filtering out** any `\Git\mingw64\bin` from `PATH` (Git's MinGW is removed
   entirely — mirrors the renderer's `_get_renderer_env()`).
3. Prepending Qt's Ninja (`C:\Qt\Tools\ninja`) so the build uses a single,
   unambiguous generator.

> This is the Qt-side analogue of the project-wide pitfall documented in the
> root `AGENTS.md` ("MinGW PATH": Git's bundled MinGW conflicts with the
> project's MinGW) and constraint **R7 (MinGW version conflict)** in
> `docs/controller/development-plan.md`. If you build `controller_qt` manually
> with bare `cmake`, you **must** replicate this PATH filtering yourself — Qt's
  MinGW must be found first.

---

## Layout

```
controller_qt/
├── CMakeLists.txt      # Qt6 find_package, qt_add_executable, qt_add_qml_module
├── src/
│   └── main.cpp        # QGuiApplication + QQmlApplicationEngine entry
├── qml/
│   └── Main.qml        # trivial "Desktop Pet (Qt)" window (T1 placeholder)
└── README.md           # this file
```

`network/`, `core/`, `system/`, `ui/` subpackages and `tests/` arrive in later
tasks (see `docs/controller/development-plan.md` §4.3 for the target structure).
`enable_testing()` is already declared so QTest registration is a one-liner add.
