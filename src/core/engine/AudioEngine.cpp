// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/engine/AudioEngine.h"

namespace tekno::core
{
AudioEngine::AudioEngine (Session& sessionToPlay)
    : session (sessionToPlay)
{
    prepare (sampleRate);
}

void AudioEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    voice.setControls (readControls());
    voice.prepare (sampleRate);
    wasPlaying = false;
    step = -1;
    lastStep = {};
    samplesIntoStep = 0.0;
    currentStepLength = 0.0;
    holdGate = gateReleased = false;
    session.transport.currentStep.store (-1);
}

dsp::AcidVoice::Controls AudioEngine::readControls() const noexcept
{
    const auto& p = session.synth;
    constexpr auto relaxed = std::memory_order_relaxed;
    return { p.cutoff.load (relaxed), p.resonance.load (relaxed), p.envMod.load (relaxed),
             p.decay.load (relaxed),  p.accent.load (relaxed),    p.square.load (relaxed) };
}

void AudioEngine::render (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    auto& transport = session.transport;

    const auto playing = transport.playing.load (std::memory_order_relaxed);
    const auto bpm =
        std::clamp (transport.bpm.load (std::memory_order_relaxed), TransportState::minBpm, TransportState::maxBpm);
    const auto straightLength = samplesPerStep (sampleRate, bpm);

    voice.setControls (readControls());

    if (playing && ! wasPlaying)
    {
        step = -1;
        lastStep = {};
        currentStepLength = 0.0;
        samplesIntoStep = 0.0; // the very first sample starts step 0
    }
    else if (! playing && wasPlaying)
    {
        voice.gateOff();
        step = -1;
        transport.currentStep.store (-1, std::memory_order_relaxed);
    }
    wasPlaying = playing;

    for (int i = 0; i < numSamples; ++i)
    {
        if (playing)
        {
            if (samplesIntoStep >= currentStepLength)
            {
                samplesIntoStep = std::max (0.0, samplesIntoStep - currentStepLength);
                startStep (step + 1, straightLength);
                if (samplesIntoStep >= currentStepLength) // tempo jumped up a lot: don't fire twice in a row
                    samplesIntoStep = 0.0;
            }

            if (! gateReleased && ! holdGate && samplesIntoStep >= currentStepLength * gateLength)
            {
                voice.gateOff();
                gateReleased = true;
            }
            samplesIntoStep += 1.0;
        }

        const auto sample = voice.process();
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                outputs[ch][i] = sample;
    }
}

void AudioEngine::startStep (int index, double straightLength) noexcept
{
    // Wrap, and switch pattern on the wrap: the pattern being edited takes over at the bar line.
    if (step < 0 || index >= session.pattern (patternIndex).getLength())
    {
        index = 0;
        patternIndex = session.selected.load (std::memory_order_relaxed);
        session.playing.store (patternIndex, std::memory_order_relaxed);
    }

    const auto& pattern = session.pattern (patternIndex);
    const auto current = pattern.getStep (index);
    const auto gateOpen = step >= 0 && voice.isGateOn();
    const auto slidingIn = gateOpen && lastStep.on && lastStep.slide;
    const auto tiedIn = gateOpen && current.tie;

    step = index;
    lastStep = current;
    session.transport.currentStep.store (index, std::memory_order_relaxed);
    currentStepLength =
        swungStepLength (straightLength, session.transport.swing.load (std::memory_order_relaxed), index);
    gateReleased = false;

    if (current.on)
    {
        if (tiedIn)
        {
            // hold the previous note: no new pitch, no retrigger
        }
        else if (slidingIn)
            voice.glideTo (current.note); // no retrigger: the slide is one continuous gesture
        else
            voice.noteOn (current.note, current.accent);
    }
    else
    {
        voice.gateOff();
        gateReleased = true;
    }

    const auto nextIndex = index + 1 < pattern.getLength() ? index + 1 : 0;
    const auto& nextPattern =
        nextIndex == 0 ? session.pattern (session.selected.load (std::memory_order_relaxed)) : pattern;
    const auto next = nextPattern.getStep (nextIndex);
    holdGate = current.on && next.on && (current.slide || next.tie);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    render (outputs, numOutputs, numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepare (device != nullptr ? device->getCurrentSampleRate() : 48000.0);
}

void AudioEngine::audioDeviceStopped()
{
    session.transport.currentStep.store (-1);
}
} // namespace tekno::core
