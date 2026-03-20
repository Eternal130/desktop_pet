# Desktop Pet — Build

## Environment

| Tool | Path |
|------|------|
| JDK 21 | `C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot` |
| Maven wrapper | `controller/mvnw.cmd` |
| CMake | `C:\Program Files\CMake\bin\cmake.exe` |
| MinGW bin | `C:\Users\Eternal130\Desktop\libs\mingw64\bin` (on user PATH) |
| MinGW Make | `C:\Users\Eternal130\Desktop\libs\mingw64\bin\mingw32-make.exe` |
| CMake build dir | `build/renderer_mingw/` (already configured) |

**CRITICAL — MinGW PATH**: `mingw64\bin` MUST be on PATH for renderer builds. The compiler (`c++.exe`) spawns `cc1plus.exe` which dynamically loads DLLs from `mingw64\bin`. Without it, compilation **silently fails** (exit code 1, zero error output). Already added to user PATH; for safety, renderer build commands below prepend it to session PATH.

Shell quirk: `cmd.exe` swallows output. **ALWAYS** use PowerShell wrapper:
```
powershell -Command "& cmd.exe /c 'call mvnw.cmd <goal>' 2>&1 | Out-String"
```

## Determine What to Build

| Changed files | Default build |
|---------------|---------------|
| `controller/src/**`, `controller/pom.xml` | Controller → fat JAR (`build/bin/desktop-pet-controller.jar`) |
| `renderer/src/**`, `renderer/CMakeLists.txt` | Renderer → EXE (`build/bin/desktop-pet-renderer.exe`) |
| Both | Both targets above |

jpackage (native EXE wrapper) is **NOT** part of the default build. Run it only when explicitly requested.

---

## Controller Build (Java) — fat JAR

workdir: `controller/`

```
powershell -Command "& cmd.exe /c 'call mvnw.cmd package -DskipTests' 2>&1 | Out-String"
```

Output: `build/bin/desktop-pet-controller.jar`

MUST see `BUILD SUCCESS`. If not, fix compilation errors first.

### Controller compile-only (fast check, no packaging)

```
powershell -Command "& cmd.exe /c 'call mvnw.cmd compile' 2>&1 | Out-String"
```

---

## Renderer Build (C++) — EXE

workdir: project root

CMake build directory is pre-configured at `build/renderer_mingw/`. Do NOT re-run `cmake` unless `CMakeLists.txt` changed.

```
powershell -Command "\$env:PATH = 'C:\Users\Eternal130\Desktop\libs\mingw64\bin;' + \$env:PATH; & mingw32-make -C 'build\renderer_mingw' -j8 2>&1 | Out-String"
```

Output: `build/bin/desktop-pet-renderer.exe`

MUST see `[100%] Built target desktop-pet-renderer`. Also copies:
- `build/bin/Live2DCubismCore.dll`
- `build/bin/Resources/` (model assets)
- `build/bin/FrameworkShaders/`

### Clean rebuild (when incremental build fails)

If build fails with exit code 1 but **no compiler error output**, stale obj cache is likely. Clean app objects and rebuild:

workdir: project root

```
powershell -ExecutionPolicy Bypass -Command "\$env:PATH = 'C:\Users\Eternal130\Desktop\libs\mingw64\bin;' + \$env:PATH; Get-ChildItem 'build\renderer_mingw\CMakeFiles\desktop-pet-renderer.dir' -Recurse -Include '*.obj','*.d' | Remove-Item -Force; & mingw32-make -C 'build\renderer_mingw' -j8 2>&1 | Out-String"
```

### Re-configure CMake (only if CMakeLists.txt changed)

workdir: project root

```
powershell -Command "\$env:PATH = 'C:\Users\Eternal130\Desktop\libs\mingw64\bin;' + \$env:PATH; & cmake -G 'MinGW Makefiles' -S renderer -B build/renderer_mingw -DCMAKE_BUILD_TYPE=Release 2>&1 | Out-String"
```

Then run the normal renderer build.

---

## Optional: jpackage → native EXE (on-demand only)

Only run when explicitly requested. Wraps the fat JAR into a native Windows application.

workdir: `controller/`

```
powershell -Command "if (Test-Path '..\build\bin\desktop-pet-controller') { Remove-Item -Recurse -Force '..\build\bin\desktop-pet-controller' }; New-Item -ItemType Directory -Path '..\build\jpackage-input' -Force | Out-Null; Copy-Item '..\build\bin\desktop-pet-controller.jar' '..\build\jpackage-input\'; & 'C:\Program Files\OpenLogic\jdk-21.0.8.9-hotspot\bin\jpackage.exe' --type app-image --input '..\build\jpackage-input' --main-jar desktop-pet-controller.jar --main-class com.desktoppet.Launcher --dest '..\build\bin' --name desktop-pet-controller --app-version 1.0.0 --vendor DesktopPet 2>&1; Remove-Item -Recurse -Force '..\build\jpackage-input'"
```

Output: `build/bin/desktop-pet-controller/desktop-pet-controller.exe`

Requires fat JAR to exist first.

---

## Verify

After build, confirm artifacts in `build/bin/`:

| Artifact | Default build | Path |
|----------|:---:|------|
| Controller JAR | ✅ | `build/bin/desktop-pet-controller.jar` (~11 MB) |
| Renderer EXE | ✅ | `build/bin/desktop-pet-renderer.exe` |
| Cubism DLL | ✅ | `build/bin/Live2DCubismCore.dll` |
| Shaders | ✅ | `build/bin/FrameworkShaders/` |
| Models | ✅ | `build/bin/Resources/` |
| Controller EXE | on-demand | `build/bin/desktop-pet-controller/desktop-pet-controller.exe` |

---

## Troubleshooting

### Renderer: exit code 1, zero error output

**Symptom**: `mingw32-make` reports `Error 1` on `.obj` targets, but no compiler error messages appear.

**Root cause**: `cc1plus.exe` (GCC's actual compiler backend) needs DLLs from `mingw64\bin` (`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll`). When that directory is missing from PATH, Windows terminates the process silently — exit code 1, zero stderr.

**Fix**:
1. Verify `C:\Users\Eternal130\Desktop\libs\mingw64\bin` is on PATH
2. Use the "Clean rebuild" command above to wipe stale `.obj`/`.d` files
3. All renderer commands in this skill prepend PATH defensively
