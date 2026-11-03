cmake_minimum_required(VERSION 3.28)
project(BrickTraceNative VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(Vulkan REQUIRED)

add_library(bricktrace_core STATIC
    src/core/device_probe.cpp
    src/world/voxel_world.cpp
    src/util/png_writer.cpp
    src/renderer/rt_renderer.cpp
)
target_include_directories(bricktrace_core PUBLIC include)
target_link_libraries(bricktrace_core PUBLIC Vulkan::Vulkan)

add_executable(bricktrace-vkprobe tools/vkprobe/main.cpp)
target_link_libraries(bricktrace-vkprobe PRIVATE bricktrace_core)

add_executable(bricktrace-rtbench tools/rtbench/main.cpp)
target_link_libraries(bricktrace-rtbench PRIVATE bricktrace_core)

# JNI bridge used by the Fabric mod. Set JAVA_HOME to a JDK 25 install.
if(DEFINED ENV{JAVA_HOME})
    add_library(bricktrace_jni SHARED src/jni/bricktrace_jni.cpp)
    target_include_directories(bricktrace_jni PRIVATE "$ENV{JAVA_HOME}/include"
                                                      "$ENV{JAVA_HOME}/include/win32")
    target_link_libraries(bricktrace_jni PRIVATE bricktrace_core)
else()
    message(STATUS "JAVA_HOME not set; skipping the JNI bridge target")
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../shaders" "${CMAKE_BINARY_DIR}/shaders-cmake")
