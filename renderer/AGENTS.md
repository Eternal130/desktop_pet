# Renderer — C++ Live2D Engine

C++17 rendering engine using Live2D Cubism SDK 5. Displays animated pet models in a transparent, topmost, frameless window. Connects to the Qt control panel (`controller_qt/`) via WebSocket.

## STRUCTURE

```
renderer/
├── CMakeLists.txt              # CMake (C++17, optional Vulkan backend)
├── src/
│   ├── main.cpp                # Entry: parses CLI args, inits LAppDelegate
│   ├── LAppDelegate.cpp/hpp    # Singleton: lifecycle, main loop, GLFW window
│   ├── LAppLive2DManager.cpp/hpp  # Model load/switch/release
│   ├── LAppModel.cpp/hpp       # Single model wrapper (extends CubismUserModel)
│   ├── LAppView.cpp/hpp        # Coordinate transforms, hit testing, render dispatch
│   ├── LAppPal.cpp/hpp         # Platform abstraction: file I/O, time, logging
│   ├── LAppDefine.cpp/hpp      # Global constants (paths, ports, defaults)
│   ├── LAppTextureManager.cpp/hpp  # stb_image → texture loading (GL via Backend, VK via CubismImageVulkan)
│   ├── AudioManager.cpp/hpp    # miniaudio + libvorbis audio playback
│   ├── network/                # WebSocket client + JSON protocol
│   │   ├── WebSocketClient.cpp/hpp  # IXWebSocket wrapper
│   │   ├── MessageHandler.cpp/hpp   # Routes incoming by action
│   │   ├── CommandHandlers.cpp/hpp  # Handles load_model, play_motion, etc.
│   │   ├── EventEmitter.cpp/hpp     # Sends hit, drag, model_loaded events
│   │   └── Protocol.cpp/hpp         # Envelope serialize/deserialize (nlohmann/json)
│   ├── graphics/               # Backend abstraction (compile-time switch via USE_VULKAN)
│   │   ├── IGraphicsBackend.hpp     # Interface (6 virtual methods)
│   │   ├── OpenGLBackend.cpp/hpp    # OpenGL implementation
│   │   └── VulkanBackend.cpp/hpp    # Vulkan implementation (VulkanManager + SwapchainManager merged)
│   └── platform/
│       └── WindowManager.cpp/hpp    # GLFW window management (shared by GL and Vulkan)
├── scripts/                    # build_mingw.bat, setup_thirdparty.bat
├── tests/                      # Google Test
│   ├── ProtocolTest.cpp        # 11 cases
│   ├── MessageHandlerTest.cpp  # 6 cases
│   └── integration/            # websocket_smoke_test.sh
└── third_party/                # Vendored headers
    ├── nlohmann/               # JSON (header-only)
    ├── googletest/             # Google Test
    └── miniaudio.h             # Audio playback (single header)
```

## WHERE TO LOOK

| Task | File(s) | Notes |
|------|---------|-------|
| Add command handler | `network/CommandHandlers.cpp` | Register in `RegisterHandlers()`, match C++ `Protocol.hpp` |
| Add event emission | `network/EventEmitter.cpp` | Match `controller_qt` InstanceSession (Handlers TU) handling |
| Change protocol format | `network/Protocol.hpp/cpp` + `controller_qt/src/network/Protocol.hpp` | Both sides must match |
| Add graphics feature | `graphics/` | Implement `IGraphicsBackend` for both backends; Vulkan path also touches `LAppView`, `LAppModel`, `LAppLive2DManager` via `#ifdef USE_VULKAN` |
| Change model rendering | `LAppModel.cpp`, `LAppView.cpp` | Cubism SDK integration point |
| Change window behavior | `platform/WindowManager.cpp`, `LAppDelegate.cpp` | GLFW window management; WindowManager shared by both GL and Vulkan |
| Add audio feature | `AudioManager.cpp/hpp` | miniaudio + libvorbis (OGG playback) |
| Change hit detection | `LAppView.cpp` | Coordinate transform + Cubism hit test |
| Change logging | `LAppPal.cpp` | `LAppPal::PrintLogLn` is the only logging API |

## KEY DEPENDENCIES

| Dep | Version | Source |
|-----|---------|--------|
| Cubism SDK (Core+Framework) | 5-r.5-beta.3.1 | Git submodule (`Live2D/CubismNativeSamples` @ 5-r.5-beta.3.1); Core binaries fetched via `scripts/fetch_cubism_core.sh\|.bat` |
| GLFW | 3.4 | Auto-downloaded by `build.py` |
| GLEW | 2.2.0 | Auto-downloaded (OpenGL only) |
| Vulkan SDK | system install | `find_package(Vulkan REQUIRED)` (Vulkan only) |
| IXWebSocket | v11.4.5 | CMake FetchContent |
| nlohmann/json | 3.12.0 | Vendored header |
| miniaudio | single-header | Vendored |
| libogg + libvorbis | 1.3.5 / 1.3.7 | CMake FetchContent |

## CONVENTIONS

- **LApp\* naming**: All source files follow Cubism SDK sample convention (`LAppDelegate`, `LAppModel`, etc.)
- **Singletons**: `LAppDelegate` and `LAppLive2DManager` are singletons via `GetInstance()`
- **Member vars**: `m_camelCase` prefix
- **Headers**: `#pragma once` (not `#ifndef` guards)
- **Logging**: Only `LAppPal::PrintLogLn()` — no `printf`, no `std::cout`, no `spdlog`
- **No config files**: All configuration comes from the Qt control panel via WebSocket commands
- **JSON keys**: `snake_case` in protocol, matching the Envelope spec in `docs/protocol/`
- **Tests**: Google Test, separate from main executable (no GL/GLFW/Framework linking)
- **Backend selection**: Compile-time via `USE_VULKAN` cmake option — no runtime switching. `CUBISM_RENDERER_TYPE` macro selects CubismRenderer subclass

## ANTI-PATTERNS

- **NO GL/GLES calls from WebSocket callback thread** — only `glfwPostEmptyEvent()` is safe to call from non-main thread
- **NO spdlog** — use `LAppPal::PrintLogLn` exclusively
- **NO config files on disk** — renderer receives all config via WS commands from controller
- **NO custom reconnect logic** — IXWebSocket handles reconnection internally
- **NO abstract interfaces for network layer** — concrete classes only
- **NO capturing lambdas for Cubism callbacks** — `FinishedMotionCallback` is a raw C function pointer; use static function + `SetFinishedMotionCustomData(void*)`

## CLI ARGUMENTS

```
desktop-pet-renderer --port 9001 --instance-id 0 --token <hex> --model Hiyori --x 100 --y 200 --width 400 --height 500
```

| Arg | Default | Notes |
|-----|---------|-------|
| `--port` | 9001 | WebSocket server port to connect to |
| `--instance-id` | 0 | Instance ID for WS URL query param |
| `--token` | (none) | Per-process auth token for WS connection validation |
| `--model` | (none) | Startup model name (e.g., "Hiyori") |
| `--x`, `--y` | -1 | Window position (-1 = default) |
| `--width`, `--height` | -1 | Window size (-1 = default) |

## BUILD

```bash
# Via build.py (recommended)
python build.py renderer

# Direct CMake (Windows)
cmake -S renderer -B build/renderer_mingw -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/renderer_mingw --config Release -j

# Vulkan backend
cmake -S renderer -B build/renderer_vulkan -G "MinGW Makefiles" -DUSE_VULKAN=ON -DCMAKE_BUILD_TYPE=Release

# Tests
cd build/renderer_mingw && ctest
```

## PITFALLS

- **MinGW PATH**: Git's bundled MinGW conflicts — `build.py` auto-filters; manual CMake must do the same
- **CMake target_sources**: Adding new `.cpp` files requires `cmake -S ... -B ...` reconfigure
- **`-fpermissive`**: Enabled for Cubism Framework target (GCC rejects SDK's `wglGetProcAddress` PROC→void*)
- **`set_scale` stub**: Command registered but only logs, no actual scale change
- **VulkanBackend**: Fully implemented (Instance → Device → Swapchain → Render → Present pipeline). Uses dynamic rendering (`vkCmdBeginRendering`), no RenderPass/Framebuffer. Pixel readback for click-through via `vkCmdCopyImageToBuffer`.
- **IXWebSocket**: Bundled version lacks `Reconnecting` message type (only Open/Close/Error/Ping/Pong/Fragment)
