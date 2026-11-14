// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>

#include <juce_audio_devices/juce_audio_devices.h>

#include "core/dsp/AcidVoice.h"
#include "core/dsp/Sampler.h"
#include "core/model/Session.h"

namespace tekno::core
{
/** Plays a Session: one step clock for every channel, each channel's instrument, the mixer.

    Patterns: the engine plays `session.playing` and switches to `session.selected` when the
    pattern wraps (or on start), so a pattern change always lands on the bar, like on a groovebox.

    The same render() runs inside the audio device callback and offline (tests, rendering to a
    file): the engine never depends on a device, a window or the wall clock.

    Lanes hold notes (ticks, 24 per step). At each step the engine collects the notes that start in it
    and fires each at its exact sample; the acid gate closes at the note's end minus 45% of its last
    step (55% gate, as on the 303), unless the note slides into one that starts where it ends.
    Swing stretches and shrinks steps, and everything inside a step with it.

    Timing is sample-accurate: note starts and gate-offs are checked on every sample, so they land on
    the exact sample whatever the buffer size.

    Mixer: each channel goes through volume, constant-power pan, mute and solo into a
    stereo master. Gain changes ramp across the block so moving a fader never clicks. */
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    /** Fraction of a step the acid gate stays open when the step does not slide. */
    static constexpr double gateLength = 0.55;

    explicit AudioEngine (Session& sessionToPlay);

    void prepare (double sampleRate) noexcept;
    double getSampleRate() const noexcept { return sampleRate; }

    /** Writes numSamples of the stereo mix: outputs 0 and 1 get left and right, a single output gets
        (left + right) / 2, any further outputs silence. Real-time safe. */
    void render (float* const* outputs, int numOutputs, int numSamples) noexcept;

    static double samplesPerStep (double sampleRate, double bpm) noexcept { return sampleRate * 60.0 / bpm / 4.0; }

    /** Length of step `index` with swing (MPC %): even steps take `swing`% of each pair of 16ths. */
    static double swungStepLength (double straightLength, float swingPercent, int index) noexcept
    {
        const auto share = static_cast<double> (swingPercent) / 100.0;
        return 2.0 * straightLength * (index % 2 == 0 ? share : 1.0 - share);
    }

    /** Linear gain of a volume in dB; at or below the minimum it is silence. */
    static float volumeGain (float volumeDb) noexcept;

    /** Constant-power pan normalised to unity in the centre: hard left gives +3 dB left, nothing right. */
    static std::pair<float, float> panGains (float pan) noexcept;

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    /** A note that starts inside the current step, `fraction` of the way through it. */
    struct Pending
    {
        Note note;
        double fraction = 0.0;
    };

    /** What the engine keeps for one channel slot between blocks. */
    struct ChannelPlayer
    {
        static constexpr int maxPerStep = 8; // notes starting in one step; more are dropped

        InstrumentKind kind = InstrumentKind::acid;
        std::uint32_t generation = 0;
        dsp::AcidVoice acid;
        dsp::Sampler sampler;

        std::array<Pending, maxPerStep> pending {};
        int pendingCount = 0, nextPending = 0;

        Note lastNote; // the acid note that started last
        bool hasLastNote = false;
        bool releasePending = false; // the acid gate closes at releaseStep + releaseFraction
        std::int64_t releaseStep = 0;
        double releaseFraction = 0.0;

        float gainLeft = 0.0f, gainRight = 0.0f; // gains reached at the end of the last block

        void resetPlayback() noexcept
        {
            pendingCount = nextPending = 0;
            hasLastNote = releasePending = false;
        }
    };

    void startStep (int index, double straightLength, int channels) noexcept;
    void collectNotes (ChannelPlayer& player, const Lane& lane, int index, int lengthInTicks) noexcept;
    void play (ChannelPlayer& player, int channel, const Note& note) noexcept;
    bool noteStartsAt (int channel, int tick) const noexcept;
    void syncPlayer (ChannelPlayer& player, const Channel& channel) noexcept;

    Session& session;
    std::array<ChannelPlayer, Session::maxChannels> players;
    double sampleRate = 48000.0;

    bool wasPlaying = false;
    bool firstStep = true;        // the first step since play: nothing carries over into it
    int step = -1;                // step of the pattern being played
    std::int64_t stepCounter = 0; // steps since play, across pattern wraps
    int patternIndex = 0;
    double samplesIntoStep = 0.0;
    double currentStepLength = 0.0;
    float masterGain = 1.0f;

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
} // namespace tekno::core
