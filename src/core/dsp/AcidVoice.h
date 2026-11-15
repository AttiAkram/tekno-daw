// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include "core/dsp/LadderFilter.h"

namespace tekno::core::dsp
{
/** The 303-style monophonic voice: oscillator -> resonant lowpass -> amp -> drive, with a decaying
    filter envelope, accent and slide. Real-time safe: no allocation, no locks.

    The drive is the acid "pedal": a tanh waveshaper with first-order anti-derivative antialiasing
    (ADAA), so it can be pushed hard without the fizz of aliasing, then a tone filter that darkens as
    the drive rises. At drive 0 it is the old soft ceiling.

    All controls are normalised 0..1 (AcidParams); the mapping to Hz and seconds lives here. */
class AcidVoice
{
public:
    struct Controls
    {
        float cutoff = 0.35f;
        float resonance = 0.8f;
        float envMod = 0.6f;
        float decay = 0.45f;
        float accent = 0.7f;
        float shape = 0.0f; // 0 saw .. 1 square
        float drive = 0.0f;
        float glide = 0.035f; // slide time, seconds
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** New targets; they are smoothed per sample so knob moves don't click. */
    void setControls (const Controls& controls) noexcept { target = controls; }

    void noteOn (int midiNote, bool accented) noexcept; // retriggers the envelopes
    void glideTo (int midiNote) noexcept;               // slide: pitch moves, envelopes carry on
    void gateOff() noexcept;
    bool isGateOn() const noexcept { return gate; }

    float process() noexcept;

    // Mappings, public so tests and the UI can show real units.
    static float cutoffHz (float normalised) noexcept;     // 40 Hz .. ~14.5 kHz, exponential
    static float decaySeconds (float normalised) noexcept; // 0.15 s .. 2.5 s

private:
    double sampleRate = 48000.0;
    LadderFilter filter;

    Controls target, smoothed;
    float smoothing = 0.0f;

    double phase = 0.0;
    float pitch = 45.0f, targetPitch = 45.0f; // in semitones (MIDI note numbers)
    float glide = 0.0f;
    float glideSeconds = 0.0f; // what `glide` was computed for
    bool sliding = false;

    bool gate = false;
    bool accentedNote = false;
    float filterEnv = 0.0f, filterEnvDecay = 0.0f;
    float amp = 0.0f, ampHold = 0.0f, ampHoldDecay = 0.0f;
    float attack = 0.0f, release = 0.0f;

    double shaperPrevious = 0.0; // last input of the ADAA waveshaper
    float toneState = 0.0f;

    float shape (double input) noexcept;
};
} // namespace tekno::core::dsp
