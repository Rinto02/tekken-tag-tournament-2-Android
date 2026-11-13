cmake_minimum_required(VERSION 3.20)
project(ttt2_android CXX)

# Android / POSIX build of the Tekken Tag Tournament 2 port.
#
# Milestone 1: the platform layer (src/platform) that replaces the Win32 calls the runtime
# makes. It builds for the NDK (arm64-v8a) and for a Linux host, where its tests run.
# The runtime itself (src/runtime) is moved onto this layer step by step; see port/ANDROID.md.
#
#   Host:    cmake -S port/android -B build-host && cmake --build build-host && ctest --test-dir build-host
#   Android: cmake -S port/android -B build-android \
#              -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
#              -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(PLATFORM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform")

add_library(ttt2_platform STATIC
    "${PLATFORM_DIR}/posix_memory.cpp"
    "${PLATFORM_DIR}/posix_fault.cpp"
    "${PLATFORM_DIR}/posix_clock_event.cpp")
target_include_directories(ttt2_platform PUBLIC "${PLATFORM_DIR}")
target_compile_options(ttt2_platform PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)
target_link_libraries(ttt2_platform PUBLIC Threads::Threads)

add_executable(platform_test "${PLATFORM_DIR}/platform_test.cpp")
target_link_libraries(platform_test PRIVATE ttt2_platform)

if(NOT ANDROID)
    enable_testing()
    add_test(NAME platform COMMAND platform_test)
endif()
