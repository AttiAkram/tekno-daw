# SPDX-License-Identifier: AGPL-3.0-or-later

include(Catch)

add_executable(tekno_tests
    core/AudioEngineTests.cpp
    core/ChannelTests.cpp
    core/CommandLineTests.cpp
    core/CommandTests.cpp
    core/DriveTests.cpp
    core/EffectsTests.cpp
    core/KeymapTests.cpp
    core/LiveInputTests.cpp
    core/MidiImportTests.cpp
    core/NoteTests.cpp
    core/PatternTests.cpp
    core/ScaleTests.cpp
    core/ProjectFileTests.cpp
    core/VersionTests.cpp
    ui/FontsTests.cpp
    ui/KeyboardPanelTests.cpp
    ui/KeyBindingsTests.cpp
    ui/MixerTests.cpp
    ui/PaletteTests.cpp
    ui/TileLayoutTests.cpp
    ui/TokensTests.cpp)

target_link_libraries(tekno_tests PRIVATE tekno_ui Catch2::Catch2WithMain)
tekno_configure_target(tekno_tests)
# Tests compare token constants exactly on purpose: they pin the written spec. Set per source,
# because source options come after juce_recommended_warning_flags' -Wfloat-equal on the command line.
get_target_property(test_sources tekno_tests SOURCES)
set_source_files_properties(${test_sources} PROPERTIES COMPILE_OPTIONS -Wno-float-equal)

catch_discover_tests(tekno_tests
    TEST_PREFIX "unit."
    DISCOVERY_MODE PRE_TEST)

# The engine must never depend on the GUI. The compiler can't see it, this can.
add_test(NAME layering
    COMMAND "${CMAKE_COMMAND}" -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -P "${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake")

# Tests that open a real window. Need a display: on CI they run under xvfb-run.
if(TEKNO_GUI_TESTS)
    set(juce_failures "Leaked objects detected|JUCE Assertion failure")

    # Under sanitizers the window tests skip the audio device: pipewire-jack and the ALSA PipeWire
    # plugin keep global caches and are unloaded at exit, so LeakSanitizer reports them as leaks it
    # cannot even attribute (checked 2026-11-14: every block is allocated inside those libraries).
    # Our audio code is still covered under ASan by the engine unit tests, which call the same render().
    #
    # Elsewhere, the device is only opened where a Linux sound system exists: constructing JUCE's
    # AudioDeviceManager also opens the ALSA MIDI sequencer, and without /dev/snd/seq (CI runners,
    # containers) JUCE's Debug build asserts in juce_Midi_linux.cpp. Not our bug, and harmless in
    # Release — but it would fail every window test on a machine with no sound hardware.
    set(gui_test_args "")
    if(TEKNO_SANITIZERS)
        set(gui_test_args --no-audio)
    elseif(NOT EXISTS "/dev/snd/seq")
        set(gui_test_args --no-audio)
        message(STATUS "tekno: no ALSA sequencer here, window tests run with --no-audio")
    else()
        message(STATUS "tekno: window tests open the real audio device")
    endif()

    add_test(NAME app.smoke COMMAND tekno-daw --smoke-test ${gui_test_args})
    add_test(NAME app.snapshot.dark  COMMAND tekno-daw ${gui_test_args} --theme=dark  "--snapshot=${CMAKE_BINARY_DIR}/snapshots/pattern-dark.png")
    add_test(NAME app.snapshot.light COMMAND tekno-daw ${gui_test_args} --theme=light "--snapshot=${CMAKE_BINARY_DIR}/snapshots/pattern-light.png")
    add_test(NAME app.snapshot.pianoroll COMMAND tekno-daw ${gui_test_args} --theme=light --view=pianoroll "--snapshot=${CMAKE_BINARY_DIR}/snapshots/pianoroll-light.png")
    add_test(NAME app.snapshot.keys COMMAND tekno-daw ${gui_test_args} --theme=light --view=keys "--snapshot=${CMAKE_BINARY_DIR}/snapshots/keys-light.png")
    add_test(NAME app.snapshot.tiles COMMAND tekno-daw ${gui_test_args} --theme=dark "--run=ui.tile pianoroll; ui.tile mixer; ui.terminal" "--snapshot=${CMAKE_BINARY_DIR}/snapshots/tiles-dark.png")
    add_test(NAME app.snapshot.effects COMMAND tekno-daw ${gui_test_args} --theme=light --view=effects "--run=fx.add delay; fx.add distortion; fx.add reverb; ui.terminal" "--snapshot=${CMAKE_BINARY_DIR}/snapshots/effects-light.png")
    add_test(NAME app.snapshot.mixer COMMAND tekno-daw ${gui_test_args} --theme=dark --view=mixer "--snapshot=${CMAKE_BINARY_DIR}/snapshots/mixer-dark.png")
    # The smallest window the app allows: the top bar must still fit between the name and the transport.
    add_test(NAME app.snapshot.narrow COMMAND tekno-daw ${gui_test_args} --theme=dark --size=1024x640 "--snapshot=${CMAKE_BINARY_DIR}/snapshots/narrow-dark.png")
    add_test(NAME app.snapshot.keyboard COMMAND tekno-daw ${gui_test_args} --theme=light --view=keyboard "--snapshot=${CMAKE_BINARY_DIR}/snapshots/keyboard-light.png")

    # Window tests never read or write the developer's own settings, nor each other's: each one starts
    # from its own empty folder, emptied before every run. (A layout saved by one test, or by the last
    # run, would otherwise show up in the next snapshot.)
    set(gui_tests app.smoke app.snapshot.dark app.snapshot.light app.snapshot.mixer app.snapshot.pianoroll
        app.snapshot.keys app.snapshot.tiles app.snapshot.effects app.snapshot.keyboard app.snapshot.narrow)
    add_test(NAME app.settings.clean COMMAND "${CMAKE_COMMAND}" -E rm -rf "${CMAKE_BINARY_DIR}/test-config")
    set_tests_properties(app.settings.clean PROPERTIES FIXTURES_SETUP gui_settings)
    foreach(test IN LISTS gui_tests)
        set(env "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config/${test}")
        if(TEKNO_SANITIZERS)
            list(APPEND env "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/lsan.supp:print_suppressions=0")
        endif()
        set_tests_properties(${test} PROPERTIES
            ENVIRONMENT "${env}"
            FIXTURES_REQUIRED gui_settings
            TIMEOUT 30
            FAIL_REGULAR_EXPRESSION "${juce_failures}"
            RESOURCE_LOCK display)
    endforeach()
endif()
