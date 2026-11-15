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

    std::vector<EffectInfo> makeInfos()
    {
        return {
            { EffectType::distortion,
              "distortion",
              "Distortion",
              {
                  { "mode", 0, 2, 0, "", { "clip", "fold", "crush" } },
                  { "drive", 0, 48, 12, "dB", {} },
                  { "tone", 0, 1, 1, "", {} },
                  { "bits", 2, 16, 8, "bits", {} },
                  { "rate", 1, 32, 1, "x", {} },
                  { "mix", 0, 1, 1, "", {} },
                  { "output", -24, 6, -6, "dB", {} },
              } },
            { EffectType::delay,
              "delay",
              "Ping-pong delay",
              {
                  { "time", 0.25f, 16, 3, "steps", {} },
                  { "feedback", 0, 1.1f, 0.45f, "", {} },
                  { "pingpong", 0, 1, 1, "", {} },
                  { "tone", 0, 1, 0.6f, "", {} },
                  { "lowcut", 0, 1, 0.2f, "", {} },
                  { "mix", 0, 1, 0.3f, "", {} },
              } },
            { EffectType::reverb,
              "reverb",
              "Plate reverb",
              {
                  { "decay", 0, 1, 0.6f, "", {} },
                  { "damping", 0, 1, 0.4f, "", {} },
                  { "predelay", 0, 200, 20, "ms", {} },
                  { "lowcut", 0, 1, 0.25f, "", {} },
                  { "mix", 0, 1, 0.25f, "", {} },
              } },
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
    static const std::vector<EffectType> list { EffectType::distortion, EffectType::delay, EffectType::reverb };
    return list;
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
