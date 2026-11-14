// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <atomic>

#include "core/model/Pattern.h"

namespace tekno::core
{
/** Transport state. `playing` and `bpm` are written by commands; `currentStep` by the audio thread. */
struct TransportState
{
    static constexpr double minBpm = 40.0;
    static constexpr double maxBpm = 300.0;
    static constexpr double defaultBpm = 130.0;

    std::atomic<bool> playing { false };
    std::atomic<double> bpm { defaultBpm };
    std::atomic<int> currentStep { -1 }; // the step being played, -1 when stopped
};

/** The acid synth's controls, normalised to 0..1. The voice maps them to Hz, seconds, gain. */
struct AcidParams
{
    static constexpr float defaultCutoff = 0.35f;
    static constexpr float defaultResonance = 0.8f;
    static constexpr float defaultEnvMod = 0.6f;
    static constexpr float defaultDecay = 0.45f;
    static constexpr float defaultAccent = 0.7f;

    std::atomic<float> cutoff { defaultCutoff };
    std::atomic<float> resonance { defaultResonance };
    std::atomic<float> envMod { defaultEnvMod };
    std::atomic<float> decay { defaultDecay };
    std::atomic<float> accent { defaultAccent };
    std::atomic<bool> square { false }; // false = saw
};

/** Everything the running DAW is playing, shared between the message thread and the audio thread.
    Only commands write to it (ADR 0014); the audio thread only reads it, except currentStep. */
class Session
{
public:
    Session();

    void loadDefaultPattern() noexcept;

    TransportState transport;
    Pattern pattern;
    AcidParams synth;

    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
};
} // namespace tekno::core
