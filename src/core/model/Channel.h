// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <atomic>
#include <cstdint>

#include "core/model/Effects.h"

namespace tekno::core
{
struct Sample;

/** The instruments a channel can play. Plugins join in M4. */
enum class InstrumentKind : std::uint8_t
{
    acid,
    sampler,
};

const char* instrumentName (InstrumentKind kind) noexcept; // "acid", "sampler"

// Each group of parameters comes twice: a plain struct (a copy: files, undo, defaults) and the same
// values as atomics (shared live with the audio thread). load() and store() move between the two.

/** The acid synth's controls, normalised to 0..1. The voice maps them to Hz, seconds, gain. */
struct AcidSettings
{
    float cutoff = 0.35f;
    float resonance = 0.8f;
    float envMod = 0.6f;
    float decay = 0.45f;
    float accent = 0.7f;
    float drive = 0.0f;
    float shape = 0.0f;   // 0 = saw .. 1 = square, morphing in between
    float glide = 0.035f; // seconds a slide takes
    static constexpr float minGlide = 0.005f, maxGlide = 0.5f;

    bool operator== (const AcidSettings&) const = default;
};

struct AcidParams
{
    std::atomic<float> cutoff, resonance, envMod, decay, accent, drive;
    std::atomic<float> shape, glide;

    AcidParams() noexcept { store ({}); }
    AcidSettings load() const noexcept;
    void store (const AcidSettings&) noexcept;
};

/** The drum sampler's controls. */
struct SamplerSettings
{
    static constexpr float minTune = -24.0f, maxTune = 24.0f;

    float tune = 0.0f;   // semitones
    float decay = 1.0f;  // 0..1: 1 = the whole sample, lower = a shorter tail
    float accent = 0.5f; // 0..1: how much louder an accented step hits

    bool operator== (const SamplerSettings&) const = default;
};

struct SamplerParams
{
    std::atomic<float> tune, decay, accent;

    SamplerParams() noexcept { store ({}); }
    SamplerSettings load() const noexcept;
    void store (const SamplerSettings&) noexcept;
};

/** One channel's strip in the mixer. */
struct MixerSettings
{
    static constexpr float minVolumeDb = -60.0f; // and below: silence
    static constexpr float maxVolumeDb = 6.0f;

    float volumeDb = 0.0f;
    float pan = 0.0f; // -1 left .. +1 right
    bool mute = false;
    bool solo = false;

    bool operator== (const MixerSettings&) const = default;
};

struct MixerParams
{
    std::atomic<float> volumeDb, pan;
    std::atomic<bool> mute, solo;

    MixerParams() noexcept { store ({}); }
    MixerSettings load() const noexcept;
    void store (const MixerSettings&) noexcept;
};

/** A channel as the audio thread sees it: only atomics. Its name and the origin of its sample live in
    ProjectInfo (message thread). */
struct Channel
{
    std::atomic<InstrumentKind> kind { InstrumentKind::acid };
    AcidParams acid;
    SamplerParams sampler;
    MixerParams mixer;
    EffectChain effects; // inserts, before the fader

    /** The sampler's sound, owned by the SampleLibrary; null = silent. Replaced, never modified. */
    std::atomic<const Sample*> sample { nullptr };

    /** Bumped whenever this slot starts holding a different channel (add, remove, open a project):
        the engine then resets the voices it keeps for the slot. */
    std::atomic<std::uint32_t> generation { 0 };

    /** Written by the audio thread: the loudest sample since the GUI last took it (exchange with 0). */
    std::atomic<float> peak { 0.0f };

    static_assert (std::atomic<InstrumentKind>::is_always_lock_free);
    static_assert (std::atomic<const Sample*>::is_always_lock_free);
};
} // namespace tekno::core
