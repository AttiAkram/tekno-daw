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
    delete it; Shift+click for accent, Ctrl+click for slide. Hold and drag on empty space to select
    several notes, then drag one of them to move them all; Delete removes them. The lane at the bottom
    shows each note's velocity: drag a bar up or down. Every gesture is previewed and runs one note(s).*
    command when released: one undo step, the same action the terminal would run. */
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
    bool keyPressed (const juce::KeyPress&) override;

    /** The selected notes, as (start, pitch). View state: not saved, not undone. */
    const std::vector<std::pair<int, int>>& getSelection() const noexcept { return selection; }

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
        move,     // the selection, by moveTicks and moveSemitones
        resize,   // one note's length
        select,   // a rectangle from dragStart
        velocity, // one note's bar in the velocity lane (or the whole selection's)
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
    bool isSelected (const core::Note& note) const noexcept;
    juce::String selectionList() const;                             // "48:57 72:60", for the notes.* commands
    int velocityBarAt (juce::Point<float> position) const noexcept; // index into shown.notes, or -1
    int velocityFromY (float y) const noexcept;
    void addNoteAt (juce::Point<float> position);

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::CommandRegistry& commands;

    Snapshot shown;
    Snap snap = Snap::step;
    int newNoteLength = core::Note::ticksPerStep;
    float scrollY = 0.0f;
    int scrolledFor = -1; // pattern * 16 + channel the view last scrolled to

    Drag drag = Drag::none;
    core::Note dragOriginal, dragNow; // the note grabbed (resize, velocity) and how it looks now
    juce::Point<float> dragStart, dragEnd;
    int moveTicks = 0, moveSemitones = 0;
    std::vector<std::pair<int, int>> selection;

    juce::Rectangle<float> keyboard, grid, velocityLane, velocityLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollView)
};
} // namespace tekno::ui
