// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Channel.h"

namespace tekno::core
{
namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;
}

const char* instrumentName (InstrumentKind kind) noexcept
{
    return kind == InstrumentKind::sampler ? "sampler" : "acid";
}

AcidSettings AcidParams::load() const noexcept
{
    return { cutoff.load (relaxed), resonance.load (relaxed), envMod.load (relaxed), decay.load (relaxed),
             accent.load (relaxed), drive.load (relaxed),     shape.load (relaxed),  glide.load (relaxed) };
}

void AcidParams::store (const AcidSettings& s) noexcept
{
    cutoff.store (s.cutoff, relaxed);
    resonance.store (s.resonance, relaxed);
    envMod.store (s.envMod, relaxed);
    decay.store (s.decay, relaxed);
    accent.store (s.accent, relaxed);
    drive.store (s.drive, relaxed);
    shape.store (s.shape, relaxed);
    glide.store (s.glide, relaxed);
}

SamplerSettings SamplerParams::load() const noexcept
{
    return { tune.load (relaxed), decay.load (relaxed), accent.load (relaxed) };
}

void SamplerParams::store (const SamplerSettings& s) noexcept
{
    tune.store (s.tune, relaxed);
    decay.store (s.decay, relaxed);
    accent.store (s.accent, relaxed);
}

MixerSettings MixerParams::load() const noexcept
{
    return { volumeDb.load (relaxed), pan.load (relaxed), mute.load (relaxed), solo.load (relaxed) };
}

void MixerParams::store (const MixerSettings& s) noexcept
{
    volumeDb.store (s.volumeDb, relaxed);
    pan.store (s.pan, relaxed);
    mute.store (s.mute, relaxed);
    solo.store (s.solo, relaxed);
}
} // namespace tekno::core
