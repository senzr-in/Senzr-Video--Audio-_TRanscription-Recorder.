# Toolchain file for ARM64/aarch64 Linux cross-compilation (e.g. Rockchip RK3588, Orange Pi 5 Max)
# Usage: cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler definitions
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Sysroot target configuration (override with -DSYSROOT_PATH=... if using custom sysroot)
if(DEFINED SYSROOT_PATH)
    set(CMAKE_SYSROOT ${SYSROOT_PATH})
    set(CMAKE_FIND_ROOT_PATH ${SYSROOT_PATH})
endif()

# Search mode settings for cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Platform flag
add_compile_definitions(SENZR_PLATFORM_ROCKCHIP)
message(STATUS "Cross-compiling Senzr SDK for aarch64 (Linux / Rockchip RK3588)")
