# Desktop Pet — Build

## Environment

| Tool | Path |
|------|------|
| JDK 21 | `C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot` |
| Maven wrapper | `controller/mvnw.cmd` |
| CMake | `C:\Program Files\CMake\bin\cmake.exe` |
| MinGW bin | `C:\Users\Eternal130\Desktop\libs\mingw64\bin` (on user PATH) |
| Python | `python` (available on PATH) |

**CRITICAL — MinGW PATH**: `mingw64\bin` MUST be on PATH for renderer builds. The compiler (`c++.exe`) spawns `cc1plus.exe` which dynamically loads DLLs from `mingw64\bin`. Without it, compilation **silently fails** (exit code 1, zero error output).

---

## Build Command

workdir: project root

```
python build.py [TARGET...]
```

| Argument | Description |
|----------|-------------|
| (none) | Interactive mode - prompts for target selection |
| `renderer` | Build C++ renderer only |
| `controller` | Build Java controller only |
| `renderer controller` | Build both (space-separated) |
| `all` | Build all targets |

### Examples

```bash
# Interactive mode
python build.py

# Build renderer only
python build.py renderer

# Build controller only
python build.py controller

# Build both
python build.py renderer controller
python build.py all
```

---

## Output Artifacts

After build, confirm artifacts in `build/bin/`:

| Artifact | Path |
|----------|------|
| Controller JAR | `build/bin/desktop-pet-controller.jar` (~11 MB) |
| Renderer EXE | `build/bin/desktop-pet-renderer.exe` |
| Cubism DLL | `build/bin/Live2DCubismCore.dll` |
| Shaders | `build/bin/FrameworkShaders/` |
| Models | `build/bin/Resources/` |

---

## What build.py Does

1. **Renderer (C++)**: 
   - Runs CMake configuration (if needed)
   - Builds with MinGW Makefiles on Windows
   - Auto-downloads GLEW/GLFW if missing

2. **Controller (Java)**:
   - Uses Maven wrapper (`mvnw`) if available
   - Runs `clean package -DskipTests`
   - Copies JAR to `build/bin/`

---

## Troubleshooting

### Renderer: exit code 1, zero error output

**Root cause**: `cc1plus.exe` needs DLLs from `mingw64\bin`. When missing from PATH, Windows terminates silently.

**Fix**:
1. Verify `C:\Users\Eternal130\Desktop\libs\mingw64\bin` is on PATH
2. Re-run `python build.py renderer`

The `build.py` script automatically filters out Git's bundled MinGW to avoid DLL conflicts.

### CMake not found

Install CMake and ensure it's on PATH, or verify `C:\Program Files\CMake\bin\cmake.exe` exists.

### Maven not found

Ensure `controller/mvnw.cmd` exists (Windows) or `controller/mvnw` exists (Unix).
