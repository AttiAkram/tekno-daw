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

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"
#include "ui/components/LevelMeters.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** The mixer: a strip per channel (name, mute, solo, pan, fader, meter, level in dB) and the master.

    Like every view it changes nothing directly: faders and buttons run mixer.* commands, naming
    their channel. A whole fader drag is one undo step. Levels come from LevelMeters. */
class MixerView final : public juce::Component, private juce::Timer
{
public:
    MixerView (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::CommandRegistry& commands,
               LevelMeters& meters);
    ~MixerView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Repaint only the meters' parts. Called by the owner when LevelMeters changes. */
    void metersChanged();

    /** Fader taper: where a level sits on a fader or meter, 0 (bottom) to 1 (top). -12 dB is halfway. */
    static float faderProportion (float db) noexcept;
    static constexpr float midPointDb = -12.0f;

    static constexpr float stripWidth = 88.0f;

private:
    class Strips;

    void timerCallback() override;
    void run (const juce::String& command, const core::CommandArgs& args);
    void setUpFader (juce::Slider& fader);

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;
    LevelMeters& meters;

    std::unique_ptr<Strips> strips;
    juce::Viewport viewport;
    juce::Slider masterFader;
    juce::Rectangle<float> masterArea, masterMeterArea, masterReadoutArea, masterClipArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};
} // namespace tekno::ui
