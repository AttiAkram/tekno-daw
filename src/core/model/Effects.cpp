// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Effects.h"

namespace tekno::core
{
namespace
{
    constexpr auto relaxed = std::memory_order_relaxed;

    // Soft first: it is the default curve of a new waveshaper.
    const std::vector<ShaperCurve>& curveList()
    {
        static const std::vector<ShaperCurve> list {
            // tanh (2x)
            { "soft", { -1.0f, -0.939f, -0.790f, -0.479f, 0.0f, 0.479f, 0.790f, 0.939f, 1.0f }, "Warm saturation" },
            // clip (2x)
            { "hard",
              { -1.0f, -1.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f },
              "Squared off: buzzing, aggressive" },
            // sin (1.5 pi x)
            { "fold",
              { 1.0f, 0.383f, -0.707f, -0.924f, 0.0f, 0.924f, 0.707f, -0.383f, -1.0f },
              "Folds the loud parts back: metallic, harsh" },
            // softer above than below
            { "tube",
              { -0.6f, -0.59f, -0.546f, -0.383f, 0.0f, 0.396f, 0.702f, 0.894f, 1.0f },
              "Lopsided: warmer, adds even harmonics" },
            // |x|
            { "rectify",
              { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f },
              "Flips the lows up: an octave higher" },
            { "clean", { -1.0f, -0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }, "A straight line: no change" },
        };
        return list;
    }

    std::vector<EffectParamInfo> shaperParams()
    {
        std::vector<EffectParamInfo> params {
            { "drive",
              0,
              36,
              6,
              "dB",
              {},
              false,
              "How hard the sound goes into the curve: the grey band shows how far" }
        };
        for (int i = 0; i < shaperPoints; ++i)
        {
            static const char* names[] { "p1", "p2", "p3", "p4", "p5", "p6", "p7", "p8", "p9" };
            params.push_back ({ names[i],
                                -1,
                                1,
                                curveList().front().points[static_cast<size_t> (i)],
                                "",
                                {},
                                true,
                                "A point of the curve: drag it on the graph" });
        }
        params.push_back (
            { "mix", 0, 1, 1, "", {}, false, "How much effect against the dry sound (at 50% both are full)" });
        params.push_back ({ "output", -24, 6, -3, "dB", {}, false, "Level after the effect, to match the loudness" });
        return params;
    }

    std::vector<EffectInfo> makeInfos()
    {
        return {
            { EffectType::distortion,
              "distortion",
              "Distortion",
              {
                  { "mode",
                    0,
                    2,
                    0,
                    "",
                    { "clip", "fold", "crush" },
                    false,
                    "Clip squares the wave off, fold bends it back, crush makes it coarse" },
                  { "drive", 0, 48, 12, "dB", {}, false, "How hard the sound hits the curve: more = dirtier" },
                  { "tone", 0, 1, 1, "", {}, false, "Darkens the distorted sound (100% = open)" },
                  { "bits", 2, 16, 8, "bits", {}, false, "Crush only: fewer bits, grittier steps" },
                  { "rate",
                    1,
                    32,
                    1,
                    "x",
                    {},
                    false,
                    "Crush only: holds each sample this many times, for a lo-fi buzz" },
                  { "mix", 0, 1, 1, "", {}, false, "How much effect against the dry sound (at 50% both are full)" },
                  { "output", -24, 6, -6, "dB", {}, false, "Level after the effect, to match the loudness" },
              } },
            { EffectType::delay,
              "delay",
              "Ping-pong delay",
              {
                  { "time", 0.25f, 16, 3, "steps", {}, false, "Time between echoes, in steps (3 = a dotted eighth)" },
                  { "feedback",
                    0,
                    1.1f,
                    0.45f,
                    "",
                    {},
                    false,
                    "How many echoes: higher lasts longer; past 100% it runs away, kept in bounds" },
                  { "pingpong", 0, 1, 1, "", {}, false, "How far the echoes jump between left and right" },
                  { "tone", 0, 1, 0.6f, "", {}, false, "Darker echoes, each one more than the last" },
                  { "lowcut", 0, 1, 0.2f, "", {}, false, "Thinner echoes: less bass in the repeats" },
                  { "mix", 0, 1, 0.3f, "", {}, false, "How much effect against the dry sound (at 50% both are full)" },
              } },
            { EffectType::reverb,
              "reverb",
              "Plate reverb",
              {
                  { "decay", 0, 1, 0.6f, "", {}, false, "How long the tail lasts" },
                  { "damping", 0, 1, 0.4f, "", {}, false, "How much sooner the highs die than the lows" },
                  { "predelay", 0, 200, 20, "ms", {}, false, "A gap before the tail, so the hit stays clear" },
                  { "lowcut", 0, 1, 0.25f, "", {}, false, "Less bass in the tail, so the low end stays clean" },
                  { "mix", 0, 1, 0.25f, "", {}, false, "How much effect against the dry sound (at 50% both are full)" },
              } },
            { EffectType::shaper, "shaper", "Waveshaper", shaperParams() },
        };
    }

    const std::vector<EffectInfo>& infos()
    {
        static const auto list = makeInfos();
        return list;
    }
} // namespace

int EffectInfo::paramIndex (const juce::String& paramName) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (paramName.equalsIgnoreCase (params[i].name))
            return static_cast<int> (i);
    return -1;
}

const EffectInfo& effectInfo (EffectType type)
{
    static const EffectInfo none { EffectType::none, "none", "", {} };
    for (const auto& info : infos())
        if (info.type == type)
            return info;
    return none;
}

const std::vector<EffectType>& effectTypes()
{
    static const std::vector<EffectType> list { EffectType::distortion, EffectType::shaper, EffectType::delay,
                                                EffectType::reverb };
    return list;
}

const std::vector<ShaperCurve>& shaperCurves()
{
    return curveList();
}

std::optional<std::array<float, shaperPoints>> shaperCurve (const juce::String& name)
{
    for (const auto& c : curveList())
        if (name.trim().equalsIgnoreCase (c.name))
            return c.points;
    return std::nullopt;
}

std::optional<EffectType> effectFromName (const juce::String& name)
{
    const auto n = name.trim().toLowerCase();
    if (n == "dist" || n == "drive" || n == "distorsione")
        return EffectType::distortion;
    if (n == "echo" || n == "pingpong")
        return EffectType::delay;
    if (n == "verb" || n == "riverbero")
        return EffectType::reverb;
    if (n == "waveshaper" || n == "shape")
        return EffectType::shaper;
    for (const auto& info : infos())
        if (n == info.name)
            return info.type;
    return std::nullopt;
}

EffectSettings EffectSettings::defaults (EffectType type)
{
    EffectSettings s;
    s.type = type;
    const auto& info = effectInfo (type);
    for (size_t i = 0; i < info.params.size(); ++i)
        s.values[i] = info.params[i].defaultValue;
    return s;
}

EffectSettings EffectSlot::load() const noexcept
{
    EffectSettings s;
    s.type = type.load (relaxed);
    s.bypass = bypass.load (relaxed);
    for (size_t i = 0; i < values.size(); ++i)
        s.values[i] = values[i].load (relaxed);
    return s;
}

void EffectSlot::store (const EffectSettings& s) noexcept
{
    for (size_t i = 0; i < values.size(); ++i)
        values[i].store (s.values[i], relaxed);
    bypass.store (s.bypass, relaxed);
    if (type.load (relaxed) != s.type)
    {
        type.store (s.type, relaxed);
        generation.fetch_add (1, std::memory_order_release); // another effect: start from silence
    }
}

EffectChainSettings EffectChain::load() const noexcept
{
    EffectChainSettings s;
    for (size_t i = 0; i < slots.size(); ++i)
        s[i] = slots[i].load();
    return s;
}

void EffectChain::store (const EffectChainSettings& s) noexcept
{
    for (size_t i = 0; i < slots.size(); ++i)
        slots[i].store (s[i]);
}

bool EffectChain::empty() const noexcept
{
    for (const auto& slot : slots)
        if (slot.type.load (relaxed) != EffectType::none)
            return false;
    return true;
}
} // namespace tekno::core
