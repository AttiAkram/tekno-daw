// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/engine/AudioEngine.h"

#include <cmath>

namespace tekno::core
{
namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    dsp::AcidVoice::Controls controlsOf (const AcidSettings& s) noexcept
    {
        dsp::AcidVoice::Controls c;
        c.cutoff = s.cutoff;
        c.resonance = s.resonance;
        c.envMod = s.envMod;
        c.decay = s.decay;
        c.accent = s.accent;
        c.shape = s.shape;
        c.glide = s.glide;
        c.drive = s.drive;
        return c;
    }

    /** Raises a meter to `value` if it is louder. The GUI resets it with exchange (0). */
    void raisePeak (std::atomic<float>& meter, float value) noexcept
    {
        auto current = meter.load (relaxed);
        while (value > current && ! meter.compare_exchange_weak (current, value, relaxed))
        {
        }
    }
} // namespace

AudioEngine::AudioEngine (Session& sessionToPlay)
    : session (sessionToPlay)
{
    prepare (sampleRate);
}

float AudioEngine::volumeGain (float volumeDb) noexcept
{
    if (volumeDb <= MixerSettings::minVolumeDb)
        return 0.0f;
    return std::pow (10.0f, volumeDb / 20.0f);
}

std::pair<float, float> AudioEngine::panGains (float pan) noexcept
{
    const auto angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    return { std::cos (angle) * juce::MathConstants<float>::sqrt2,
             std::sin (angle) * juce::MathConstants<float>::sqrt2 };
}

void AudioEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    for (auto& p : players)
    {
        p.acid.prepare (sampleRate);
        p.sampler.prepare (sampleRate);
        p.generation = ~0u; // force a full sync on the first block
        p.resetPlayback();
        p.gainLeft = p.gainRight = 0.0f;
        p.acidGain = p.acidGainTarget = 1.0f;
    }
    velocityGlide = static_cast<float> (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));

    // Effects: scratch buffers and the pool, sized for this sample rate. prepare() runs before audio
    // starts (or when the device restarts), never inside the callback.
    stripScratch.assign (static_cast<size_t> (strips) * 2 * maxChunk, 0.0f);
    for (auto& d : distortions)
        d.prepare (sampleRate);
    delays.resize (maxDelays);
    for (auto& d : delays)
        d.prepare (sampleRate);
    reverbs.resize (maxReverbs);
    for (auto& r : reverbs)
        r.prepare (sampleRate);
    delayOwner.fill (-1);
    reverbOwner.fill (-1);
    for (auto& strip : slotStates)
        strip.fill ({});
    lastPreview = session.preview.load() >> 32; // an old request is not played again
    wasPlaying = false;
    step = -1;
    stepCounter = 0;
    firstStep = true;
    samplesIntoStep = 0.0;
    currentStepLength = 0.0;
    masterGain = volumeGain (session.master.volumeDb.load (relaxed));
    session.transport.currentStep.store (-1);
}

void AudioEngine::syncPlayer (ChannelPlayer& player, const Channel& channel) noexcept
{
    const auto generation = channel.generation.load (std::memory_order_acquire);
    const auto kind = channel.kind.load (relaxed);
    if (generation != player.generation || kind != player.kind)
    {
        // Another channel now lives in this slot: start from silence.
        player.generation = generation;
        player.kind = kind;
        player.acid.reset();
        player.sampler.reset();
        player.resetPlayback();
        player.gainLeft = player.gainRight = 0.0f;
    }

    if (kind == InstrumentKind::acid)
        player.acid.setControls (controlsOf (channel.acid.load()));
    else
    {
        player.sampler.setSettings (channel.sampler.load());
        player.sampler.setSample (channel.sample.load (std::memory_order_acquire));
    }
}

void AudioEngine::render (float* const* outputs, int numOutputs, int numSamples) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    for (int offset = 0; offset < numSamples; offset += maxChunk)
        renderChunk (outputs, numOutputs, offset, std::min (maxChunk, numSamples - offset));
    session.audioBlocks.fetch_add (1, std::memory_order_release);
}

void AudioEngine::syncSlot (int strip, int index, const EffectSlot& slot, std::uint32_t stripGeneration) noexcept
{
    auto& state = slotStates[static_cast<size_t> (strip)][static_cast<size_t> (index)];
    const auto type = slot.type.load (relaxed);
    const auto generation = slot.generation.load (std::memory_order_acquire);
    if (type == state.type && generation == state.generation && stripGeneration == state.stripGeneration)
        return;

    // Another effect (or another channel) in this slot: give back the old instance, take a fresh one.
    const auto owner = strip * effectSlots + index;
    if (state.type == EffectType::delay && state.instance >= 0)
        delayOwner[static_cast<size_t> (state.instance)] = -1;
    if (state.type == EffectType::reverb && state.instance >= 0)
        reverbOwner[static_cast<size_t> (state.instance)] = -1;
    state = { type, generation, stripGeneration, -1 };

    auto take = [owner] (auto& owners) -> int
    {
        for (size_t i = 0; i < owners.size(); ++i)
            if (owners[i] < 0)
            {
                owners[i] = owner;
                return static_cast<int> (i);
            }
        return -1; // full: the slot stays silent (the commands keep this from happening)
    };
    switch (type)
    {
    case EffectType::distortion:
        state.instance = owner;
        distortions[static_cast<size_t> (owner)].reset();
        break;
    case EffectType::delay:
        if ((state.instance = take (delayOwner)) >= 0)
            delays[static_cast<size_t> (state.instance)].reset();
        break;
    case EffectType::reverb:
        if ((state.instance = take (reverbOwner)) >= 0)
            reverbs[static_cast<size_t> (state.instance)].reset();
        break;
    case EffectType::none:
        break;
    }
}

void AudioEngine::runEffects (int strip, EffectChain& chain, std::uint32_t stripGeneration, float* left, float* right,
                              int numSamples, double bpm) noexcept
{
    for (int index = 0; index < effectSlots; ++index)
    {
        auto& slot = chain.slots[static_cast<size_t> (index)];
        syncSlot (strip, index, slot, stripGeneration);
        const auto& state = slotStates[static_cast<size_t> (strip)][static_cast<size_t> (index)];
        if (state.type == EffectType::none || state.instance < 0 || slot.bypass.load (relaxed))
            continue;

        std::array<float, maxEffectParams> params {};
        for (size_t p = 0; p < params.size(); ++p)
            params[p] = slot.values[p].load (relaxed);

        dsp::EffectMeters meters;
        const auto instance = static_cast<size_t> (state.instance);
        if (state.type == EffectType::distortion)
            meters = distortions[instance].process (left, right, numSamples, params);
        else if (state.type == EffectType::delay)
            meters = delays[instance].process (left, right, numSamples, params, bpm);
        else
            meters = reverbs[instance].process (left, right, numSamples, params);
        raisePeak (slot.activity, meters.peak);
        raisePeak (slot.clip, meters.clip);
    }
}

void AudioEngine::renderChunk (float* const* outputs, int numOutputs, int offset, int numSamples) noexcept
{
    auto& transport = session.transport;

    const auto playing = transport.playing.load (relaxed);
    const auto bpm = std::clamp (transport.bpm.load (relaxed), TransportState::minBpm, TransportState::maxBpm);
    const auto straightLength = samplesPerStep (sampleRate, bpm);
    const auto channels = std::clamp (session.channelCount(), 0, Session::maxChannels);

    // Once per block: each channel's settings, sample and target gains.
    bool anySolo = false;
    for (int c = 0; c < channels; ++c)
        anySolo = anySolo || session.channel (c).mixer.solo.load (relaxed);

    std::array<float, Session::maxChannels> targetLeft {}, targetRight {}, peaks {};
    for (int c = 0; c < channels; ++c)
    {
        const auto& channel = session.channel (c);
        syncPlayer (players[static_cast<size_t> (c)], channel);
        const auto mix = channel.mixer.load();
        const auto audible = ! mix.mute && (! anySolo || mix.solo);
        const auto gain = audible ? volumeGain (mix.volumeDb) : 0.0f;
        const auto [left, right] = panGains (mix.pan);
        targetLeft[static_cast<size_t> (c)] = gain * left;
        targetRight[static_cast<size_t> (c)] = gain * right;
    }
    const auto masterTarget = volumeGain (session.master.volumeDb.load (relaxed));

    if (playing && ! wasPlaying)
    {
        step = -1;
        stepCounter = 0;
        firstStep = true;
        for (auto& p : players)
            p.resetPlayback();
        currentStepLength = 0.0;
        samplesIntoStep = 0.0; // the very first sample starts step 0
    }
    else if (! playing && wasPlaying)
    {
        for (auto& p : players)
        {
            p.acid.gateOff(); // samplers ring out: a one-shot is not cut by stop
            p.resetPlayback();
        }
        step = -1;
        transport.currentStep.store (-1, relaxed);
    }
    wasPlaying = playing;

    startPreview (channels);
    live.drain ([this, channels, straightLength] (const LiveInput::Event& e)
                { playLive (e, channels, straightLength); });

    float masterPeakLeft = 0.0f, masterPeakRight = 0.0f;
    const auto rampStep = 1.0f / static_cast<float> (std::max (1, numSamples));

    for (int i = 0; i < numSamples; ++i)
    {
        if (playing)
        {
            if (samplesIntoStep >= currentStepLength)
            {
                samplesIntoStep = std::max (0.0, samplesIntoStep - currentStepLength);
                startStep (step + 1, straightLength, channels);
                if (samplesIntoStep >= currentStepLength) // tempo jumped up a lot: don't fire twice in a row
                    samplesIntoStep = 0.0;
            }

            for (int c = 0; c < channels; ++c)
            {
                auto& p = players[static_cast<size_t> (c)];
                while (p.nextPending < p.pendingCount &&
                       p.pending[static_cast<size_t> (p.nextPending)].fraction * currentStepLength <= samplesIntoStep)
                    play (p, c, p.pending[static_cast<size_t> (p.nextPending++)].note);

                if (p.releasePending &&
                    (stepCounter > p.releaseStep ||
                     (stepCounter == p.releaseStep && samplesIntoStep >= currentStepLength * p.releaseFraction)))
                {
                    p.acid.gateOff();
                    p.releasePending = false;
                }
            }
            samplesIntoStep += 1.0;
        }

        for (int c = 0; c < channels; ++c) // auditioned and short-gated live acid notes end by themselves
        {
            auto& p = players[static_cast<size_t> (c)];
            if (p.previewSamplesLeft > 0 && --p.previewSamplesLeft == 0)
                p.acid.gateOff();
            if (p.shortGateSamples > 0 && --p.shortGateSamples == 0)
                releaseLive (p, p.shortGatePitch);
        }

        // Each channel's dry sound into its own buffer: effects and faders come after the loop.
        for (int c = 0; c < channels; ++c)
        {
            auto& p = players[static_cast<size_t> (c)];
            float l = 0.0f, r = 0.0f;
            if (p.kind == InstrumentKind::acid)
            {
                // Velocity 100 everywhere keeps this exactly 1: old projects sound bit for bit the same.
                p.acidGain += (p.acidGainTarget - p.acidGain) * velocityGlide;
                l = r = p.acid.process() * p.acidGain;
            }
            else
                p.sampler.process (l, r);
            stripBuffer (c, 0)[i] = l;
            stripBuffer (c, 1)[i] = r;
        }
    }

    // Channels: inserts, then the fader (ramped across the chunk), summed into the master in channel
    // order, so with no effects every sample is computed exactly as before effects existed.
    auto* masterLeft = stripBuffer (masterStrip, 0);
    auto* masterRight = stripBuffer (masterStrip, 1);
    std::fill (masterLeft, masterLeft + numSamples, 0.0f);
    std::fill (masterRight, masterRight + numSamples, 0.0f);
    for (int c = 0; c < channels; ++c)
    {
        const auto slot = static_cast<size_t> (c);
        auto& p = players[slot];
        auto* left = stripBuffer (c, 0);
        auto* right = stripBuffer (c, 1);
        auto& channel = session.channel (c);
        runEffects (c, channel.effects, channel.generation.load (relaxed), left, right, numSamples, bpm);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto ramp = static_cast<float> (i + 1) * rampStep;
            const auto l = left[i] * (p.gainLeft + (targetLeft[slot] - p.gainLeft) * ramp);
            const auto r = right[i] * (p.gainRight + (targetRight[slot] - p.gainRight) * ramp);
            peaks[slot] = std::max (peaks[slot], std::max (std::abs (l), std::abs (r)));
            masterLeft[i] += l;
            masterRight[i] += r;
        }
    }

    // Master: inserts, then its fader, then the device.
    runEffects (masterStrip, session.master.effects, 0, masterLeft, masterRight, numSamples, bpm);
    for (int i = 0; i < numSamples; ++i)
    {
        const auto ramp = static_cast<float> (i + 1) * rampStep;
        const auto master = masterGain + (masterTarget - masterGain) * ramp;
        const auto left = masterLeft[i] * master;
        const auto right = masterRight[i] * master;
        masterPeakLeft = std::max (masterPeakLeft, std::abs (left));
        masterPeakRight = std::max (masterPeakRight, std::abs (right));

        if (numOutputs == 1)
        {
            if (outputs[0] != nullptr)
                outputs[0][offset + i] = (left + right) * 0.5f;
        }
        else
            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                    outputs[ch][offset + i] = ch == 0 ? left : (ch == 1 ? right : 0.0f);
    }

    for (int c = 0; c < channels; ++c)
    {
        const auto slot = static_cast<size_t> (c);
        players[slot].gainLeft = targetLeft[slot];
        players[slot].gainRight = targetRight[slot];
        raisePeak (session.channel (c).peak, peaks[slot]);
    }
    masterGain = masterTarget;
    raisePeak (session.master.peakLeft, masterPeakLeft);
    raisePeak (session.master.peakRight, masterPeakRight);
}

void AudioEngine::startPreview (int channels) noexcept
{
    const auto request = session.preview.load (std::memory_order_acquire);
    if ((request >> 32) == lastPreview)
        return;
    lastPreview = request >> 32;

    const auto channel = static_cast<int> ((request >> 16) & 0xffu);
    const auto velocity = static_cast<int> ((request >> 8) & 0xffu);
    const auto pitch = static_cast<int> (request & 0xffu);
    if (channel >= channels)
        return;

    auto& player = players[static_cast<size_t> (channel)];
    if (player.kind == InstrumentKind::sampler)
    {
        player.sampler.trigger (pitch, false, Note::velocityGain (velocity));
        return;
    }
    player.acid.noteOn (pitch, false);
    player.acidGainTarget = Note::velocityGain (velocity);
    player.previewSamplesLeft = static_cast<int> (sampleRate * 0.25); // a quarter of a second
}

void AudioEngine::playLive (const LiveInput::Event& event, int channels, double straightLength) noexcept
{
    using Type = LiveInput::Event::Type;
    if (event.type == Type::allOff)
    {
        for (auto& p : players)
        {
            if (p.heldCount > 0 || p.shortGateSamples > 0)
                p.acid.gateOff();
            p.heldCount = 0;
            p.shortGateSamples = 0;
        }
        return;
    }
    if (event.channel < 0 || event.channel >= channels)
        return;
    auto& player = players[static_cast<size_t> (event.channel)];

    if (event.type == Type::noteOff)
    {
        if (player.kind == InstrumentKind::acid &&
            ! (player.shortGateSamples > 0 && event.pitch == player.shortGatePitch))
            releaseLive (player, event.pitch);
        return;
    }

    live.noteWait (juce::Time::getMillisecondCounterHiRes() - event.timeMs);
    const auto gain = Note::velocityGain (event.velocity);
    if (player.kind == InstrumentKind::sampler)
    {
        player.sampler.trigger (event.pitch, event.accent, gain);
        return;
    }

    // Acid, mono legato: a key pressed while another is held slides to it, like a 303 played by hand.
    if (player.heldCount == 0)
        player.acid.noteOn (event.pitch, event.accent);
    else
        player.acid.glideTo (event.pitch);
    auto count = 0; // keep each pitch once, the newest last
    for (int i = 0; i < player.heldCount; ++i)
        if (player.held[static_cast<size_t> (i)] != event.pitch)
            player.held[static_cast<size_t> (count++)] = player.held[static_cast<size_t> (i)];
    if (count == static_cast<int> (player.held.size())) // 16 keys down at once: forget the oldest
    {
        std::move (player.held.begin() + 1, player.held.end(), player.held.begin());
        --count;
    }
    player.held[static_cast<size_t> (count)] = event.pitch;
    player.heldCount = count + 1;
    player.acidGainTarget = gain;
    player.previewSamplesLeft = 0;
    player.releasePending = false; // a pattern note's gate-off must not cut the live one
    if (event.shortGate)
    {
        player.shortGateSamples = std::max (1, static_cast<int> (straightLength * gateLength));
        player.shortGatePitch = event.pitch;
    }
}

void AudioEngine::releaseLive (ChannelPlayer& player, std::uint8_t pitch) noexcept
{
    auto count = 0;
    auto wasSounding = player.heldCount > 0 && player.held[static_cast<size_t> (player.heldCount - 1)] == pitch;
    for (int i = 0; i < player.heldCount; ++i)
        if (player.held[static_cast<size_t> (i)] != pitch)
            player.held[static_cast<size_t> (count++)] = player.held[static_cast<size_t> (i)];
    if (count == player.heldCount)
        return; // it wasn't held
    player.heldCount = count;
    if (count == 0)
        player.acid.gateOff();
    else if (wasSounding)
        player.acid.glideTo (player.held[static_cast<size_t> (count - 1)]); // back to the key still down
}

void AudioEngine::startStep (int index, double straightLength, int channels) noexcept
{
    firstStep = step < 0;

    // Wrap, and switch pattern on the wrap: the pattern being edited takes over at the bar line.
    if (firstStep || index >= session.pattern (patternIndex).getLength())
    {
        index = 0;
        patternIndex = session.selected.load (relaxed);
        session.playing.store (patternIndex, relaxed);
    }

    const auto& pattern = session.pattern (patternIndex);
    step = index;
    ++stepCounter;
    session.transport.currentStep.store (index, relaxed);
    currentStepLength = swungStepLength (straightLength, session.transport.swing.load (relaxed), index);

    for (int c = 0; c < channels; ++c)
        collectNotes (players[static_cast<size_t> (c)], pattern.lane (c), index, pattern.lengthInTicks());
}

void AudioEngine::collectNotes (ChannelPlayer& player, const Lane& lane, int index, int lengthInTicks) noexcept
{
    const auto from = index * Note::ticksPerStep;
    const auto to = std::min (from + Note::ticksPerStep, lengthInTicks);

    player.pendingCount = player.nextPending = 0;
    for (int i = 0; i < Lane::maxNotes; ++i)
    {
        const auto bits = lane.slot (i);
        if (bits == 0)
            continue;
        const auto note = Note::unpack (bits);
        if (note.start < from || note.start >= to || player.pendingCount == ChannelPlayer::maxPerStep)
            continue;

        // Keep them in order of start (then pitch): a handful at most, so insertion is enough.
        auto at = player.pendingCount++;
        while (at > 0 && earlier (note, player.pending[static_cast<size_t> (at - 1)].note))
        {
            player.pending[static_cast<size_t> (at)] = player.pending[static_cast<size_t> (at - 1)];
            --at;
        }
        player.pending[static_cast<size_t> (at)] = { note,
                                                     (note.start - from) / static_cast<double> (Note::ticksPerStep) };
    }
}

bool AudioEngine::noteStartsAt (int channel, int tick) const noexcept
{
    const auto& current = session.pattern (patternIndex);
    const auto length = current.lengthInTicks();
    const auto& pattern = tick < length ? current : session.pattern (session.selected.load (relaxed));
    if (tick >= length)
        tick -= length;
    const auto& lane = pattern.lane (channel);
    for (int i = 0; i < Lane::maxNotes; ++i)
        if (const auto bits = lane.slot (i); bits != 0 && Note::unpack (bits).start == tick)
            return true;
    return false;
}

void AudioEngine::play (ChannelPlayer& player, int channel, const Note& note) noexcept
{
    if (player.kind == InstrumentKind::sampler)
    {
        player.sampler.trigger (note.pitch, note.accent, Note::velocityGain (note.velocity));
        return;
    }

    if (player.heldCount > 0)
        return; // someone is playing this channel live: live play wins over the pattern

    auto& voice = player.acid;
    player.previewSamplesLeft = 0; // the sequencer takes over from an audition
    const auto gateOpen = ! firstStep && voice.isGateOn();
    if (gateOpen && player.hasLastNote && player.lastNote.slide)
        voice.glideTo (note.pitch); // no retrigger: the slide is one continuous gesture
    else
        voice.noteOn (note.pitch, note.accent);
    player.lastNote = note;
    player.hasLastNote = true;
    player.acidGainTarget = Note::velocityGain (note.velocity);

    // Slide into a note that starts where this one ends: the gate stays open until it arrives.
    if (note.slide && noteStartsAt (channel, note.end()))
    {
        player.releasePending = false;
        return;
    }

    // The gate closes 45% of a step before the end (55% of a one-step note), shorter notes in proportion.
    constexpr auto tps = Note::ticksPerStep;
    const auto fromStepStart = note.start - step * tps; // ticks into the current step
    const auto tail = std::min (tps, note.length);
    const auto lastStepStart = fromStepStart + note.length - tail;
    if (tail == tps && lastStepStart % tps == 0)
    {
        player.releaseStep = stepCounter + lastStepStart / tps;
        player.releaseFraction = gateLength; // exactly M2's gate for notes on the grid
    }
    else
    {
        const auto ticks = lastStepStart + tail * gateLength;
        const auto steps = std::floor (ticks / tps);
        player.releaseStep = stepCounter + static_cast<std::int64_t> (steps);
        player.releaseFraction = (ticks - steps * tps) / tps;
    }
    player.releasePending = true;
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
