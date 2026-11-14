// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/components/LevelMeters.h"

#include <cmath>

#include "ui/theme/Tokens.h"

namespace tekno::ui
{
LevelMeters::Level LevelMeters::advance (Level level, float peak, double secondsSinceLast, double nowMs) noexcept
{
    const auto db = peak > 0.0f ? std::max (minDb, 20.0f * std::log10 (peak)) : minDb;
    const auto fallen = level.db - tokens::motion::meterFallDbPerSecond * static_cast<float> (secondsSinceLast);
    level.db = std::clamp (std::max (db, fallen), minDb, maxDb);

    if (db >= level.holdDb)
    {
        level.holdDb = std::min (db, maxDb);
        level.holdSince = nowMs;
    }
    else if (nowMs - level.holdSince > tokens::motion::meterHoldMs)
        level.holdDb = level.db; // after the hold, the line rides on the bar

    level.clipped = level.clipped || peak >= 1.0f;
    return level;
}

LevelMeters::LevelMeters (core::Session& sessionToRead)
    : session (sessionToRead)
{
    lastMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

const LevelMeters::Level& LevelMeters::channel (int index) const noexcept
{
    return channels[static_cast<size_t> (std::clamp (index, 0, core::Session::maxChannels - 1))];
}

void LevelMeters::resetClip (int index) noexcept
{
    if (index < 0)
        master[0].clipped = master[1].clipped = false;
    else
        channels[static_cast<size_t> (std::clamp (index, 0, core::Session::maxChannels - 1))].clipped = false;
    if (onChange)
        onChange();
}

void LevelMeters::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto seconds = (now - lastMs) / 1000.0;
    lastMs = now;

    auto changed = false;
    auto update = [&] (Level& level, std::atomic<float>& peak)
    {
        const auto next = advance (level, peak.exchange (0.0f), seconds, now);
        // Only what is drawn counts: the hold timestamp moves even in silence.
        changed = changed || ! juce::exactlyEqual (next.db, level.db) ||
                  ! juce::exactlyEqual (next.holdDb, level.holdDb) || next.clipped != level.clipped;
        level = next;
    };

    const auto count = session.channelCount();
    for (int c = 0; c < core::Session::maxChannels; ++c)
    {
        auto& level = channels[static_cast<size_t> (c)];
        if (c < count)
            update (level, session.channel (c).peak);
        else if (! (level == Level {}))
        {
            level = {};
            changed = true;
        }
    }
    update (master[0], session.master.peakLeft);
    update (master[1], session.master.peakRight);

    if (changed && onChange)
        onChange();
}
} // namespace tekno::ui
