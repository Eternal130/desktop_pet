cmake_minimum_required(VERSION 3.16)

project(desktop-pet-renderer)

set(APP_NAME desktop-pet-renderer)

# === Paths ===
set(SDK_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CubismSdkForNative)
set(CORE_PATH ${SDK_ROOT_PATH}/Core)
set(FRAMEWORK_PATH ${SDK_ROOT_PATH}/Framework)
set(THIRD_PARTY_PATH ${SDK_ROOT_PATH}/Samples/OpenGL/thirdParty)
set(STB_PATH ${THIRD_PARTY_PATH}/stb)
set(GLEW_PATH ${THIRD_PARTY_PATH}/glew)
set(GLFW_PATH ${THIRD_PARTY_PATH}/glfw)
set(RES_PATH ${SDK_ROOT_PATH}/Samples/Resources)
set(FRAMEWORK_SHADER_PATH ${FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard)
set(COMMON_SRC_PATH ${SDK_ROOT_PATH}/Samples/Common)

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

# === GLEW & GLFW ===
add_subdirectory(${GLEW_PATH}/build/cmake ${CMAKE_CURRENT_BINARY_DIR}/glew)
add_subdirectory(${GLFW_PATH} ${CMAKE_CURRENT_BINARY_DIR}/glfw)

# === Cubism Framework ===
set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
target_link_libraries(Framework Live2DCubismCore glew_s)
# GCC rejects PROC-to-void* implicit conversion in SDK's wglGetProcAddress usage
target_compile_options(Framework PRIVATE $<$<CXX_COMPILER_ID:GNU>:-fpermissive>)

# === OpenGL ===
find_package(OpenGL REQUIRED)

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
)

target_include_directories(${APP_NAME}
  PRIVATE
    ${STB_PATH}
    ${COMMON_SRC_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${APP_NAME}
  Framework
  glfw
  ${OPENGL_LIBRARIES}
)

# === Post-build: copy resources, shaders, and DLL ===
add_custom_command(
  TARGET ${APP_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRAMEWORK_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORE_PATH}/dll/windows/x86_64/Live2DCubismCore.dll $<TARGET_FILE_DIR:${APP_NAME}>
)
