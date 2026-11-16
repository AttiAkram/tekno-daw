// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <vector>

#include <juce_core/juce_core.h>

namespace tekno::core
{
/** The native effects an insert slot can hold. VST3 effects join later. Files name them, so the order
    of the values doesn't matter; new ones go at the end. */
enum class EffectType : std::uint8_t
{
    none,
    distortion,
    delay,
    reverb,
    shaper,
};

inline constexpr int effectSlots = 4;      // per channel, and on the master
inline constexpr int maxEffectParams = 16; // values per slot
inline constexpr int shaperPoints = 9;     // the waveshaper's curve: y at x = -1, -0.75 ... 0.75, 1
inline constexpr int maxDelays = 8;        // per project: each engine preallocates this many
inline constexpr int maxReverbs = 8;

/** One parameter of an effect, in its own units. With choices, the value is the choice's index. */
struct EffectParamInfo
{
    const char* name;
    float min, max, defaultValue;
    const char* unit; // "dB", "steps", "ms", "" ...
    std::vector<const char*> choices;
    bool graph = false;    // changed on the effect's graph (the waveshaper's points), not with a knob
    const char* help = ""; // what it does, in a few words: the tooltip

    bool isChoice() const noexcept { return ! choices.empty(); }
};

struct EffectInfo
{
    EffectType type;
    const char* name;  // "distortion": commands and files
    const char* title; // "Distortion": the UI
    std::vector<EffectParamInfo> params;

    int paramIndex (const juce::String& paramName) const; // -1 if unknown
};

const EffectInfo& effectInfo (EffectType type);
const std::vector<EffectType>& effectTypes(); // every real one, in menu order
std::optional<EffectType> effectFromName (const juce::String& name);

/** A ready-made waveshaper curve: the y of each of its points. */
struct ShaperCurve
{
    const char* name;
    std::array<float, shaperPoints> points;
    const char* help; // what it sounds like
};
const std::vector<ShaperCurve>& shaperCurves(); // soft (the default), hard, fold, tube, rectify, clean
std::optional<std::array<float, shaperPoints>> shaperCurve (const juce::String& name);

/** A slot as plain values: files, undo, defaults. */
struct EffectSettings
{
    EffectType type = EffectType::none;
    bool bypass = false;
    std::array<float, maxEffectParams> values {};

    static EffectSettings defaults (EffectType type);
    friend bool operator== (const EffectSettings&, const EffectSettings&) = default;
};

using EffectChainSettings = std::array<EffectSettings, effectSlots>;

/** A slot as the audio thread sees it. */
struct EffectSlot
{
    std::atomic<EffectType> type { EffectType::none };
    std::atomic<bool> bypass { false };
    std::array<std::atomic<float>, maxEffectParams> values {};
    std::atomic<std::uint32_t> generation { 0 }; // bumped when the slot gets another effect: state resets

    /** Written by the audio thread, taken by the UI with exchange (0): how loud its output was, how
        hard a distortion clipped (0 = not at all), and how far into its curve the input reached (the
        graph's band). */
    std::atomic<float> activity { 0.0f }, clip { 0.0f }, level { 0.0f };

    EffectSettings load() const noexcept;
    void store (const EffectSettings& settings) noexcept;
};

struct EffectChain
{
    std::array<EffectSlot, effectSlots> slots;

    EffectChainSettings load() const noexcept;
    void store (const EffectChainSettings& settings) noexcept;
    bool empty() const noexcept;
};
} // namespace tekno::core
