// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>

#include "core/model/Pattern.h"

namespace tekno::core
{
/** Transport state. `playing`, `bpm`, `swing` are written by commands; `currentStep` by the audio thread. */
struct TransportState
{
    static constexpr double minBpm = 40.0;
    static constexpr double maxBpm = 300.0;
    static constexpr double defaultBpm = 130.0;

    /** Swing in MPC notation: the share of a pair of 16ths taken by the first one.
        50 = straight, 66.7 = triplet shuffle, 75 = dotted. */
    static constexpr float minSwing = 50.0f;
    static constexpr float maxSwing = 75.0f;

    std::atomic<bool> playing { false };
    std::atomic<double> bpm { defaultBpm };
    std::atomic<float> swing { minSwing };
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
    static constexpr float defaultDrive = 0.0f;

    std::atomic<float> cutoff { defaultCutoff };
    std::atomic<float> resonance { defaultResonance };
    std::atomic<float> envMod { defaultEnvMod };
    std::atomic<float> decay { defaultDecay };
    std::atomic<float> accent { defaultAccent };
    std::atomic<float> drive { defaultDrive };
    std::atomic<bool> square { false }; // false = saw

    void reset() noexcept;
};

/** Facts about the project as a document. Message thread only: the audio thread never touches it. */
struct ProjectInfo
{
    juce::File file;                 // empty until saved or opened
    std::uint64_t revision = 0;      // bumped by every change that would be saved
    std::uint64_t savedRevision = 0; // revision at the last save or open
    std::array<juce::String, 16> patternNames;

    bool hasUnsavedChanges() const noexcept { return revision != savedRevision; }
};

/** Everything the running DAW is playing, shared between the message thread and the audio thread.
    Only commands write to it (ADR 0014); the audio thread only reads it, except where noted. */
class Session
{
public:
    static constexpr int maxPatterns = 16;

    Session();

    /** A new project: one acid line in pattern 1, everything else empty. */
    void loadDefaultProject();

    Pattern& pattern (int index) noexcept
    {
        return patterns[static_cast<size_t> (std::clamp (index, 0, maxPatterns - 1))];
    }
    const Pattern& pattern (int index) const noexcept
    {
        return patterns[static_cast<size_t> (std::clamp (index, 0, maxPatterns - 1))];
    }
    Pattern& selectedPattern() noexcept { return pattern (selected.load()); }
    const Pattern& selectedPattern() const noexcept { return pattern (selected.load()); }

    static juce::String defaultPatternName (int index) { return "pattern " + juce::String (index + 1); }

    TransportState transport;
    AcidParams synth;
    std::array<Pattern, maxPatterns> patterns;

    std::atomic<int> selected { 0 }; // the pattern being edited; playback switches to it at the next wrap
    std::atomic<int> playing { 0 };  // the pattern being played: written by the audio thread

    ProjectInfo project; // message thread only

    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<float>::is_always_lock_free);
};
} // namespace tekno::core
