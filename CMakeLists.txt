cmake_minimum_required(VERSION 3.20)
project(NoviumEmulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Compiler warnings ---
if(MSVC)
    add_compile_options(/W4 /permissive-)
    add_compile_options(/wd4244 /wd4242 /wd4456 /wd4459)
    # Host acceleration: enable AVX2 on x64 when available
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|x64")
        add_compile_options(/arch:AVX2)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" HAS_AVX2)
    if(HAS_AVX2)
        add_compile_options(-mavx2 -mfma)
    endif()
    # ARM: enable NEON/SVE when toolchain supports it
    check_cxx_compiler_flag("-march=armv8-a+simd" HAS_NEON)
endif()

# Optimize for host CPU when requested
option(AISM_NATIVE "Build with -march=native for max host throughput" OFF)
if(AISM_NATIVE AND NOT MSVC)
    add_compile_options(-march=native)
endif()

# --- Core emulator library (static for in-process use) ---
add_library(aism_core STATIC
    src/emulator.cpp
    src/frontend.cpp
    src/coremark.cpp
    src/decoder.cpp
    src/mmu.cpp
    src/interconnect.cpp
    src/interrupt.cpp
    src/csr.cpp
    src/pipeline.cpp
    src/cache.cpp
    src/parallel.cpp
    src/trace.cpp
    src/tlm.cpp
    src/fpga.cpp
    src/pmc.cpp
    src/checkpoint.cpp
    src/cluster.cpp
)
target_include_directories(aism_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- Shared library with C-ABI (optional) ---
option(BUILD_SHARED_LIB "Build shared library with C-ABI" ON)
if(BUILD_SHARED_LIB)
    add_library(aism_emulator SHARED
        src/emulator.cpp
        src/frontend.cpp
        src/coremark.cpp
        src/decoder.cpp
        src/mmu.cpp
        src/interconnect.cpp
        src/interrupt.cpp
        src/csr.cpp
        src/pipeline.cpp
        src/cache.cpp
        src/parallel.cpp
        src/trace.cpp
        src/tlm.cpp
        src/fpga.cpp
        src/pmc.cpp
        src/checkpoint.cpp
        src/cluster.cpp
    )
    target_include_directories(aism_emulator PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_definitions(aism_emulator PRIVATE AISM_EXPORTS)
    if(WIN32)
        set_target_properties(aism_emulator PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
        )
    endif()
endif()

# --- Test executable ---
add_executable(novium_emu
    src/main.cpp
)
target_link_libraries(novium_emu PRIVATE aism_core)
target_include_directories(novium_emu PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
