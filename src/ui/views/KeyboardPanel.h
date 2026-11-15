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
#include <optional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/engine/LiveInput.h"
#include "core/model/Session.h"
#include "ui/AudioStatus.h"
#include "ui/components/ValueChip.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** Play the selected channel live, from the PC keyboard, the mouse, or (through the app) a MIDI keyboard.

    The PC keyboard is laid out as in trackers and FL Studio, two octaves: Z S X D C V G B H N J M , L .
    for the lower one (white keys on the bottom row, black ones above) and Q 2 W 3 E R 5 T 6 Y 7 U I 9 O
    0 P for the upper one. Shift + a letter plays an accent. The shell hands key presses here while this
    panel is the active one; releases are noticed from the key states (and a timer, for a release that
    happens while the window doesn't have the focus).

    Notes go straight into the engine's LiveInput queue: they are events, not edits. The octave (one for
    acid channels, one for drums), the velocity and the gate mode are settings, changed by live.*
    commands, so keys can be bound to them. */
class KeyboardPanel final : public juce::Component, private juce::Timer
{
public:
    KeyboardPanel (TeknoLookAndFeel& lookAndFeel, core::Session& session, core::LiveInput& live,
                   std::function<AudioStatus()> audioStatus, std::function<juce::String()> midiStatus);
    ~KeyboardPanel() override;

    /** A key went down while this panel is active: true when it was a note key (repeats included). */
    bool keyDown (const juce::KeyPress& key);
    /** Some key went up or down: release the notes whose keys are no longer held. */
    void keysChanged();
    /** Everything off, now (Esc, or the panel going away). */
    void allOff();

    // Settings, for the live.* commands.
    int getOctave() const noexcept; // for the selected channel's instrument
    void setOctave (int octave);
    int getVelocity() const noexcept { return velocity; }
    void setVelocity (int newVelocity);
    bool isShortGate() const noexcept { return shortGate; }
    void setShortGate (bool on);
    void setOctaves (int acidOctave, int samplerOctave);
    int getAcidOctave() const noexcept { return octaves[0]; }
    int getSamplerOctave() const noexcept { return octaves[1]; }
    std::function<void()> onSettingsChanged; // the app saves them

    static constexpr int minOctave = 0, maxOctave = 7;

    /** The semitone above Z's note a PC key plays (case doesn't matter), or nothing. */
    static std::optional<int> semitoneFor (juce::juce_wchar key) noexcept;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    struct Held
    {
        int keyCode; // the PC key holding it, or -1 for the mouse
        int pitch;
        int channel;
    };

    void timerCallback() override;
    void noteOn (int keyCode, int pitch, bool accent);
    void noteOff (const Held& held);
    int targetChannel() const noexcept;
    bool targetIsSampler() const noexcept;
    int lowestPitch() const noexcept; // the note of Z
    int pitchAt (juce::Point<float> position) const;
    juce::Rectangle<float> keyArea (int semitone) const; // semitone from Z's note
    bool isSounding (int pitch) const noexcept;

    TeknoLookAndFeel& lnf;
    core::Session& session;
    core::LiveInput& live;
    std::function<AudioStatus()> audioStatus;
    std::function<juce::String()> midiStatus;

    std::array<int, 2> octaves { 2, 4 }; // acid (Z = C2), sampler (Z = C4: the sample as it is)
    int velocity = 100;
    bool shortGate = false;
    std::vector<Held> held;

    ValueChip octaveChip, velocityChip;
    juce::TextButton gateButton;
    juce::Rectangle<float> pianoArea, infoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardPanel)
};
} // namespace tekno::ui
