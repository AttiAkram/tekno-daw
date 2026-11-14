# SPDX-License-Identifier: AGPL-3.0-or-later

include(Catch)

add_executable(tekno_tests
    core/AudioEngineTests.cpp
    core/ChannelTests.cpp
    core/CommandLineTests.cpp
    core/CommandTests.cpp
    core/DriveTests.cpp
    core/PatternTests.cpp
    core/ProjectFileTests.cpp
    core/VersionTests.cpp
    ui/FontsTests.cpp
    ui/PaletteTests.cpp
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

    set_tests_properties(app.smoke app.snapshot.dark app.snapshot.light PROPERTIES
        TIMEOUT 30
        FAIL_REGULAR_EXPRESSION "${juce_failures}"
        RESOURCE_LOCK display)

    if(TEKNO_SANITIZERS)
        set_tests_properties(app.smoke app.snapshot.dark app.snapshot.light PROPERTIES
            ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/lsan.supp:print_suppressions=0")
    endif()
endif()
