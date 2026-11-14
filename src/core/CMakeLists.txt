# SPDX-License-Identifier: AGPL-3.0-or-later

configure_file(BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.h" @ONLY)

add_library(tekno_core STATIC
    Version.cpp
    model/Channel.cpp
    model/NoteNames.cpp
    model/Pattern.cpp
    model/SampleLibrary.cpp
    model/Scale.cpp
    model/Session.cpp
    commands/CommandLine.cpp
    commands/CommandRegistry.cpp
    commands/SessionCommands.cpp
    dsp/AcidVoice.cpp
    dsp/BuiltinKit.cpp
    dsp/Sampler.cpp
    engine/AudioEngine.cpp
    engine/OfflineRender.cpp
    io/MidiImport.cpp
    io/ProjectFile.cpp)
add_library(tekno::core ALIAS tekno_core)

target_include_directories(tekno_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(tekno_core PUBLIC tekno_juce)
tekno_configure_target(tekno_core)
