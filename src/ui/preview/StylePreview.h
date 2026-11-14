// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/components/IconButton.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** M0 style preview (ADR 0016): a mock of the main screen, so the visual style can be judged on a
    real window before any feature exists. There is NO audio and NO model behind it: steps,
    playhead and transport are fake and live only in this component. Replaced by real views in M1.

    Keys: T theme, Space play/stop. Mouse: click step on/off, Shift+click accent, Ctrl+click slide. */
class StylePreview final : public juce::Component, private juce::Timer
{
public:
    explicit StylePreview (TeknoLookAndFeel& teknoLookAndFeel);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int numSteps = 16;
    static constexpr int numChannels = 3;

private:
    struct Step
    {
        bool on = false;
        bool accent = false;
        bool slide = false;
    };

    struct Channel
    {
        juce::String name;
        std::array<Step, numSteps> steps {};
    };

    struct StepRef
    {
        int channel = -1;
        int step = -1;

        bool isValid() const noexcept { return channel >= 0; }
        bool operator== (const StepRef&) const = default;
    };

    void timerCallback() override;
    void toggleTheme();
    void setPlaying (bool shouldPlay);
    StepRef stepAt (juce::Point<float> position) const;

    void paintTopBar (juce::Graphics&) const;
    void paintLeftColumn (juce::Graphics&) const;
    void paintContent (juce::Graphics&) const;
    void paintStep (juce::Graphics&, int channel, int step) const;
    void paintKnob (juce::Graphics&, juce::Rectangle<float> area, const juce::String& label, float value) const;
    void drawText (juce::Graphics&, const juce::String& text, const tokens::TextStyle& style, juce::Colour colour,
                   juce::Rectangle<float> area,
                   juce::Justification justification = juce::Justification::centredLeft) const;

    TeknoLookAndFeel& lnf;

    std::array<juce::TextButton, 4> navButtons;
    juce::TextButton bpmChip { "130 bpm" };
    IconButton recordButton { "record", IconButton::recordIcon() };
    juce::TextButton playButton { "play" };

    std::array<Channel, numChannels> channels;
    int selectedChannel = 0;
    int playhead = -1; // -1 = stopped
    StepRef hovered;

    // Layout, computed in resized()
    juce::Rectangle<float> topBar, leftColumn, content;
    juce::Rectangle<float> browserArea, synthArea, keysArea;
    juce::Rectangle<float> titleArea, metaArea, dividerArea, beatLabelArea;
    std::array<juce::Rectangle<float>, 4> knobAreas;
    std::array<juce::Rectangle<float>, numChannels> chipAreas;
    std::array<std::array<juce::Rectangle<float>, numSteps>, numChannels> stepAreas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StylePreview)
};
} // namespace tekno::ui
