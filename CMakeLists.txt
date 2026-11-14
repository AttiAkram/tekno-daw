# tekno-daw — pattern-based DAW for acid / mental tekno
# Copyright (C) 2026 tekno-daw contributors
# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(tekno-daw
    VERSION 0.0.1
    DESCRIPTION "Pattern-based DAW for acid / mental tekno"
    HOMEPAGE_URL "https://github.com/AttiAkram/tekno-daw"
    LANGUAGES C CXX) # C is needed: JUCE builds zlib/libpng/libflac as C since 9.0.1

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

option(TEKNO_BUILD_TESTS          "Build the unit tests"                                   ON)
option(TEKNO_GUI_TESTS            "Register tests that open a window (needs a display)"    OFF)
option(TEKNO_WARNINGS_AS_ERRORS   "Treat warnings in tekno-daw code as errors"             OFF)
option(TEKNO_SANITIZERS           "Build everything with AddressSanitizer + UBSan"         OFF)
option(TEKNO_NATIVE_ARCH          "Optimise for this CPU only (-march=native). Never for releases" OFF)

include(TeknoCompilerOptions)
include(TeknoDependencies)
include(TeknoJuce)

add_subdirectory(assets)
add_subdirectory(src)

if(TEKNO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
