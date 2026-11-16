# SPDX-License-Identifier: AGPL-3.0-or-later

configure_file(BuildInfo.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.h" @ONLY)

add_library(tekno_core STATIC
    Version.cpp
    model/Channel.cpp
    model/Effects.cpp
    model/NoteNames.cpp
    model/Pattern.cpp
    model/SampleLibrary.cpp
    model/Scale.cpp
    model/Session.cpp
    commands/CommandLine.cpp
    commands/CommandRegistry.cpp
    commands/Keymap.cpp
    commands/SessionCommands.cpp
    dsp/AcidVoice.cpp
    dsp/BuiltinKit.cpp
    dsp/Distortion.cpp
    dsp/PingPongDelay.cpp
    dsp/PlateReverb.cpp
    dsp/Sampler.cpp
    dsp/Shaper.cpp
    dsp/SubVoice.cpp
    engine/AudioEngine.cpp
    engine/LiveInput.cpp
    engine/OfflineRender.cpp
    io/MidiImport.cpp
    io/ProjectFile.cpp)
add_library(tekno::core ALIAS tekno_core)

# The demo project is built into the program, so it opens at start with no file to find. Read as bytes:
# a string this long would be over what compilers promise to handle.
set(tekno_demo "${PROJECT_SOURCE_DIR}/examples/demo.tekno")
file(READ "${tekno_demo}" tekno_demo_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," TEKNO_DEMO_BYTES "${tekno_demo_hex}")
configure_file(io/DemoProject.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/DemoProject.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${tekno_demo}")
target_sources(tekno_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/DemoProject.cpp")

target_include_directories(tekno_core PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_link_libraries(tekno_core PUBLIC tekno_juce)
tekno_configure_target(tekno_core)
