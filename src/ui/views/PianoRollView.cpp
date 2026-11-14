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
    constexpr float velocityLaneHeight = 72.0f;
    constexpr float velocityGrab = 8.0f;  // how close to a velocity bar a click takes it
    constexpr float dragThreshold = 4.0f; // less than this is a click, not a drag
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
    setWantsKeyboardFocus (true);
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
    if (now.channel != shown.channel || now.pattern != shown.pattern)
        selection.clear();
    shown = now;
    std::erase_if (selection,
                   [this] (const std::pair<int, int>& key)
                   {
                       return std::none_of (shown.notes.begin(), shown.notes.end(), [&key] (const Note& n)
                                            { return n.start == key.first && n.pitch == key.second; });
                   });
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
    velocityLane = area.removeFromBottom (velocityLaneHeight);
    velocityLabel = velocityLane.removeFromLeft (keyboardWidth);
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

// --- selection ------------------------------------------------------------------------------------

bool PianoRollView::isSelected (const Note& note) const noexcept
{
    return std::find (selection.begin(), selection.end(), std::pair { note.start, note.pitch }) != selection.end();
}

juce::String PianoRollView::selectionList() const
{
    juce::StringArray items;
    for (const auto& [at, pitch] : selection)
        items.add (juce::String (at) + ":" + juce::String (pitch));
    return items.joinIntoString (" ");
}

int PianoRollView::velocityFromY (float y) const noexcept
{
    const auto top = velocityLane.getY() + space::unit * 2.0f, bottom = velocityLane.getBottom() - space::unit;
    return std::clamp (juce::roundToInt ((bottom - y) / (bottom - top) * 127.0f), 0, 127);
}

int PianoRollView::velocityBarAt (juce::Point<float> position) const noexcept
{
    if (! velocityLane.contains (position) || position.x < grid.getX())
        return -1;
    auto best = -1;
    auto bestDistance = velocityGrab;
    for (size_t i = 0; i < shown.notes.size(); ++i)
    {
        const auto distance = std::abs (tickToX (shown.notes[i].start) + 1.5f - position.x);
        // Selected notes win a tie, so a bar in a crowd can still be reached by selecting it first.
        if (distance < bestDistance || (distance <= bestDistance && isSelected (shown.notes[i])))
        {
            best = static_cast<int> (i);
            bestDistance = distance;
        }
    }
    return best;
}

// --- painting -------------------------------------------------------------------------------------

void PianoRollView::paint (juce::Graphics& g)
{
    const auto ink = lnf.colour (&P::ink), line = lnf.colour (&P::line), muted = lnf.colour (&P::inkMuted),
               bg = lnf.colour (&P::bg);

    // How a note looks right now: moved with the selection while dragging, or with its new length/velocity.
    auto shownAs = [this] (Note note)
    {
        if (drag == Drag::move && isSelected (note))
        {
            note.start += moveTicks;
            note.pitch += moveSemitones;
        }
        else if ((drag == Drag::resize || drag == Drag::velocity) && note == dragOriginal)
            note = dragNow;
        else if (drag == Drag::velocity && isSelected (dragOriginal) && isSelected (note))
            note.velocity = dragNow.velocity;
        return note;
    };

    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (grid.toNearestInt());

        // Rows: black keys darker, a line under every C.
        for (int pitch = lowestNote; pitch <= highestNote; ++pitch)
        {
            const auto y = pitchToY (pitch);
            if (y + rowHeight < grid.getY() || y > grid.getBottom())
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

        // This channel's notes. Yellow while sounding; accent = a notch on top; slide = a line into the
        // next; selected = a ring inside.
        const auto playTick = shown.currentStep * Note::ticksPerStep;
        for (const auto& original : shown.notes)
        {
            const auto note = shownAs (original);
            const auto area = noteArea (note).reduced (0.0f, 1.0f);
            const auto moving = drag != Drag::none && ! (note == original);
            const auto sounding = ! moving && shown.currentStep >= 0 &&
                                  note.start <= playTick + Note::ticksPerStep - 1 && note.end() > playTick;
            g.setColour (sounding ? lnf.colour (&P::accent) : ink);
            g.fillRoundedRectangle (area, 2.0f);
            if (sounding)
            {
                g.setColour (lnf.colour (&P::accentOutline));
                g.drawRoundedRectangle (area, 2.0f, shape::lineWidth);
            }
            if (isSelected (original))
            {
                g.setColour (bg);
                g.drawRoundedRectangle (area.reduced (2.0f), 1.5f, 1.5f);
            }
            if (note.accent)
            {
                g.setColour (bg);
                g.fillRect (area.getX() + 2.0f, area.getY() + 2.0f, std::max (2.0f, area.getWidth() - 4.0f), 2.0f);
            }
            if (note.slide)
                for (const auto& next : shown.notes) // into the note that starts where this one ends
                    if (next.start == note.end())
                    {
                        g.setColour (ink);
                        g.drawLine (area.getRight(), area.getCentreY(), tickToX (next.start) + 4.0f,
                                    pitchToY (next.pitch) + rowHeight * 0.5f, shape::iconStroke);
                        break;
                    }
            if (area.getWidth() > 30.0f)
            {
                g.setFont (lnf.fonts().font (type::meta).withHeight (10.0f));
                g.setColour (sounding ? lnf.colour (&P::onAccent) : bg);
                g.drawText (core::noteName (note.pitch), area.reduced (5.0f, 0.0f), juce::Justification::centredLeft,
                            false);
            }
        }

        // The selection rectangle while it is being drawn.
        if (drag == Drag::select)
        {
            const float dashes[] { shape::dashLength, shape::dashGap };
            const auto box = juce::Rectangle<float> (dragStart, dragEnd);
            juce::Path outline, dashed;
            outline.addRectangle (box);
            juce::PathStrokeType (shape::lineWidth).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (ink);
            g.fillPath (dashed);
        }

        if (shown.currentStep >= 0) // playhead
        {
            g.setColour (ink);
            g.fillRect (tickToX (playTick), grid.getY(), shape::lineWidth * 2.0f, grid.getHeight());
        }
    }

    // Velocity lane: a bar per note, as tall as it is loud. 100 (unity) is marked by a faint line.
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (velocityLane.toNearestInt());
        g.setColour (line);
        g.fillRect (velocityLane.withHeight (shape::lineWidth));
        const auto top = velocityLane.getY() + space::unit * 2.0f, bottom = velocityLane.getBottom() - space::unit;
        const auto unityY = bottom - (bottom - top) * 100.0f / 127.0f;
        g.setColour (line.withMultipliedAlpha (0.6f));
        g.fillRect (grid.getX(), unityY, grid.getWidth(), shape::lineWidth);

        for (const auto& original : shown.notes)
        {
            const auto note = shownAs (original);
            const auto x = tickToX (note.start) + 1.0f;
            const auto y = bottom - (bottom - top) * static_cast<float> (note.velocity) / 127.0f;
            g.setColour (isSelected (original) || (drag == Drag::velocity && original == dragOriginal) ? ink : muted);
            g.fillRect (x, y, 3.0f, bottom - y);
            g.fillEllipse (x - 2.0f, y - 3.5f, 7.0f, 7.0f);
        }
    }
    g.setFont (lnf.fonts().font (type::meta).withHeight (10.0f));
    g.setColour (muted);
    g.drawText ("velocity", velocityLabel.reduced (space::unit, space::unit * 2.0f), juce::Justification::topRight,
                false);

    // Keyboard: white and black keys, C's named.
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (keyboard.toNearestInt());
        g.setColour (bg);
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
    if (velocityBarAt (e.position) >= 0)
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (index >= 0 && onRightEdge (shown.notes[static_cast<size_t> (index)], e.position))
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoRollView::mouseDown (const juce::MouseEvent& e)
{
    if (session.channelCount() == 0)
        return;
    grabKeyboardFocus(); // Delete, Ctrl+A and Esc act on the selection
    dragStart = dragEnd = e.position;

    if (const auto bar = velocityBarAt (e.position); bar >= 0)
    {
        drag = Drag::velocity;
        dragOriginal = dragNow = shown.notes[static_cast<size_t> (bar)];
        dragNow.velocity = velocityFromY (e.position.y);
        repaint();
        return;
    }
    if (! grid.contains (e.position))
        return;

    const auto index = noteAt (e.position);
    if (index < 0)
    {
        if (! e.mods.isPopupMenu())
            drag = Drag::select; // a click adds a note, a drag selects: decided on release
        return;
    }

    const auto note = shown.notes[static_cast<size_t> (index)];
    auto args = core::CommandArgs { { "at", note.start }, { "note", note.pitch } };
    if (e.mods.isPopupMenu())
    {
        run ("note.remove", args);
        return;
    }
    if (e.mods.isShiftDown())
    {
        args.set ("on", ! note.accent);
        run ("note.accent", args);
        return;
    }
    if (e.mods.isCtrlDown())
    {
        args.set ("on", ! note.slide);
        run ("note.slide", args);
        return;
    }

    dragOriginal = dragNow = note;
    if (onRightEdge (note, e.position))
        drag = Drag::resize;
    else
    {
        // Grabbing an unselected note moves it alone; grabbing a selected one moves them all.
        if (! isSelected (note))
            selection = { { note.start, note.pitch } };
        drag = Drag::move;
        moveTicks = moveSemitones = 0;
    }
    repaint();
}

void PianoRollView::mouseDrag (const juce::MouseEvent& e)
{
    dragEnd = e.position;
    const auto deltaTicks = xToTick (e.position.x) - xToTick (dragStart.x);
    const auto lastTick = shown.length * Note::ticksPerStep - 1;

    switch (drag)
    {
    case Drag::none:
        return;
    case Drag::select:
        break;
    case Drag::velocity:
        dragNow.velocity = velocityFromY (e.position.y);
        break;
    case Drag::resize:
        dragNow.length = std::clamp (snapped (dragOriginal.length + deltaTicks), snapTicks (snap),
                                     core::Pattern::maxSteps * Note::ticksPerStep);
        break;
    case Drag::move:
    {
        // The grabbed note lands on the snap; the others keep their distance from it. Nothing leaves the
        // pattern or the range.
        auto ticks = snapped (dragOriginal.start + deltaTicks) - dragOriginal.start;
        auto semitones = yToPitch (e.position.y) - yToPitch (dragStart.y);
        for (const auto& [at, pitch] : selection)
        {
            ticks = std::clamp (ticks, -at, lastTick - at);
            semitones = std::clamp (semitones, lowestNote - pitch, highestNote - pitch);
        }
        moveTicks = ticks;
        moveSemitones = semitones;
        break;
    }
    }
    repaint();
}

void PianoRollView::addNoteAt (juce::Point<float> position)
{
    // On the snap, as long as the last note touched.
    const auto step = snapTicks (snap);
    const auto start = std::clamp (static_cast<int> (std::floor (xToTick (position.x) / step)) * step, 0,
                                   shown.length * Note::ticksPerStep - 1);
    run ("note.add", { { "at", start }, { "note", yToPitch (position.y) }, { "length", newNoteLength } });
}

void PianoRollView::mouseUp (const juce::MouseEvent& e)
{
    const auto what = drag;
    drag = Drag::none;
    const auto args = core::CommandArgs { { "at", dragOriginal.start }, { "note", dragOriginal.pitch } };

    switch (what)
    {
    case Drag::none:
        return;
    case Drag::select:
    {
        if (dragStart.getDistanceFrom (e.position) < dragThreshold)
        {
            selection.clear();
            addNoteAt (e.position);
            return;
        }
        // Every note the rectangle touches; Shift adds to what was selected.
        if (! e.mods.isShiftDown())
            selection.clear();
        const auto box = juce::Rectangle<float> (dragStart, e.position);
        for (const auto& n : shown.notes)
            if (box.intersects (noteArea (n)) && ! isSelected (n))
                selection.push_back ({ n.start, n.pitch });
        break;
    }
    case Drag::velocity:
    {
        if (dragNow.velocity == dragOriginal.velocity)
            break;
        if (isSelected (dragOriginal) && selection.size() > 1)
            run ("notes.velocity", { { "notes", selectionList() }, { "velocity", dragNow.velocity } });
        else
        {
            auto withVelocity = args;
            withVelocity.set ("velocity", dragNow.velocity);
            run ("note.velocity", withVelocity);
        }
        break;
    }
    case Drag::resize:
    {
        if (dragNow.length == dragOriginal.length)
            break;
        auto withLength = args;
        withLength.set ("length", dragNow.length);
        run ("note.resize", withLength);
        newNoteLength = dragNow.length;
        break;
    }
    case Drag::move:
    {
        if (moveTicks == 0 && moveSemitones == 0)
            break;
        if (selection.size() == 1)
        {
            auto moved = args;
            moved.set ("toAt", dragOriginal.start + moveTicks);
            moved.set ("toNote", dragOriginal.pitch + moveSemitones);
            run ("note.move", moved);
        }
        else
            run ("notes.move", { { "notes", selectionList() }, { "by", moveTicks }, { "semitones", moveSemitones } });
        for (auto& [at, pitch] : selection) // the selection follows the notes
            at += moveTicks, pitch += moveSemitones;
        newNoteLength = dragOriginal.length;
        break;
    }
    }
    moveTicks = moveSemitones = 0;
    repaint();
}

bool PianoRollView::keyPressed (const juce::KeyPress& key)
{
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && ! selection.empty())
    {
        run ("notes.remove", { { "notes", selectionList() } });
        selection.clear();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selection.clear();
        for (const auto& n : shown.notes)
            selection.push_back ({ n.start, n.pitch });
        repaint();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && ! selection.empty())
    {
        selection.clear();
        repaint();
        return true;
    }
    return false; // everything else (Space, F-keys...) is the window's
}

void PianoRollView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    scrollBy (-wheel.deltaY * rowHeight * 12.0f);
}
} // namespace tekno::ui
