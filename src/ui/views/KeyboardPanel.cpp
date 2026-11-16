// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/KeyboardPanel.h"

#include "core/model/NoteNames.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;

    /** A PC key and the semitone above Z's note it plays. */
    struct KeyNote
    {
        char key;
        int semitone;
    };
    // Trackers and FL Studio: lower octave on the bottom row (black keys on the row above), upper octave
    // on the top row (black keys on the numbers). The ends overlap by a few notes, as in those programs.
    constexpr KeyNote layout[] {
        { 'z', 0 },  { 's', 1 },  { 'x', 2 },  { 'd', 3 },  { 'c', 4 },  { 'v', 5 },  { 'g', 6 },  { 'b', 7 },
        { 'h', 8 },  { 'n', 9 },  { 'j', 10 }, { 'm', 11 }, { ',', 12 }, { 'l', 13 }, { '.', 14 }, { 'q', 12 },
        { '2', 13 }, { 'w', 14 }, { '3', 15 }, { 'e', 16 }, { 'r', 17 }, { '5', 18 }, { 't', 19 }, { '6', 20 },
        { 'y', 21 }, { '7', 22 }, { 'u', 23 }, { 'i', 24 }, { '9', 25 }, { 'o', 26 }, { '0', 27 }, { 'p', 28 },
    };
    constexpr int span = 29; // Z's note .. P's note

    bool isBlack (int semitone) noexcept
    {
        const auto pc = semitone % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    /** The label on a piano key: the PC keys that play it, lower row first. */
    juce::String keysFor (int semitone)
    {
        juce::String text;
        for (const auto& k : layout)
            if (k.semitone == semitone)
                text << (text.isEmpty() ? "" : " ")
                     << juce::String::charToString (static_cast<juce::juce_wchar> (k.key)).toUpperCase();
        return text;
    }
} // namespace

KeyboardPanel::KeyboardPanel (TeknoLookAndFeel& teknoLookAndFeel, core::Session& sessionToPlay,
                              core::LiveInput& liveInput, std::function<AudioStatus()> audioStatusProvider,
                              std::function<juce::String()> midiStatusProvider)
    : lnf (teknoLookAndFeel)
    , session (sessionToPlay)
    , live (liveInput)
    , audioStatus (std::move (audioStatusProvider))
    , midiStatus (std::move (midiStatusProvider))
    , octaveChip (minOctave, maxOctave, 2, [] (double o) { return "octave " + juce::String (juce::roundToInt (o)); })
    , velocityChip (1, 127, 100, [] (double v) { return "velocity " + juce::String (juce::roundToInt (v)); })
{
    octaveChip.onChange = [this] (double o) { setOctave (juce::roundToInt (o)); };
    velocityChip.onChange = [this] (double v) { setVelocity (juce::roundToInt (v)); };
    gateButton.onClick = [this] { setShortGate (! shortGate); };
    for (auto* c : std::initializer_list<juce::Component*> { &octaveChip, &velocityChip, &gateButton })
    {
        c->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (c);
    }
    setShortGate (false);
    startTimerHz (30);
}

KeyboardPanel::~KeyboardPanel()
{
    allOff();
}

std::optional<int> KeyboardPanel::semitoneFor (juce::juce_wchar key) noexcept
{
    const auto lower = juce::CharacterFunctions::toLowerCase (key);
    for (const auto& k : layout)
        if (static_cast<juce::juce_wchar> (k.key) == lower)
            return k.semitone;
    return std::nullopt;
}

// --- settings -----------------------------------------------------------------------------------------

int KeyboardPanel::getOctave() const noexcept
{
    return octaves[targetIsSampler() ? 1 : 0];
}

void KeyboardPanel::setOctave (int octave)
{
    octaves[targetIsSampler() ? 1 : 0] = std::clamp (octave, minOctave, maxOctave);
    octaveChip.setValue (getOctave());
    repaint();
    if (onSettingsChanged)
        onSettingsChanged();
}

void KeyboardPanel::setOctaves (int acidOctave, int samplerOctave)
{
    octaves = { std::clamp (acidOctave, minOctave, maxOctave), std::clamp (samplerOctave, minOctave, maxOctave) };
    octaveChip.setValue (getOctave());
    repaint();
}

void KeyboardPanel::setVelocity (int newVelocity)
{
    velocity = std::clamp (newVelocity, 1, 127);
    velocityChip.setValue (velocity);
    repaint();
    if (onSettingsChanged)
        onSettingsChanged();
}

void KeyboardPanel::setShortGate (bool on)
{
    shortGate = on;
    gateButton.setButtonText (on ? "gate: short" : "gate: hold");
    if (onSettingsChanged)
        onSettingsChanged();
}

int KeyboardPanel::targetChannel() const noexcept
{
    return std::clamp (session.selectedChannel.load(), 0, std::max (0, session.channelCount() - 1));
}

bool KeyboardPanel::targetIsSampler() const noexcept
{
    return session.channelCount() > 0 && session.channel (targetChannel()).kind.load() == core::InstrumentKind::sampler;
}

int KeyboardPanel::lowestPitch() const noexcept
{
    return (getOctave() + 1) * 12; // scientific octaves: C4 = 60
}

// --- playing ------------------------------------------------------------------------------------------

void KeyboardPanel::noteOn (int keyCode, int pitch, bool accent)
{
    if (session.channelCount() == 0 || pitch < 0 || pitch > 127)
        return;
    Held h { keyCode, pitch, targetChannel() };
    core::LiveInput::Event e;
    e.type = core::LiveInput::Event::Type::noteOn;
    e.channel = static_cast<std::int8_t> (h.channel);
    e.pitch = static_cast<std::uint8_t> (pitch);
    e.velocity = static_cast<std::uint8_t> (velocity);
    e.accent = accent;
    e.shortGate = shortGate;
    e.timeMs = juce::Time::getMillisecondCounterHiRes();
    if (live.push (core::LiveInput::Source::ui, e))
        held.push_back (h);
    repaint (pianoArea.toNearestInt());
}

void KeyboardPanel::noteOff (const Held& h)
{
    core::LiveInput::Event e;
    e.type = core::LiveInput::Event::Type::noteOff;
    e.channel = static_cast<std::int8_t> (h.channel);
    e.pitch = static_cast<std::uint8_t> (h.pitch);
    e.timeMs = juce::Time::getMillisecondCounterHiRes();
    live.push (core::LiveInput::Source::ui, e);
    repaint (pianoArea.toNearestInt());
}

bool KeyboardPanel::keyDown (const juce::KeyPress& key)
{
    auto code =
        static_cast<int> (juce::CharacterFunctions::toLowerCase (static_cast<juce::juce_wchar> (key.getKeyCode())));
    const auto mods = key.getModifiers();
    if (mods.isCommandDown() || mods.isAltDown())
        return false; // Ctrl and Alt combinations stay with the keymap
    const auto isHeld = [this] (int keyCode)
    { return std::any_of (held.begin(), held.end(), [keyCode] (const Held& h) { return h.keyCode == keyCode; }); };

    auto semitone = semitoneFor (static_cast<juce::juce_wchar> (code));
    if (! semitone && mods.isShiftDown())
    {
        // Shift (the accent) turns 2 into " on an Italian keyboard and into @ on an American one, and
        // the key code is that character. Which note key it was is then the one physically down, which
        // the system knows by its unshifted character, whatever the layout.
        for (const auto& k : layout)
            if (! isHeld (k.key) && juce::KeyPress::isKeyCurrentlyDown (k.key))
            {
                code = k.key;
                semitone = k.semitone;
                break;
            }
    }
    if (! semitone)
        return false;
    if (! isHeld (code)) // the system repeats a held key: one note per press
        noteOn (code, lowestPitch() + *semitone, mods.isShiftDown());
    return true;
}

void KeyboardPanel::keysChanged()
{
    for (auto it = held.begin(); it != held.end();)
        if (it->keyCode >= 0 && ! juce::KeyPress::isKeyCurrentlyDown (it->keyCode))
        {
            noteOff (*it);
            it = held.erase (it);
        }
        else
            ++it;
}

void KeyboardPanel::allOff()
{
    held.clear();
    core::LiveInput::Event e;
    e.type = core::LiveInput::Event::Type::allOff;
    e.timeMs = juce::Time::getMillisecondCounterHiRes();
    live.push (core::LiveInput::Source::ui, e);
    repaint();
}

void KeyboardPanel::visibilityChanged()
{
    if (! isVisible() && ! held.empty())
        allOff(); // nothing keeps sounding from a panel nobody can see
}

void KeyboardPanel::timerCallback()
{
    if (! held.empty())
        keysChanged(); // a key released while the window had no focus sends no event: look for it
    if (! octaveChip.isBeingDragged() && juce::roundToInt (octaveChip.getValue()) != getOctave())
        octaveChip.setValue (getOctave()); // the selected channel changed instrument
    if (isShowing())
        repaint (infoArea.toNearestInt());
}

bool KeyboardPanel::isSounding (int pitch) const noexcept
{
    return std::any_of (held.begin(), held.end(), [pitch] (const Held& h) { return h.pitch == pitch; });
}

// --- the piano ----------------------------------------------------------------------------------------

juce::Rectangle<float> KeyboardPanel::keyArea (int semitone) const
{
    // White keys share the width; a black key sits across the line between two whites.
    auto whites = 0, whiteIndex = 0;
    for (int s = 0; s < span; ++s)
        if (! isBlack (s))
        {
            if (s < semitone || (s == semitone))
                whiteIndex = whites;
            ++whites;
        }
    const auto width = pianoArea.getWidth() / static_cast<float> (whites);
    if (! isBlack (semitone))
        return { pianoArea.getX() + width * static_cast<float> (whiteIndex), pianoArea.getY(), width,
                 pianoArea.getHeight() };
    auto before = 0; // white keys left of this black one
    for (int s = 0; s < semitone; ++s)
        before += isBlack (s) ? 0 : 1;
    return juce::Rectangle<float> (width * 0.6f, pianoArea.getHeight() * 0.6f)
        .withCentre ({ pianoArea.getX() + width * static_cast<float> (before),
                       pianoArea.getY() + pianoArea.getHeight() * 0.3f });
}

int KeyboardPanel::pitchAt (juce::Point<float> position) const
{
    if (! pianoArea.contains (position))
        return -1;
    for (int s = 0; s < span; ++s) // black keys first: they are on top
        if (isBlack (s) && keyArea (s).contains (position))
            return lowestPitch() + s;
    for (int s = 0; s < span; ++s)
        if (! isBlack (s) && keyArea (s).contains (position))
            return lowestPitch() + s;
    return -1;
}

void KeyboardPanel::mouseDown (const juce::MouseEvent& e)
{
    if (const auto pitch = pitchAt (e.position); pitch >= 0)
        noteOn (-1, pitch, e.mods.isShiftDown());
}

void KeyboardPanel::mouseDrag (const juce::MouseEvent& e)
{
    // Sliding across the keys: on an acid channel each new key slides from the last (legato).
    const auto pitch = pitchAt (e.position);
    auto mouse = std::find_if (held.begin(), held.end(), [] (const Held& h) { return h.keyCode < 0; });
    if (mouse != held.end() && mouse->pitch == pitch)
        return;
    const auto previous = mouse != held.end() ? std::optional<Held> (*mouse) : std::nullopt;
    if (mouse != held.end())
        held.erase (mouse);
    if (pitch >= 0)
        noteOn (-1, pitch, e.mods.isShiftDown()); // on first, so the voice slides instead of stopping
    if (previous)
        noteOff (*previous);
}

void KeyboardPanel::mouseUp (const juce::MouseEvent&)
{
    for (auto it = held.begin(); it != held.end();)
        if (it->keyCode < 0)
        {
            noteOff (*it);
            it = held.erase (it);
        }
        else
            ++it;
}

void KeyboardPanel::resized()
{
    auto area = getLocalBounds().toFloat();
    auto controls = area.removeFromTop (shape::pillHeight);
    octaveChip.setBounds (controls.removeFromLeft (128.0f).toNearestInt());
    controls.removeFromLeft (space::gap);
    velocityChip.setBounds (controls.removeFromLeft (144.0f).toNearestInt());
    controls.removeFromLeft (space::gap);
    gateButton.setBounds (controls.removeFromLeft (128.0f).toNearestInt());
    area.removeFromTop (space::gap);
    infoArea = area.removeFromTop (22.0f + 2 * 18.0f);
    area.removeFromTop (space::gap);
    pianoArea = area.withHeight (std::min (area.getHeight(), 220.0f));
}

void KeyboardPanel::paint (juce::Graphics& g)
{
    const auto ink = lnf.colour (&P::ink), muted = lnf.colour (&P::inkMuted), line = lnf.colour (&P::line),
               bg = lnf.colour (&P::bg);

    // What is played, where, and how late.
    {
        auto info = infoArea;
        const auto channel = targetChannel();
        const auto name = session.channelCount() > 0 ? session.project.channelNames[static_cast<size_t> (channel)]
                                                     : juce::String ("-");
        g.setFont (lnf.fonts().font (type::body));
        g.setColour (ink);
        g.drawText ("playing " + name +
                        (targetIsSampler()
                             ? juce::String ("  (drums: one hit per key)")
                             : juce::String ("  (") + core::instrumentName (session.channel (channel).kind.load()) +
                                   ": mono, keys held together slide)"),
                    info.removeFromTop (22.0f), juce::Justification::centredLeft, true);

        juce::String sound;
        const auto audio = audioStatus ? audioStatus() : AudioStatus {};
        if (audio.running && audio.sampleRate > 0.0)
        {
            const auto bufferMs = 1000.0 * audio.bufferSize / audio.sampleRate;
            const auto outputMs = 1000.0 * audio.outputLatency / audio.sampleRate;
            const auto waitMs = static_cast<double> (live.lastWaitMs.load()); // the last note's: it varies
            const auto maxWaitMs = static_cast<double> (live.maxWaitMs.load());
            sound << audio.driver << ", " << juce::roundToInt (audio.sampleRate) << " Hz, buffer " << audio.bufferSize
                  << "   /   latency about " << juce::String (waitMs + bufferMs + outputMs, 1) << " ms: wait "
                  << juce::String (waitMs, 1) << " + buffer " << juce::String (bufferMs, 1) << " + device "
                  << juce::String (outputMs, 1) << (audio.outputLatencyGuessed ? " (estimated)" : "")
                  << "   /   longest wait " << juce::String (maxWaitMs, 1) << " ms";
            if (audio.dropouts >= 0)
                sound << "   /   dropouts " << audio.dropouts;
        }
        else
            sound << "no audio output: " << audio.problem;

        juce::String midi = "MIDI: " + (midiStatus ? midiStatus() : juce::String ("-"));
        midi << "   /   " << static_cast<int> (live.notesReceived (core::LiveInput::Source::midi))
             << " MIDI notes received";

        g.setFont (lnf.fonts().font (type::meta));
        g.setColour (muted);
        g.drawText (sound, info.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        g.drawText (midi, info.removeFromTop (18.0f), juce::Justification::centredLeft, true);
    }

    // The piano: whites, then blacks on top, light and dark as on a real one in either theme. What sounds
    // is yellow, like the steps that sound.
    const auto light = ink.getPerceivedBrightness() > bg.getPerceivedBrightness() ? ink : bg;
    const auto dark = light == ink ? bg : ink;
    const auto base = lowestPitch();
    for (int pass = 0; pass < 2; ++pass)
        for (int s = 0; s < span; ++s)
        {
            if (isBlack (s) != (pass == 1))
                continue;
            auto area = keyArea (s).reduced (1.0f, 0.0f);
            const auto sounding = isSounding (base + s);
            g.setColour (sounding ? lnf.colour (&P::accent) : (isBlack (s) ? dark : light));
            g.fillRoundedRectangle (area, shape::stepRadius);
            g.setColour (sounding ? lnf.colour (&P::accentOutline) : line);
            g.drawRoundedRectangle (area, shape::stepRadius, shape::lineWidth);

            g.setFont (lnf.fonts().font (type::control));
            g.setColour (sounding ? lnf.colour (&P::onAccent) : (isBlack (s) ? light : dark));
            g.drawText (keysFor (s), area.withTrimmedBottom (isBlack (s) ? 8.0f : 28.0f).removeFromBottom (18.0f),
                        juce::Justification::centred, false);
            if (! isBlack (s) && (base + s) % 12 == 0) // the C's (Do) named at the bottom
            {
                g.setFont (lnf.fonts().font (type::meta));
                g.setColour (sounding ? lnf.colour (&P::onAccent) : dark);
                g.drawText (core::noteName (base + s), area.removeFromBottom (24.0f), juce::Justification::centred,
                            false);
            }
        }
}
} // namespace tekno::ui
