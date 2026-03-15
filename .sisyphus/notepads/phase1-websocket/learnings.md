# Learnings — phase1-websocket

## [2026-03-15] Session: ses_31046375bffeo7R0SNKZPM07tb

### Codebase State
- `renderer/CMakeLists.txt` uses C++14 (line 29: `set(CMAKE_CXX_STANDARD 14)`)
- No `renderer/third_party/` directory exists yet
- No `renderer/tests/` directory exists yet
- No `renderer/src/network/` directory exists yet
- Build output: `renderer/build/bin/desktop-pet-renderer/`
- App name: `desktop-pet-renderer`
- SDK at: `../third_party/CubismSdkForNative`

### Key Architecture Points
- `LAppDelegate` is singleton, has `GetWindow()` returning `GLFWwindow*`
- Main loop in `LAppDelegate::Run()` uses `glfwWaitEventsTimeout(1.0/30.0)`
- `glfwPostEmptyEvent()` is thread-safe, can be called from WS callback thread
- WS callbacks run in background thread — NEVER call GL from there
- Queue limit: 1000 messages, max 50 per frame

### CMakeLists.txt Structure
- Line 29: C++ standard (change to 17)
- Lines 86-91: `target_include_directories` — add `third_party` here
- Lines 93-97: `target_link_libraries` — add ixwebsocket here
- `CMAKE_RUNTIME_OUTPUT_DIRECTORY` = `build/bin/${APP_NAME}/`

### Protocol Design
- Envelope: `{ type, action, id, payload, timestamp }`
- Types: command, event, response
- Response: `{ success, error_code, error_message }`
- Error codes: 1001(model not found), 2001(motion group), 2002(index OOB), 2003(expression), 4001(coords), 5003(unknown action)

### Must NOT Violations to Watch
- No spdlog — use `LAppPal::PrintLogLn`
- No TLS — `USE_TLS=OFF`
- No Phase 3 audio commands
- No custom reconnect logic
- No abstract interfaces
- No config files
- No GL calls in WS callback thread (except `glfwPostEmptyEvent`)

## [2026-03-15] Task 1: C++17 Upgrade + nlohmann/json Integration

### Changes Made
- `renderer/CMakeLists.txt` line 29: Changed `CMAKE_CXX_STANDARD` from 14 to 17
- `renderer/CMakeLists.txt` line 91: Added `${CMAKE_CURRENT_SOURCE_DIR}/third_party` to `target_include_directories`
- Downloaded nlohmann/json v3.12.0 to `renderer/third_party/nlohmann/json.hpp` (932K)

### Build Results
✓ Full build succeeded with C++17 standard
✓ Framework compiled without errors (only 1 warning about NULL conversion in Cubism code, not our issue)
✓ All targets built: glew_s, Framework, glfw, desktop-pet-renderer, glew
✓ Binary created: `renderer/build/bin/desktop-pet-renderer/desktop-pet-renderer` (5.2M)

### Key Findings
- Global C++17 setting does NOT break Cubism Framework — it compiles cleanly
- nlohmann/json header-only approach works perfectly with include path setup
- Include directive: `#include <nlohmann/json.hpp>` works as expected
- No per-target compilation features needed — global C++17 is safe

### Evidence Files
- `.sisyphus/evidence/task-1-cpp17-build.txt` — Full build output (105 lines)
- `.sisyphus/evidence/task-1-nlohmann-include.txt` — Include verification

### Commit
- Hash: 71e3ae3
- Message: `build(renderer): upgrade C++ standard to 17, add nlohmann/json`

## Task 2: IXWebSocket 11.4.6 Integration

### Successful Approach
- Downloaded IXWebSocket v11.4.6 from machinezone/IXWebSocket GitHub repository
- Used git clone with --depth 1 and --branch v11.4.6 for efficient download
- Integrated via add_subdirectory() in CMakeLists.txt BEFORE add_executable()

### CMake Configuration Pattern
```cmake
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/ixwebsocket ${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket)
```
- USE_TLS=OFF: Disables OpenSSL/TLS support (not needed for local WebSocket)
- IXWEBSOCKET_INSTALL=OFF: Prevents system installation
- Binary dir separation: Keeps build artifacts in renderer/build/ixwebsocket

### Build Results
- Full build completed successfully (exit code 0)
- ixwebsocket library compiled to libixwebsocket.a
- All 35+ IXWebSocket source files compiled without errors
- Linked successfully with desktop-pet-renderer executable

### Include Path Configuration
- Headers located: renderer/third_party/ixwebsocket/ixwebsocket/
- Include directive: #include <ixwebsocket/IXWebSocket.h>
- Works because renderer/third_party is in target_include_directories
- ixwebsocket::ixwebsocket target provides INTERFACE_INCLUDE_DIRECTORIES

### Key Learnings
1. IXWebSocket CMakeLists.txt respects cache variables set before add_subdirectory()
2. Embedded git repository warning is expected (not using submodule)
3. No additional dependencies needed (zlib already available on system)
4. C++17 standard (set globally in line 29) is compatible with IXWebSocket

### Next Steps
- Ready for WebSocket client/server implementation
- Can now include <ixwebsocket/IXWebSocket.h> in renderer source files

## Task 3: Google Test Infrastructure Setup

### Successful Approach
- Downloaded Google Test v1.17.0 from google/googletest GitHub repository
- Used git clone with --depth 1 and --branch v1.17.0 for efficient download
- Integrated via add_subdirectory() in CMakeLists.txt AFTER add_custom_command()

### CMake Configuration Pattern
```cmake
enable_testing()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)
add_executable(renderer-tests tests/placeholder_test.cpp)
target_link_libraries(renderer-tests PRIVATE GTest::gtest_main)
target_include_directories(renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
include(GoogleTest)
gtest_discover_tests(renderer-tests)
```
- enable_testing(): Enables CTest infrastructure
- add_subdirectory(): Builds googletest as separate target
- renderer-tests: Isolated executable, NO Framework/glfw/OpenGL/ixwebsocket links
- GTest::gtest_main: Provides main() function for tests

### Build Results
- Full build completed successfully (exit code 0)
- Google Test library compiled (gtest, gtest_main, gmock, gmock_main)
- renderer-tests executable created at: renderer/build/bin/desktop-pet-renderer/renderer-tests
- All 1 test passed via ctest

### Test Execution
```
ctest --test-dir renderer/build --output-on-failure
Test project /opt/desktop_pet/renderer/build
    Start 1: PlaceholderTest.AlwaysPasses
1/1 Test #1: PlaceholderTest.AlwaysPasses .....   Passed    0.00 sec
100% tests passed, 0 tests failed out of 1
```

### Dependency Verification
- ldd output shows NO libGL, libGLEW, libglfw dependencies
- Only standard C++ runtime libraries linked:
  - libstdc++.so.6 (C++ standard library)
  - libm.so.6 (math library)
  - libgcc_s.so.1 (GCC runtime)
  - libc.so.6 (C library)
- Confirms test binary is completely isolated from graphics stack

### Key Learnings
1. Google Test CMakeLists.txt respects standard CMake patterns
2. Separate executable target prevents accidental linking of graphics libraries
3. target_include_directories with PRIVATE scope isolates test includes
4. gtest_discover_tests() automatically registers tests with CTest
5. Embedded git repository warning is expected (not using submodule)

### Evidence Files
- `.sisyphus/evidence/task-3-gtest-ctest.txt` — CTest output (1 test passed)
- `.sisyphus/evidence/task-3-no-gl-dep.txt` — ldd output (no GL dependencies)

### Commit
- Hash: 6239752
- Message: `build(renderer): add Google Test infrastructure`

### Next Steps
- Ready for unit test implementation
- Can now write tests for WebSocket protocol, message handling, etc.
- Test infrastructure is completely isolated from graphics/rendering code

## [2026-03-15] Task 4: Protocol Envelope Serialization/Deserialization (TDD)

### TDD Execution
- RED: Added `renderer/tests/ProtocolTest.cpp` first with 11 test cases covering serialize, deserialize error handling, factory helpers, and ID generation.
- GREEN: Implemented `renderer/src/network/Protocol.hpp` and `renderer/src/network/Protocol.cpp` to satisfy tests.
- REFACTOR: Kept module pure C++17 + nlohmann/json, no renderer/GL dependencies.

### Protocol Module Details
- `Envelope` implemented with required fields: `type`, `action`, `id`, `payload`, `timestamp`, plus response fields `success`, `error_code`, `error_message`.
- `serialize()` writes base fields for all messages and response fields only when `type == "response"`.
- `deserialize()` returns `std::nullopt` on malformed input, empty input, and missing required fields.
- `generateId()` uses `<random>` (`std::mt19937_64` + `std::uniform_int_distribution<uint64_t>`), no UUID dependency.
- `createCommand()`, `createEvent()`, `createResponse()` set envelope type and current millisecond timestamp.

### CMake Integration
- Added Protocol sources to app target (`target_sources(${APP_NAME})`):
  - `src/network/Protocol.cpp`
  - `src/network/Protocol.hpp`
- Expanded `renderer-tests` executable sources:
  - `tests/placeholder_test.cpp`
  - `tests/ProtocolTest.cpp`
  - `src/network/Protocol.cpp`

### Verification Results
- Build command passed:
  - `cmake --build renderer/build 2>&1`
- Protocol tests passed:
  - `ctest --test-dir renderer/build --output-on-failure -R "Protocol"`
  - 11/11 Protocol tests passed
- Malformed JSON cases verified safe/no crash:
  - `deserialize("not json") -> nullopt`
  - `deserialize("{}") -> nullopt`
  - `deserialize("") -> nullopt`

### Evidence
- `.sisyphus/evidence/task-4-protocol-tests.txt` contains build + ctest outputs.
