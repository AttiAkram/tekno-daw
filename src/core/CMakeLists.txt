# SPDX-License-Identifier: AGPL-3.0-or-later

configure_file(BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.h" @ONLY)

add_library(tekno_core STATIC
    Version.cpp)
add_library(tekno::core ALIAS tekno_core)

target_include_directories(tekno_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(tekno_core PUBLIC tekno_juce)
tekno_configure_target(tekno_core)
