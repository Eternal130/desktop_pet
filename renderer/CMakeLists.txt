cmake_minimum_required(VERSION 3.16)

project(desktop-pet-renderer)

set(APP_NAME desktop-pet-renderer)

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

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../build/bin)
set(CMAKE_CONFIGURATION_TYPES Debug Release CACHE STRING "Configurations" FORCE)
set(CMAKE_SUPPRESS_REGENERATION ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(Live2DCubismCore STATIC IMPORTED)
set_target_properties(Live2DCubismCore
  PROPERTIES
    IMPORTED_LOCATION ${CORE_PATH}/lib/linux/x86_64/libLive2DCubismCore.a
    INTERFACE_INCLUDE_DIRECTORIES ${CORE_PATH}/include
)

add_subdirectory(${GLEW_PATH}/build/cmake ${CMAKE_CURRENT_BINARY_DIR}/glew)
add_subdirectory(${GLFW_PATH} ${CMAKE_CURRENT_BINARY_DIR}/glfw)

set(FRAMEWORK_SOURCE OpenGL)
add_subdirectory(${FRAMEWORK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/Framework)
target_compile_definitions(Framework PUBLIC CSM_TARGET_LINUX_GL)
target_include_directories(Framework PUBLIC ${GLEW_PATH}/include)
target_link_libraries(Framework Live2DCubismCore glew_s)

find_package(OpenGL REQUIRED)

set(USE_TLS OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/ixwebsocket ${CMAKE_CURRENT_BINARY_DIR}/ixwebsocket)

add_executable(${APP_NAME})

target_sources(${APP_NAME}
  PRIVATE
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/MessageHandler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/WebSocketClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/WebSocketClient.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/CommandHandlers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/CommandHandlers.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/EventEmitter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network/EventEmitter.hpp
)

target_include_directories(${APP_NAME}
  PRIVATE
    ${STB_PATH}
    ${COMMON_SRC_PATH}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_link_libraries(${APP_NAME}
  Framework
  glfw
  ${OPENGL_LIBRARIES}
  ixwebsocket::ixwebsocket
)

add_custom_command(
  TARGET ${APP_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${RES_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/Resources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${FRAMEWORK_SHADER_PATH} $<TARGET_FILE_DIR:${APP_NAME}>/FrameworkShaders
)

enable_testing()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest ${CMAKE_CURRENT_BINARY_DIR}/googletest)
add_executable(renderer-tests
  tests/placeholder_test.cpp
  tests/ProtocolTest.cpp
  tests/MessageHandlerTest.cpp
  src/network/Protocol.cpp
  src/network/MessageHandler.cpp
)
target_link_libraries(renderer-tests PRIVATE GTest::gtest_main)
target_include_directories(renderer-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
include(GoogleTest)
gtest_discover_tests(renderer-tests)
