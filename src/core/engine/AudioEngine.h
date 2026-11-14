// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "core/dsp/AcidVoice.h"
#include "core/model/Session.h"

namespace tekno::core
{
/** Plays a Session: the step sequencer and the acid voice, sample by sample.

    The same render() runs inside the audio device callback and offline (tests, rendering to a
    file): the engine never depends on a device, a window or the wall clock.

    Timing is sample-accurate: step boundaries and gate-offs are checked on every sample, so they
    land on the exact sample whatever the buffer size. */
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    /** Fraction of a step the gate stays open when the step does not slide. */
    static constexpr double gateLength = 0.55;

    explicit AudioEngine (Session& sessionToPlay);

    void prepare (double sampleRate) noexcept;
    double getSampleRate() const noexcept { return sampleRate; }

    /** Writes numSamples into every output channel (the synth is mono). Real-time safe. */
    void render (float* const* outputs, int numOutputs, int numSamples) noexcept;

    static double samplesPerStep (double sampleRate, double bpm) noexcept { return sampleRate * 60.0 / bpm / 4.0; }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void startStep (int index) noexcept;
    dsp::AcidVoice::Controls readControls() const noexcept;

    Session& session;
    dsp::AcidVoice voice;
    double sampleRate = 48000.0;

    bool wasPlaying = false;
    int step = -1;
    double samplesIntoStep = 0.0;
    bool holdGate = false; // this step slides into the next: keep the gate open
    bool gateReleased = false;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
} // namespace tekno::core
