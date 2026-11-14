// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"
#include "ui/ProjectActions.h"
#include "ui/components/ValueChip.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** What the view shows about the audio device. Filled by the app, which owns the device. */
struct AudioStatus
{
    bool running = false;
    juce::String deviceName;
    double sampleRate = 0.0;
    int bufferSize = 0;
    juce::String problem; // why there is no audio, when not running
};

/** The main screen: transport, project, the 303's controls, the pattern bank and the step grid.

    The view never changes the model directly: every edit is a command (ADR 0014), and file dialogs go
    through ProjectActions. It reads the shared Session (atomics) on a timer, so what it shows is what
    the audio thread is playing. */
class PatternView final : public juce::Component, private juce::Timer
{
public:
    PatternView (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::CommandRegistry& commands,
                 ProjectActions& projectActions, std::function<AudioStatus()> audioStatus);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int stepsPerRow = 16;
    static constexpr int maxRows = core::Pattern::maxSteps / stepsPerRow;

private:
    struct Knob
    {
        const char* label;
        const char* command;
        std::atomic<float>* value;
        float defaultValue;
        juce::Slider slider;
    };

    /** Everything on screen that comes from the Session, to repaint only when something changed. */
    struct Snapshot
    {
        std::array<std::uint32_t, core::Pattern::maxSteps> steps {};
        int length = 0;
        int selected = 0;
        int playingPattern = 0;
        std::uint32_t usedPatterns = 0; // bit per pattern that holds something
        int currentStep = -1;
        bool playing = false;
        double bpm = 0.0;
        float swing = 0.0f;
        bool square = false;
        juce::String patternName, documentTitle;

        bool operator== (const Snapshot&) const = default;
    };

    void timerCallback() override;
    Snapshot takeSnapshot() const;
    void run (const juce::String& command, const core::CommandArgs& args = {});
    int stepAt (juce::Point<float> position) const;
    int patternChipAt (juce::Point<float> position) const;
    void selectPattern (int index);
    void startRename();
    void finishRename (bool keep);
    void layoutSteps();

    void paintTopBar (juce::Graphics&) const;
    void paintLeftColumn (juce::Graphics&) const;
    void paintContent (juce::Graphics&) const;
    void paintStep (juce::Graphics&, int index) const;
    void paintBadge (juce::Graphics&, juce::Rectangle<float> area, bool ok, const juce::String& letter) const;
    void drawText (juce::Graphics&, const juce::String& text, const tokens::TextStyle& style, juce::Colour colour,
                   juce::Rectangle<float> area,
                   juce::Justification justification = juce::Justification::centredLeft) const;

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;
    ProjectActions& actions;
    std::function<AudioStatus()> audioStatus;

    std::array<juce::TextButton, 4> navButtons;
    ValueChip swingChip, bpmChip, lengthChip;
    juce::TextButton playButton { "play" };
    juce::TextButton waveButton { "saw" };
    juce::TextButton newButton { "new" }, openButton { "open" }, saveButton { "save" };
    juce::TextEditor renameEditor;
    std::array<Knob, 5> knobs;

    Snapshot shown;
    AudioStatus shownAudio;
    int hoveredStep = -1;
    int timerTicks = 0;

    // Layout, computed in resized()
    juce::Rectangle<float> topBar, leftColumn, content;
    juce::Rectangle<float> projectArea, synthArea, keysArea, audioArea;
    juce::Rectangle<float> titleArea, metaArea, dividerArea, patternBarArea, gridArea, beatLabelArea, chipArea;
    std::array<juce::Rectangle<float>, core::Session::maxPatterns> patternChips;
    std::array<juce::Rectangle<float>, core::Pattern::maxSteps> stepAreas;
    int laidOutLength = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternView)
};
} // namespace tekno::ui
