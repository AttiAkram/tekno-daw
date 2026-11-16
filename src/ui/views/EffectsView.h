// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** The insert effects of the selected channel, or of the master: one card per slot, left to right in
    the order the sound goes through them. A card has the effect's graph (what it does to the sound, see
    EffectGraph), its knobs (from its parameter table), its choices, bypass, move left/right, remove,
    and a light: ink while it is making sound, the warning colour when a distortion clips hard. The
    waveshaper's points are dragged on its graph, and its ready-made curves sit above it. An empty slot
    offers "+ add". Every change is an fx.* command naming its target, so it undoes, and the terminal
    can do the same. */
class EffectsView final : public juce::Component, private juce::Timer
{
public:
    EffectsView (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::CommandRegistry& commands);
    ~EffectsView() override;

    void resized() override;
    void paint (juce::Graphics&) override;

    bool showsMaster() const noexcept { return master; }

private:
    class Card;

    void timerCallback() override;
    void run (const juce::String& command, core::CommandArgs args);
    core::EffectChain& chain() noexcept;
    juce::String targetName() const;

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;

    bool master = false; // false: the selected channel
    juce::TextButton channelTab, masterTab;
    std::array<std::unique_ptr<Card>, core::effectSlots> cards;
    juce::Rectangle<float> titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsView)
};
} // namespace tekno::ui
