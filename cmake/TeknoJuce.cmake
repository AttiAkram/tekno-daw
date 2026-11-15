# SPDX-License-Identifier: AGPL-3.0-or-later
#
# `tekno_juce`: every JUCE module we use, compiled ONCE into a single static library.
#
# JUCE modules are CMake INTERFACE libraries: each target linking one compiles the module's
# sources again. Two of our libraries linking JUCE directly would put two copies of JUCE in the
# same executable. Instead every tekno target links `tekno_juce`, which is built once and shared
# by the app, the tests and (later) the headless engine.

add_library(tekno_juce STATIC)

target_link_libraries(tekno_juce
    PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_graphics
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

target_compile_definitions(tekno_juce
    PUBLIC
        JUCE_STANDALONE_APPLICATION=1
        JUCE_USE_CURL=0                 # no network code in the binary yet
        JUCE_WEB_BROWSER=0              # no webkit2gtk dependency
        JUCE_JACK=1                     # JACK (and PipeWire through pipewire-jack) next to ALSA
        "JUCE_JACK_CLIENT_NAME=\"tekno-daw\"" # our name in the JACK / PipeWire graph, not "JUCEJack"
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_DISABLE_JUCE_VERSION_PRINTING=1)

# Re-export what the PRIVATE module links gave us, so consumers can include JUCE headers.
target_include_directories(tekno_juce INTERFACE
    $<TARGET_PROPERTY:tekno_juce,INCLUDE_DIRECTORIES>)
target_compile_definitions(tekno_juce INTERFACE
    $<TARGET_PROPERTY:tekno_juce,COMPILE_DEFINITIONS>)

set_target_properties(tekno_juce PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)
