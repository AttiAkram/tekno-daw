// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** The piano roll: the selected channel's notes in the selected pattern, time across and pitch up,
    with the other channels' notes in grey behind them (ghost notes).

    Click to add a note, drag it to move it, drag its right edge to change its length, right-click to
    delete it; Shift+click for accent, Ctrl+click for slide. A drag is previewed and runs one note.*
    command when released, so it is one undo step and the same action the terminal would run. */
class PianoRollView final : public juce::Component, private juce::Timer
{
public:
    /** Where clicks and drags land. An editor setting, not part of the project. */
    enum class Snap
    {
        step,         // 1/16, 24 ticks
        thirtySecond, // 12 ticks
        triplet,      // 1/16 triplets, 16 ticks
        free,         // any tick
    };
    static std::optional<Snap> snapFromName (const juce::String& name);
    static juce::String snapName (Snap snap);
    static int snapTicks (Snap snap) noexcept;

    PianoRollView (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::CommandRegistry& commands);

    void setSnap (Snap snap);
    Snap getSnap() const noexcept { return snap; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int lowestNote = core::Step::minNote;
    static constexpr int highestNote = core::Step::maxNote;

private:
    struct Snapshot
    {
        int pattern = 0, channel = 0, length = 0, currentStep = -1;
        bool playing = false;
        std::vector<core::Note> notes, ghosts;

        bool operator== (const Snapshot&) const = default;
    };

    enum class Drag
    {
        none,
        move,
        resize,
    };

    void timerCallback() override;
    Snapshot takeSnapshot() const;
    void run (const juce::String& command, core::CommandArgs args);
    void scrollTo (int pitch); // centre this pitch vertically
    void scrollBy (float pixels);
    void scrollToNotesIfNew();

    float tickToX (double tick) const noexcept;
    double xToTick (float x) const noexcept;
    float pitchToY (int pitch) const noexcept; // top of the row
    int yToPitch (float y) const noexcept;
    int snapped (double tick) const noexcept; // nearest snap point
    juce::Rectangle<float> noteArea (const core::Note& note) const noexcept;
    int noteAt (juce::Point<float> position) const noexcept; // index into shown.notes, or -1
    bool onRightEdge (const core::Note& note, juce::Point<float> position) const noexcept;

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;

    Snapshot shown;
    Snap snap = Snap::step;
    int newNoteLength = core::Note::ticksPerStep;
    float scrollY = 0.0f;
    int scrolledFor = -1; // pattern * 16 + channel the view last scrolled to

    Drag drag = Drag::none;
    core::Note dragOriginal, dragNow;
    juce::Point<float> dragStart;

    juce::Rectangle<float> keyboard, grid;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
} // namespace tekno::ui
