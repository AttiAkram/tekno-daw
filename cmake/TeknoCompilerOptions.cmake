# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Toolchain speed-ups and per-target compiler options. See ADR 0010.

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
