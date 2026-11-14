// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <functional>

#include <juce_events/juce_events.h>

#include "core/model/Session.h"

namespace tekno::ui
{
/** The one reader of the levels the audio thread publishes: it takes every channel's and the master's
    peak (exchange with 0) 60 times a second and turns them into what meters show. Every view reads
    from here, so two views never steal peaks from each other. */
class LevelMeters final : private juce::Timer
{
public:
    static constexpr float minDb = -60.0f; // the bottom of every meter: silence
    static constexpr float maxDb = 6.0f;

    struct Level
    {
        float db = minDb;     // what the bar shows: rises at once, falls at a fixed rate
        float holdDb = minDb; // the peak line
        double holdSince = 0.0;
        bool clipped = false; // a sample reached full scale; stays on until reset

        bool operator== (const Level&) const = default;
    };

    /** One step of the meter ballistics: `peak` is the loudest linear sample since the last step. */
    static Level advance (Level level, float peak, double secondsSinceLast, double nowMs) noexcept;

    explicit LevelMeters (core::Session& session);

    const Level& channel (int index) const noexcept;
    const Level& masterLeft() const noexcept { return master[0]; }
    const Level& masterRight() const noexcept { return master[1]; }

    /** Turns off the clip light of a channel, or of the master with -1. */
    void resetClip (int channel) noexcept;

    /** Called after each update in which something changed, to repaint. */
    std::function<void()> onChange;

private:
    void timerCallback() override;

    core::Session& session;
    std::array<Level, core::Session::maxChannels> channels;
    std::array<Level, 2> master;
    double lastMs = 0.0;
};
} // namespace tekno::ui
