cmake_minimum_required(VERSION 3.16)

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

# === Paths (common) ===
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(RES_PATH ${SDK_ROOT_PATH}/Samples/Resources)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/WindowManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/WindowManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/IGraphicsBackend.hpp
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

target_include_directories(${APP_NAME}
  PRIVATE
    ${STB_PATH}
    ${COMMON_SRC_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
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
    )
else()
    target_link_libraries(${APP_NAME}
      Framework
      glfw
      ${OPENGL_LIBRARIES}
      ixwebsocket::ixwebsocket
      vorbisfile
    )
endif()

# === Post-build: copy resources, shaders, and DLL ===
if(USE_VULKAN)
    add_custom_command(
      TARGET ${APP_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/SampleShaders/compiledShaders $<TARGET_FILE_DIR:${APP_NAME}>/SampleShaders
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_BINARY_DIR}/FrameworkShaders/compiledShaders $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll $<TARGET_FILE_DIR:${APP_NAME}>
    )
else()
    add_custom_command(
      TARGET ${APP_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources
      COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRAMEWORK_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll $<TARGET_FILE_DIR:${APP_NAME}>
    )
endif()
