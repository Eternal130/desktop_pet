# PROJECT KNOWLEDGE BASE

**Generated:** 2026-07-05
**Commit:** 12cef2d
**Branch:** refactor/decouple-opengl-renderer

## OVERVIEW

Live2D desktop pet application. JavaFX control panel (Java 21) orchestrates a C++ rendering engine via WebSocket (JSON protocol, port 9000). Controller is WS Server; renderer is WS Client.

## STRUCTURE

```
desktop_pet/
├── controller/     # JavaFX control panel (Maven, Java 21)
├── renderer/       # Live2D renderer (CMake, C++17, Cubism SDK 5)
├── third_party/    # CubismSdkForNative (Core + Framework + Samples)
├── docs/           # Full architecture docs (Chinese, 30+ .md files)
├── build.py        # Unified build orchestrator (Python 3.6+)
├── BUILD.md        # Build guide
└── build/bin/      # Output: renderer.exe + controller.jar + DLLs + resources
```

## WHERE TO LOOK

| Task | Location | Notes |
|------|----------|-------|
| Add controller UI | `controller/src/main/java/com/desktoppet/ui/` | FXML in `resources/fxml/`, themes in `resources/css/` |
| Add controller business logic | `controller/src/main/java/com/desktoppet/core/` | State, config, scheduling, interaction |
| Add WebSocket protocol message | Both `controller/.../network/Protocol.java` and `renderer/src/network/Protocol.hpp` | Must match Envelope format |
| Add renderer command handler | `renderer/src/network/CommandHandlers.cpp` | Register in `RegisterHandlers()` |
| Add renderer event | `renderer/src/network/EventEmitter.cpp` | Controller handles in `MessageDispatcher.java` |
| Add data model | `controller/src/main/java/com/desktoppet/model/` | Use Java Records |
| Change config format | `docs/system/configuration.md` + `core/*ConfigManager.java` | Config at `~/.config/desktop-pet/` |
| Add graphics feature | `renderer/src/graphics/` | Implement `IGraphicsBackend` for both OpenGL + Vulkan |
| Protocol spec | `docs/protocol/` | commands.md, events.md, handshake.md |
| Architecture overview | `docs/README.md` | Definitive project doc |
| AI learnings/pitfalls | `.sisyphus/notepads/` | Phase-specific dev notes |

## CODE MAP — Key Classes

| Symbol | Lang | Location | Role |
|--------|------|----------|------|
| `App` | Java | `controller/.../App.java` | JavaFX Application entry |
| `Launcher` | Java | `controller/.../Launcher.java` | Fat-JAR entry (JPMS workaround) |
| `MessageDispatcher` | Java | `controller/.../network/MessageDispatcher.java` | Routes WS messages by type+action, CompletableFuture responses |
| `MainWindowController` | Java | `controller/.../ui/MainWindowController.java` | Tab container, lifecycle hub, multi-instance management |
| `ProcessManager` | Java | `controller/.../util/ProcessManager.java` | Renderer process lifecycle (start/stop/restart) |
| `AutoLaunchManager` | Java | `controller/.../util/AutoLaunchManager.java` | OS-specific auto-launch (Windows registry / Linux .desktop) |
| `LAppDelegate` | C++ | `renderer/src/LAppDelegate.hpp` | Engine singleton: lifecycle, main loop, window |
| `LAppLive2DManager` | C++ | `renderer/src/LAppLive2DManager.hpp` | Model load/switch/release |
| `AudioManager` | C++ | `renderer/src/AudioManager.hpp` | miniaudio + libvorbis OGG playback (Phase 3b renderer side) |
| `WebSocketClient` | C++ | `renderer/src/network/WebSocketClient.hpp` | IXWebSocket client wrapper |
| `MessageHandler` | C++ | `renderer/src/network/MessageHandler.hpp` | Message routing by action |

## CONVENTIONS (Non-Standard)

- **LApp\* naming**: C++ renderer follows Cubism SDK's `LApp*` prefix convention — NOT standard C++ naming
- **Dual entry points**: `Launcher.java` (fat-JAR) delegates to `App.java` (JavaFX Application) — JPMS workaround
- **JSON snake_case / Java camelCase**: Config files use `snake_case` keys, Java Records use `camelCase`, Gson maps automatically
- **Protocol Response fields**: `success`, `error_code`, `error_message` at JSON **top level**, NOT inside `payload`
- **Java Records for data**: All data models are Java 21 Records — no POJOs/DTOs
- **Conventional Commits**: `type(scope): description` — scopes: `renderer`, `controller`, `protocol`, `config`
- **Docs in Chinese**: All documentation is Simplified Chinese
- **Renderer = WS Client, Controller = WS Server** (non-intuitive direction)
- **C++ member vars**: `m_camelCase` prefix, `#pragma once` for headers

## ANTI-PATTERNS (THIS PROJECT)

### Forbidden in C++ Renderer
- **NO GL/GLES calls from WebSocket callback thread** — only `glfwPostEmptyEvent()` is thread-safe
- **NO spdlog** — use `LAppPal::PrintLogLn` exclusively
- **NO config files** on renderer side — all config managed by Java controller
- **NO custom reconnect logic** — IXWebSocket handles reconnection
- **NO abstract interfaces** for network layer — concrete classes only

### Forbidden in Java Controller
- **NO `System.out`/`System.err`** — use SLF4J (`log`)
- **NO `e.printStackTrace()`** — use `log.error("msg", e)`
- **NO `Thread.sleep()`** — use `ScheduledExecutorService`
- **NO raw `new Thread()`** — use managed executors
- **NO `@SuppressWarnings`** — fix the root cause
- **NO protobuf generated code in git** — `target/generated-sources/` is gitignored

### Known Pitfalls
- **MinGW PATH**: Git's bundled MinGW conflicts with project MinGW — `build.py` filters it; manual CMake must do the same
- **CMake target_sources**: Adding `.cpp` requires `cmake -S ... -B ...` reconfigure before `cmake --build`
- **Cubism FinishedMotionCallback**: Raw C function pointer — capturing lambdas won't work, use static function + `SetFinishedMotionCustomData(void*)`
- **Mockito**: Use `doReturn().when()` for re-stubbing; `when().thenReturn()` fails for checked-exception methods
- **JavaFX version**: Must use 21.0.5 LTS — do NOT use 25.x (requires JDK 23+)
- **module-info.java**: Empty packages in `opens`/`exports` cause `InvalidModuleDescriptorException` at runtime
- **Monocle**: Must use `io.github.sebivenlo:openjfx-monocle:jdk-21.0.1` (NOT org.testfx)

## COMMANDS

```bash
# Build everything
python build.py                # Interactive
python build.py all            # Both components

# Build individually
python build.py renderer       # C++ renderer (CMake + MinGW/Make)
python build.py controller     # Java controller (Maven)

# Direct Maven
cd controller && mvnw.cmd clean package -DskipTests    # Windows
./mvnw clean package -DskipTests                       # Linux

# Direct CMake (Windows)
cmake -S renderer -B build/renderer_mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/renderer_mingw --config Release -j

# Tests
cd controller && mvnw.cmd test                          # Java tests (headless via Monocle)
cd renderer/build && ctest                              # C++ tests (Google Test)

# Vulkan backend
cmake --build build/renderer_vulkan --config Release -j
```

## NOTES

- `third_party/CubismSdkForNative/` is NOT a git submodule — manually placed
- `renderer/third_party/freetype-gl/` IS a git submodule
- `set_scale` command is stubbed (logs only, no actual scale change)
- VulkanBackend.cpp: **Fully implemented** (Instance → Device → Swapchain → Render → Present, 979 lines, Phase 2.1–2.6 complete). Compile-time switch via `-DUSE_VULKAN=ON`; no runtime switching. See `renderer/AGENTS.md`.
- AudioManager.cpp/hpp: Implemented (miniaudio + libvorbis, OGG playback). `play_audio`/`stop_audio`/`set_volume` commands wired (error codes 7001/7002/7003).
- Dual platform: Ubuntu/X11 (original MVP) + Windows/MinGW (current). `build.py` builds both OpenGL and Vulkan variants.
- Build artifacts: `build/bin/desktop-pet-renderer.exe`, `build/bin/desktop-pet-controller.jar`
- Renderer CLI args: `--port`, `--instance-id`, `--model`, `--x`, `--y`, `--width`, `--height`
