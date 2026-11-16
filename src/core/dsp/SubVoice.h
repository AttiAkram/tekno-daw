// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

namespace tekno::core::dsp
{
/** The sub bass: a sine with a "punch" (the pitch starts high and falls in ~30 ms, like a kick's sweep),
    a tanh saturation that adds the harmonics small speakers need to make it heard, and a decay.

    Monophonic, with the acid voice's note, slide and gate calls, so the engine plays both the same way:
    gate length, slides, ties and live legato. Real-time safe: no allocation, no locks.

    Controls are normalised 0..1 (SubParams), except glide, in seconds. */
class SubVoice
{
public:
    struct Controls
    {
        float drive = 0.3f;  // saturation: 0 = a pure sine
        float punch = 0.4f;  // how far above the note it starts (up to two octaves)
        float decay = 0.5f;  // how long a held note lasts: 0 = a pluck, 1 = as long as it is held
        float glide = 0.05f; // slide time, seconds
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** New targets; drive and punch are smoothed per sample so knob moves don't click. */
    void setControls (const Controls& controls) noexcept { target = controls; }

    void noteOn (int midiNote, bool accented) noexcept; // a new attack: punch and level start again
    void glideTo (int midiNote) noexcept;               // slide: the pitch moves, the note carries on
    void gateOff() noexcept;
    bool isGateOn() const noexcept { return gate; }

    float process() noexcept;

    // Mappings, public for the tests and the UI.
    static float decaySeconds (float normalised) noexcept; // 0.08 s .. ~4 s
    static bool decayHolds (float normalised) noexcept;    // at the top of the knob the note doesn't fade
    static constexpr float punchSemitones = 24.0f;         // at punch 1
    static constexpr float punchSeconds = 0.03f;

private:
    double sampleRate = 48000.0;
    Controls target, smoothed;
    float smoothing = 0.0f;

    double phase = 0.0;
    float pitch = 33.0f, targetPitch = 33.0f; // semitones (MIDI note numbers)
    float glide = 0.0f, glideSeconds = 0.0f;
    bool sliding = false;

    bool gate = false, accentedNote = false;
    float punchEnv = 0.0f, punchDecay = 0.0f;
    float amp = 0.0f, level = 0.0f, levelDecay = 1.0f;
    float attack = 0.0f, release = 0.0f;
};
} // namespace tekno::core::dsp
