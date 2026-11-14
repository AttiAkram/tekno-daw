// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/PianoRollView.h"

#include <cmath>

#include "core/model/NoteNames.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::Note;

    constexpr float keyboardWidth = 52.0f;
    constexpr float rowHeight = 14.0f;
    constexpr float edgeGrab = 6.0f; // how close to a note's right edge a drag resizes it
    constexpr int rows = PianoRollView::highestNote - PianoRollView::lowestNote + 1;

    bool isBlackKey (int pitch) noexcept
    {
        const auto pc = pitch % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
} // namespace

std::optional<PianoRollView::Snap> PianoRollView::snapFromName (const juce::String& name)
{
    if (name == "step" || name == "1/16")
        return Snap::step;
    if (name == "1/32")
        return Snap::thirtySecond;
    if (name == "1/3" || name == "triplet" || name == "triplets")
        return Snap::triplet;
    if (name == "free" || name == "off")
        return Snap::free;
    return std::nullopt;
}

juce::String PianoRollView::snapName (Snap s)
{
    switch (s)
    {
    case Snap::step:
        return "step";
    case Snap::thirtySecond:
        return "1/32";
    case Snap::triplet:
        return "1/3";
    case Snap::free:
        return "free";
    }
    return "step";
}

int PianoRollView::snapTicks (Snap s) noexcept
{
    switch (s)
    {
    case Snap::step:
        return Note::ticksPerStep;
    case Snap::thirtySecond:
        return Note::ticksPerStep / 2;
    case Snap::triplet:
        return Note::ticksPerStep * 2 / 3;
    case Snap::free:
        return 1;
    }
    return Note::ticksPerStep;
}

PianoRollView::PianoRollView (TeknoLookAndFeel& teknoLookAndFeel, core::Session& sessionToShow,
                              core::CommandRegistry& registry)
    : lnf (teknoLookAndFeel)
    , session (sessionToShow)
    , commands (registry)
{
    shown = takeSnapshot();
    startTimerHz (30);
}

void PianoRollView::setSnap (Snap newSnap)
{
    snap = newSnap;
    newNoteLength = std::max (newNoteLength / snapTicks (snap), 1) * snapTicks (snap);
    repaint();
}

void PianoRollView::run (const juce::String& command, core::CommandArgs args)
{
    args.set ("pattern", shown.pattern + 1);
    args.set ("channel", shown.channel + 1);
    const auto outcome = commands.execute (command, args);
    if (! outcome.succeeded())
        juce::Logger::writeToLog ("command " + command + " failed: " + outcome.error);
    timerCallback();
}

// --- state ----------------------------------------------------------------------------------------

PianoRollView::Snapshot PianoRollView::takeSnapshot() const
{
    Snapshot s;
    s.pattern = session.selected.load();
    s.channel = std::clamp (session.selectedChannel.load(), 0, std::max (0, session.channelCount() - 1));
    const auto& pattern = session.pattern (s.pattern);
    s.length = pattern.getLength();
    s.playing = session.transport.playing.load();
    s.currentStep = s.playing && session.playing.load() == s.pattern ? session.transport.currentStep.load() : -1;
    for (int c = 0; c < session.channelCount(); ++c)
    {
        auto notes = pattern.lane (c).notes();
        auto& into = c == s.channel ? s.notes : s.ghosts;
        into.insert (into.end(), notes.begin(), notes.end());
    }
    return s;
}

void PianoRollView::timerCallback()
{
    if (! isShowing())
        return;
    const auto now = takeSnapshot();
    if (now == shown)
    {
        scrollToNotesIfNew(); // the first time it shows, before anything changes
        return;
    }
    shown = now;
    scrollToNotesIfNew();
    repaint();
}

void PianoRollView::scrollToNotesIfNew()
{
    // Each time another pattern or channel comes up, show where its notes are: their middle, or a
    // sensible octave when it has none. Not while dragging.
    const auto key = shown.pattern * core::Session::maxChannels + shown.channel;
    if (key == scrolledFor || drag != Drag::none || grid.getHeight() <= 0.0f)
        return;
    scrolledFor = key;
    if (shown.notes.empty())
    {
        scrollTo (session.channel (shown.channel).kind.load() == core::InstrumentKind::sampler
                      ? core::Step::samplerRootNote
                      : 48);
        return;
    }
    auto sum = 0;
    for (const auto& n : shown.notes)
        sum += n.pitch;
    scrollTo (sum / static_cast<int> (shown.notes.size()));
}

// --- geometry -------------------------------------------------------------------------------------

void PianoRollView::resized()
{
    auto area = getLocalBounds().toFloat();
    keyboard = area.removeFromLeft (keyboardWidth);
    grid = area;
    scrollBy (0.0f); // clamp
}

void PianoRollView::scrollTo (int pitch)
{
    scrollY = (static_cast<float> (highestNote - pitch) + 0.5f) * rowHeight - grid.getHeight() * 0.5f;
    scrollBy (0.0f);
}

void PianoRollView::scrollBy (float pixels)
{
    const auto maxScroll = std::max (0.0f, rows * rowHeight - grid.getHeight());
    scrollY = std::clamp (scrollY + pixels, 0.0f, maxScroll);
    repaint();
}

float PianoRollView::tickToX (double tick) const noexcept
{
    const auto ticks = std::max (1, shown.length) * Note::ticksPerStep;
    return grid.getX() + static_cast<float> (tick / ticks) * grid.getWidth();
}

double PianoRollView::xToTick (float x) const noexcept
{
    const auto ticks = std::max (1, shown.length) * Note::ticksPerStep;
    return static_cast<double> ((x - grid.getX()) / grid.getWidth()) * ticks;
}

float PianoRollView::pitchToY (int pitch) const noexcept
{
    return grid.getY() + static_cast<float> (highestNote - pitch) * rowHeight - scrollY;
}

int PianoRollView::yToPitch (float y) const noexcept
{
    const auto row = static_cast<int> (std::floor ((y - grid.getY() + scrollY) / rowHeight));
    return std::clamp (highestNote - row, lowestNote, highestNote);
}

int PianoRollView::snapped (double tick) const noexcept
{
    const auto step = snapTicks (snap);
    return static_cast<int> (std::round (tick / step)) * step;
}

juce::Rectangle<float> PianoRollView::noteArea (const Note& note) const noexcept
{
    const auto x = tickToX (note.start);
    return { x, pitchToY (note.pitch), std::max (3.0f, tickToX (note.end()) - x), rowHeight };
}

int PianoRollView::noteAt (juce::Point<float> position) const noexcept
{
    // The last drawn (latest) wins where notes overlap.
    for (auto i = static_cast<int> (shown.notes.size()) - 1; i >= 0; --i)
        if (noteArea (shown.notes[static_cast<size_t> (i)]).contains (position))
            return i;
    return -1;
}

bool PianoRollView::onRightEdge (const Note& note, juce::Point<float> position) const noexcept
{
    const auto area = noteArea (note);
    return position.x >= area.getRight() - std::min (edgeGrab, area.getWidth() * 0.4f);
}

// --- painting -------------------------------------------------------------------------------------

void PianoRollView::paint (juce::Graphics& g)
{
    const auto ink = lnf.colour (&P::ink), line = lnf.colour (&P::line), muted = lnf.colour (&P::inkMuted);

    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (getLocalBounds());

        // Rows: black keys darker, a line under every C.
        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            const auto y = pitchToY (pitch);
            if (y + rowHeight < 0.0f || y > static_cast<float> (getHeight()))
                continue;
            if (isBlackKey (pitch))
            {
                g.setColour (lnf.colour (&P::surface));
                g.fillRect (juce::Rectangle<float> (grid.getX(), y, grid.getWidth(), rowHeight));
            }
            if (pitch % 12 == 0)
            {
                g.setColour (line);
                g.fillRect (grid.getX(), y + rowHeight - shape::lineWidth, grid.getWidth(), shape::lineWidth);
            }
        }

        // Columns: steps faint, beats clear, bars strong.
        for (int s = 0; s <= shown.length; ++s)
        {
            const auto x = tickToX (s * Note::ticksPerStep);
            g.setColour (s % 16 == 0 ? muted : (s % 4 == 0 ? line : line.withMultipliedAlpha (0.4f)));
            g.fillRect (x, grid.getY(), shape::lineWidth, grid.getHeight());
        }

        // Ghost notes: the other channels, for reference.
        g.setColour (line);
        for (const auto& n : shown.ghosts)
            g.fillRoundedRectangle (noteArea (n).reduced (0.0f, 2.0f), 2.0f);

        // This channel's notes. Yellow while sounding; accent = a notch on top; slide = a line into the next.
        const auto playTick = shown.currentStep * Note::ticksPerStep;
        for (size_t i = 0; i < shown.notes.size(); ++i)
        {
            auto note = shown.notes[i];
            const auto dragged = drag != Drag::none && note == dragOriginal;
            if (dragged)
                note = dragNow;
            const auto area = noteArea (note).reduced (0.0f, 1.0f);
            const auto sounding = ! dragged && shown.currentStep >= 0 &&
                                  note.start <= playTick + Note::ticksPerStep - 1 && note.end() > playTick;
            g.setColour (sounding ? lnf.colour (&P::accent) : ink);
            g.fillRoundedRectangle (area, 2.0f);
            if (sounding)
            {
                g.setColour (lnf.colour (&P::accentOutline));
                g.drawRoundedRectangle (area, 2.0f, shape::lineWidth);
            }
            if (note.accent)
            {
                g.setColour (lnf.colour (&P::bg));
                g.fillRect (area.getX() + 2.0f, area.getY() + 2.0f, std::max (2.0f, area.getWidth() - 4.0f), 2.0f);
            }
            if (note.slide)
            {
                // Into the note that starts where this one ends, if there is one.
                for (const auto& next : shown.notes)
                    if (next.start == note.end())
                    {
                        g.setColour (ink);
                        g.drawLine (area.getRight(), area.getCentreY(), tickToX (next.start) + 4.0f,
                                    pitchToY (next.pitch) + rowHeight * 0.5f, shape::iconStroke);
                        break;
                    }
            }
            if (area.getWidth() > 30.0f)
            {
                g.setFont (lnf.fonts().font (type::meta).withHeight (10.0f));
                g.setColour (sounding ? lnf.colour (&P::onAccent) : lnf.colour (&P::bg));
                g.drawText (core::noteName (note.pitch), area.reduced (4.0f, 0.0f), juce::Justification::centredLeft,
                            false);
            }
        }

        // Playhead.
        if (shown.currentStep >= 0)
        {
            g.setColour (ink);
            g.fillRect (tickToX (playTick), grid.getY(), shape::lineWidth * 2.0f, grid.getHeight());
        }
    }

    // Keyboard: white and black keys, C's named.
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (keyboard.toNearestInt());
        g.setColour (lnf.colour (&P::bg));
        g.fillRect (keyboard);
        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            const auto row = juce::Rectangle<float> (keyboard.getX(), pitchToY (pitch), keyboard.getWidth(), rowHeight);
            if (isBlackKey (pitch)) // the line colour: a black key in light, a dark grey one in dark
            {
                g.setColour (line);
                g.fillRect (row.withWidth (keyboard.getWidth() * 0.6f).reduced (0.0f, 1.0f));
            }
            if (pitch % 12 == 0)
            {
                g.setColour (line);
                g.fillRect (row.getX(), row.getBottom() - shape::lineWidth, row.getWidth(), shape::lineWidth);
                g.setFont (lnf.fonts().font (type::meta).withHeight (10.0f));
                g.setColour (muted);
                g.drawText (core::noteName (pitch), row.withTrimmedRight (space::unit),
                            juce::Justification::centredRight, false);
            }
        }
        g.setColour (line);
        g.fillRect (keyboard.getRight() - shape::lineWidth, keyboard.getY(), shape::lineWidth, keyboard.getHeight());
    }

    g.setColour (line);
    g.drawRect (getLocalBounds().toFloat(), shape::lineWidth);
}

// --- interaction ----------------------------------------------------------------------------------

void PianoRollView::mouseMove (const juce::MouseEvent& e)
{
    const auto index = noteAt (e.position);
    setMouseCursor (index >= 0 && onRightEdge (shown.notes[static_cast<size_t> (index)], e.position)
                        ? juce::MouseCursor::LeftRightResizeCursor
                        : juce::MouseCursor::NormalCursor);
}

void PianoRollView::mouseDown (const juce::MouseEvent& e)
{
    if (! grid.contains (e.position) || session.channelCount() == 0)
        return;

    const auto index = noteAt (e.position);
    if (index >= 0)
    {
        const auto note = shown.notes[static_cast<size_t> (index)];
        const core::CommandArgs target { { "at", note.start }, { "note", note.pitch } };
        auto args = target;
        if (e.mods.isPopupMenu())
            run ("note.remove", args);
        else if (e.mods.isShiftDown())
            args.set ("on", ! note.accent), run ("note.accent", args);
        else if (e.mods.isCtrlDown())
            args.set ("on", ! note.slide), run ("note.slide", args);
        else
        {
            drag = onRightEdge (note, e.position) ? Drag::resize : Drag::move;
            dragOriginal = dragNow = note;
            dragStart = e.position;
        }
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    // Empty space: a new note, on the snap, as long as the last one touched.
    const auto start =
        std::clamp (static_cast<int> (std::floor (xToTick (e.position.x) / snapTicks (snap))) * snapTicks (snap), 0,
                    shown.length * Note::ticksPerStep - 1);
    run ("note.add", { { "at", start }, { "note", yToPitch (e.position.y) }, { "length", newNoteLength } });
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none)
        return;
    const auto deltaTicks = xToTick (e.position.x) - xToTick (dragStart.x);
    dragNow = dragOriginal;
    if (drag == Drag::move)
    {
        dragNow.start =
            std::clamp (snapped (dragOriginal.start + deltaTicks), 0, shown.length * Note::ticksPerStep - 1);
        dragNow.pitch =
            std::clamp (dragOriginal.pitch + yToPitch (e.position.y) - yToPitch (dragStart.y), lowestNote, highestNote);
    }
    else
        dragNow.length = std::clamp (snapped (dragOriginal.length + deltaTicks), snapTicks (snap),
                                     core::Pattern::maxSteps * Note::ticksPerStep);
    repaint();
}

void PianoRollView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::none)
        return;
    const auto what = drag;
    drag = Drag::none;
    if (dragNow == dragOriginal)
    {
        repaint();
        return;
    }
    const core::CommandArgs target { { "at", dragOriginal.start }, { "note", dragOriginal.pitch } };
    auto args = target;
    if (what == Drag::move)
    {
        args.set ("toAt", dragNow.start);
        args.set ("toNote", dragNow.pitch);
        run ("note.move", args);
    }
    else
    {
        args.set ("length", dragNow.length);
        run ("note.resize", args);
    }
    newNoteLength = dragNow.length;
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollBy (-wheel.deltaY * rowHeight * 12.0f);
}
} // namespace tekno::ui
