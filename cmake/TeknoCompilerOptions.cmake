# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Toolchain speed-ups and per-target compiler options.

# --- compiler cache: free 5-10x on rebuilds after a clean or a branch switch -------------------
find_program(TEKNO_CCACHE ccache)
if(TEKNO_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    message(STATUS "tekno: using ccache (${TEKNO_CCACHE})")
    set(CMAKE_C_COMPILER_LAUNCHER   "${TEKNO_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${TEKNO_CCACHE}")
endif()

# --- fast linker: linking JUCE with GNU ld is the slowest step of an incremental build ---------
# mold > lld > default. juce_recommended_lto_flags turns on -flto for Release, and lld cannot link
# GCC's LTO objects, so lld is skipped for GCC Release builds.
if(NOT DEFINED CMAKE_LINKER_TYPE AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.29)
    find_program(TEKNO_MOLD mold)
    find_program(TEKNO_LLD ld.lld)
    if(TEKNO_MOLD)
        set(CMAKE_LINKER_TYPE MOLD)
    elseif(TEKNO_LLD AND NOT (CMAKE_BUILD_TYPE STREQUAL "Release" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
        set(CMAKE_LINKER_TYPE LLD)
    endif()
    if(DEFINED CMAKE_LINKER_TYPE)
        message(STATUS "tekno: linker ${CMAKE_LINKER_TYPE}")
    endif()
endif()

# --- parallel jobs bounded by memory, not only by cores ------------------------------------------
# One JUCE translation unit peaks at ~1.05 GB (Debug) and ~1.5 GB (ASan), measured on 2026-11-14.
# Ninja's default of one job per core can exhaust RAM on a desktop that is running other programs,
# and the kernel's OOM killer then kills one of THEM. By default the build may use half of the RAM.
cmake_host_system_information(RESULT tekno_total_mb QUERY TOTAL_PHYSICAL_MEMORY)
cmake_host_system_information(RESULT tekno_cores QUERY NUMBER_OF_LOGICAL_CORES)
math(EXPR tekno_default_budget "${tekno_total_mb} / 2")
set(TEKNO_BUILD_MEMORY_MB "${tekno_default_budget}" CACHE STRING
    "RAM (MB) the compile jobs may use together; the rest stays free for the system")

if(TEKNO_SANITIZERS)
    set(tekno_mb_per_job 1600)
else()
    set(tekno_mb_per_job 1100)
endif()
math(EXPR tekno_jobs "${TEKNO_BUILD_MEMORY_MB} / ${tekno_mb_per_job}")
if(tekno_jobs LESS 1)
    set(tekno_jobs 1)
elseif(tekno_jobs GREATER tekno_cores)
    set(tekno_jobs ${tekno_cores})
endif()

set_property(GLOBAL APPEND PROPERTY JOB_POOLS tekno_compile=${tekno_jobs} tekno_link=2)
set(CMAKE_JOB_POOL_COMPILE tekno_compile)
set(CMAKE_JOB_POOL_LINK tekno_link)
message(STATUS "tekno: at most ${tekno_jobs} compile jobs (${TEKNO_BUILD_MEMORY_MB} MB of ${tekno_total_mb} MB RAM)")

# --- whole-build options (must apply to JUCE too, so they are global) ---------------------------
if(TEKNO_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

if(TEKNO_NATIVE_ARCH)
    add_compile_options(-march=native)
endif()

# --- options for tekno-daw's own targets (never for third-party code) ---------------------------
function(tekno_configure_target target)
    target_link_libraries(${target} PRIVATE juce::juce_recommended_warning_flags)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wshadow -Wnon-virtual-dtor -Wimplicit-fallthrough)
        if(TEKNO_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
