cmake_minimum_required(VERSION 3.20)

project(bvh LANGUAGES CXX)

# Bullet's vendored CMake project declares a pre-3.5 compatibility version.
# New CMake releases require opting in to their retained policy baseline.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BVH_TARGET_32BIT "Build a 32-bit Sven Co-op MetaMod plugin" ON)

if(MSVC)
    # Sven Co-op loads extensions with the release CRT.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "" FORCE)
endif()

if(BVH_TARGET_32BIT)
    if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Configure with a Win32 generator; Sven Co-op MetaMod plugins are 32-bit.")
    elseif(UNIX)
        add_compile_options(-m32)
        add_link_options(-m32)
    endif()
endif()

# Only build Bullet libraries required by the collision-query world.
set(BUILD_CPU_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET2_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_BULLET3 OFF CACHE BOOL "" FORCE)
set(BUILD_OPENGL3_DEMOS OFF CACHE BOOL "" FORCE)
set(BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PYBULLET OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(USE_GLUT OFF CACHE BOOL "" FORCE)
set(INSTALL_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_FILES OFF CACHE BOOL "" FORCE)

add_subdirectory(metamod/thirdparty/bullet3_fork EXCLUDE_FROM_ALL)

add_library(bvh SHARED
    src/plugin/game_hooks.cpp
    src/plugin/h_export.cpp
    src/plugin/meta_plugin.cpp
    src/physics/collision_world.cpp
    src/runtime/projectile_gate.cpp
)

target_include_directories(bvh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/hlsdk/common
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/hlsdk/dlls
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/hlsdk/pm_shared
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/hlsdk/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/metamod
    ${CMAKE_CURRENT_SOURCE_DIR}/metamod/thirdparty/bullet3_fork/src
)

target_link_libraries(bvh PRIVATE
    BulletCollision
    LinearMath
)

target_compile_definitions(bvh PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32;_WINDOWS;_CRT_SECURE_NO_WARNINGS;NOMINMAX>
    $<$<PLATFORM_ID:Linux>:LINUX;_LINUX>
)

if(MSVC)
    target_compile_options(bvh PRIVATE /W4 /permissive-)
else()
    target_compile_options(bvh PRIVATE -Wall -Wextra -Wpedantic -fPIC)
    target_link_libraries(bvh PRIVATE ${CMAKE_DL_LIBS} m)
endif()

set_target_properties(bvh PROPERTIES
    PREFIX ""
    OUTPUT_NAME "bvh"
)

install(TARGETS bvh
    RUNTIME DESTINATION addons/metamod/dlls
    LIBRARY DESTINATION addons/metamod/dlls
)
