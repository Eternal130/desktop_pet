cmake_minimum_required(VERSION 3.16)

# Allow target_link_libraries on targets not built in this directory (Live2DCubismCore)
cmake_policy(SET CMP0079 NEW)

project(desktop-pet-renderer)

# Compatibility: CMake 4.x requires minimum policy version 3.5.
# Dependencies (GLEW 2.8.12, GLFW 3.4, IXWebSocket, libogg, libvorbis)
# declare older cmake_minimum_required versions. Setting this variable
# before any FetchContent/add_subdirectory call tells CMake to treat
# all sub-projects as if they require at least 3.5, suppressing the
# deprecation error.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(APP_NAME desktop-pet-renderer)

# === Backend selection ===
option(USE_VULKAN "Use Vulkan renderer instead of OpenGL" OFF)

# === Resource monitor ===
option(ENABLE_GPU_MONITOR "Enable GPU/VRAM monitoring (PDH+DXGI on Windows, stub on Linux)" ON)

# === Paths (common) ===
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(RES_PATH ${SDK_ROOT_PATH}/Samples/Resources)
set(FONTS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts)
set(COMMON_SRC_PATH ${SDK_ROOT_PATH}/Samples/Common)

# === Paths (backend-specific) ===
if(USE_VULKAN)
    set(THIRD_PARTY_PATH ${SDK_ROOT_PATH}/Samples/Vulkan/thirdParty)
    set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/Vulkan/Shaders)
    set(SAMPLE_SHADER_PATH ${SDK_ROOT_PATH}/Samples/Vulkan/Shaders)
else()
    set(THIRD_PARTY_PATH ${SDK_ROOT_PATH}/Samples/OpenGL/thirdParty)
    set(GLEW_PATH ${THIRD_PARTY_PATH}/glew)
    set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard)
endif()
set(STB_PATH ${THIRD_PARTY_PATH}/stb)
set(GLFW_PATH ${THIRD_PARTY_PATH}/glfw)

# === Output ===
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../build/bin)
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "Configurations" FORCE)
set(CMAKE_SUPPRESS_REGENERATION ON)

# === Third-party build options ===
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)

# === IXWebSocket (via FetchContent) ===
include(FetchContent)
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v11.4.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# === libogg + libvorbis (official OGG Vorbis decoder) ===
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  libogg
  GIT_REPOSITORY https://github.com/xiph/ogg.git
  GIT_TAG v1.3.5
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libogg)

set(OGG_INCLUDE_DIR ${libogg_SOURCE_DIR}/include ${libogg_BINARY_DIR}/include)
set(OGG_LIBRARY ogg)
FetchContent_Declare(
  libvorbis
  GIT_REPOSITORY https://github.com/xiph/vorbis.git
  GIT_TAG v1.3.7
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libvorbis)

# === FreeType (for libass subtitle rendering) ===
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/FreeType/freetype.git
    GIT_TAG VER-2-13-3
    GIT_SHALLOW TRUE
)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(freetype)

# === HarfBuzz (for libass subtitle rendering) ===
FetchContent_Declare(harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 10.4.0
    GIT_SHALLOW TRUE
)
set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(harfbuzz)

# === FriBidi (for libass subtitle rendering) ===
add_subdirectory(third_party/fribidi)

# === libass (subtitle rendering engine) ===
# libass ships with Meson/Autotools only — no CMakeLists.txt.
# FetchContent the source, then build via our vendored CMakeLists.txt in
# third_party/libass-cmake/. That CMakeLists.txt locates the fetched source
# files through the libass_src_SOURCE_DIR variable (propagated to
# subdirectories by CMake's variable scope). No file(COPY) needed — avoids
# FetchContent auto-detecting a stale CMakeLists.txt on reconfigure.
FetchContent_Declare(libass_src
    GIT_REPOSITORY https://github.com/libass/libass.git
    GIT_TAG 0.17.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(libass_src)

add_subdirectory(third_party/libass-cmake ${CMAKE_BINARY_DIR}/libass-build)


# === Compiler settings ===
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# === Cubism Core (DLL for MinGW) ===
# MinGW cannot link MSVC static .lib files.
# Use the DLL + its import library from Core/dll/windows/x86_64/.
# MinGW's linker can handle MSVC import libraries for pure C DLLs.
add_library(Live2DCubismCore SHARED IMPORTED)
set_target_properties(Live2DCubismCore PROPERTIES
  IMPORTED_IMPLIB ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.lib
  IMPORTED_LOCATION ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll
  INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
)

# === GLEW (OpenGL only) & GLFW ===
if(NOT USE_VULKAN)
    add_subdirectory(${GLEW_PATH}/build/cmake ${CMAKE_CURRENT_BINARY_DIR}/glew)
endif()
add_subdirectory(${GLFW_PATH} ${CMAKE_CURRENT_BINARY_DIR}/glfw)

# === Graphics library ===
if(USE_VULKAN)
    find_package(Vulkan REQUIRED)
else()
    find_package(OpenGL REQUIRED)
endif()

# === Cubism Framework ===
if(USE_VULKAN)
    set(FRAMEWORK_SOURCE Vulkan)
else()
    set(FRAMEWORK_SOURCE OpenGL)
endif()
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)

if(USE_VULKAN)
    target_include_directories(Framework PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(Framework Live2DCubismCore ${Vulkan_LIBRARIES})
else()
    target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
    target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
    target_link_libraries(Framework Live2DCubismCore glew_s)
    # GCC rejects PROC-to-void* implicit conversion in SDK's wglGetProcAddress usage
    target_compile_options(Framework PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fpermissive>)
endif()

# === Vulkan Shader Compilation ===
if(USE_VULKAN)
    add_subdirectory(${SAMPLE_SHADER_PATH} ${CMAKE_CURRENT_BINARY_DIR}/SampleShaders)
    add_subdirectory(${FRAMEWORK_SHADER_PATH} ${CMAKE_CURRENT_BINARY_DIR}/FrameworkShaders)
    add_dependencies(Framework SampleShaders)
    add_dependencies(Framework FrameworkShaders)
endif()

# === Main executable ===
add_executable(${APP_NAME})

target_sources(${APP_NAME}
  PRIVATE
    # Common sources from Cubism SDK
    ${COMMON_SRC_PATH}/LAppAllocator_Common.cpp
    ${COMMON_SRC_PATH}/LAppAllocator_Common.hpp
    ${COMMON_SRC_PATH}/LAppModel_Common.cpp
    ${COMMON_SRC_PATH}/LAppModel_Common.hpp
    ${COMMON_SRC_PATH}/LAppTextureManager_Common.cpp
    ${COMMON_SRC_PATH}/LAppTextureManager_Common.hpp
    ${COMMON_SRC_PATH}/LAppView_Common.cpp
    ${COMMON_SRC_PATH}/LAppView_Common.hpp
    ${COMMON_SRC_PATH}/LAppSprite_Common.cpp
    ${COMMON_SRC_PATH}/LAppSprite_Common.hpp
    ${COMMON_SRC_PATH}/LAppWavFileHandler_Common.cpp
    ${COMMON_SRC_PATH}/LAppWavFileHandler_Common.hpp
    ${COMMON_SRC_PATH}/TouchManager_Common.cpp
    ${COMMON_SRC_PATH}/TouchManager_Common.hpp
    # App sources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppDefine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppDefine.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppDelegate.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppLive2DManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppLive2DManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppModel.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppPal.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppPal.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppTextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppTextureManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LAppView.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Protocol.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Protocol.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/WebSocketClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/WebSocketClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/EventEmitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/EventEmitter.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/CommandHandlers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/CommandHandlers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AudioManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AudioManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subtitle/SubtitleManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/subtitle/SubtitleManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/WindowManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/IGraphicsBackend.hpp
    # Resource monitor (always compiled; platform backend selected below)
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/IGpuMonitor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/ProcessStatsCollector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/ProcessStatsCollector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/StatsPayload.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/StatsPayload.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/GpuMonitorFactory.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/GpuMonitorFactory.cpp
)

# Backend-specific source files
if(USE_VULKAN)
    target_sources(${APP_NAME}
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/VulkanBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/VulkanBackend.hpp
    )
    target_compile_definitions(${APP_NAME} PRIVATE USE_VULKAN)
    set_target_properties(${APP_NAME} PROPERTIES OUTPUT_NAME "desktop-pet-renderer-vulkan")
else()
    target_sources(${APP_NAME}
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/OpenGLBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/OpenGLBackend.hpp
    )
    target_compile_definitions(${APP_NAME} PRIVATE USE_OPENGL)
endif()

# ── GPU monitor platform backend ───────────────────────────────────
if(ENABLE_GPU_MONITOR)
    if(WIN32)
        target_sources(${APP_NAME}
          PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/win/PdhGpuMonitor.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/win/PdhGpuMonitor.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/win/DxgiVramMonitor.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/win/DxgiVramMonitor.cpp
        )
        target_compile_definitions(${APP_NAME} PRIVATE GPU_MONITOR_WIN)
    elseif(UNIX)
        target_sources(${APP_NAME}
          PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/linux/StubGpuMonitor.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/linux/StubGpuMonitor.cpp
        )
        target_compile_definitions(${APP_NAME} PRIVATE GPU_MONITOR_LINUX)
    endif()
endif()

target_include_directories(${APP_NAME}
  PRIVATE
    ${STB_PATH}
    ${COMMON_SRC_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${libass_src_SOURCE_DIR}/libass
)

if(USE_VULKAN)
    target_include_directories(${APP_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
endif()

# === Link libraries ===
if(USE_VULKAN)
    target_link_libraries(${APP_NAME}
      Framework
      glfw
      ${Vulkan_LIBRARIES}
      ixwebsocket::ixwebsocket
      vorbisfile
      ass freetype harfbuzz fribidi
    )
else()
    target_link_libraries(${APP_NAME}
      Framework
      glfw
      ${OPENGL_LIBRARIES}
      ixwebsocket::ixwebsocket
      vorbisfile
      ass freetype harfbuzz fribidi
    )
endif()

# Windows-only system libs required by the GPU monitor and process stats
# collector (pdh: GPU%; psapi: WorkingSetSize; dxgi+dxguid: VRAM + adapter).
if(WIN32)
    target_link_libraries(${APP_NAME}
      psapi
      pdh
      dxgi
      dxguid
      gdi32
      dwrite
    )
endif()

# === Post-build: copy resources, shaders, and DLL ===
if(USE_VULKAN)
    add_custom_command(
      TARGET ${APP_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources/Models
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/SampleShaders/compiledShaders $<TARGET_FILE_DIR:${APP_NAME}>/SampleShaders
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/FrameworkShaders/compiledShaders $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${FONTS_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/fonts
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll $<TARGET_FILE_DIR:${APP_NAME}>
    )
else()
    add_custom_command(
      TARGET ${APP_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources/Models
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRAMEWORK_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${FONTS_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/fonts
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll $<TARGET_FILE_DIR:${APP_NAME}>
    )
endif()

# === Tests (Google Test via FetchContent) ==========================
option(BUILD_TESTS "Build the renderer_tests target and register with CTest" ON)
if(BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
      googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG v1.14.0
      GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_rt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(renderer_tests
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_stats_payload.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/ProtocolTest.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/MessageHandlerTest.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/SubtitleColorTest.cpp
      # Implementation files needed by the tests:
      ${CMAKE_CURRENT_SOURCE_DIR}/src/monitor/StatsPayload.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/network/Protocol.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageHandler.cpp
    )

    target_include_directories(renderer_tests
      PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )

    target_link_libraries(renderer_tests PRIVATE GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(renderer_tests)
endif()
