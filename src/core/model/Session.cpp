// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Session.h"

namespace tekno::core
{
Session::Session()
{
    loadDefaultPattern();
}

void Session::loadDefaultPattern() noexcept
{
    // A minor acid line: A2 root with octave jumps, accents on the off-beats, three slides.
    constexpr int A2 = 45, C3 = 48, D3 = 50, E3 = 52, G3 = 55, A3 = 57;
    const Step line[Pattern::numSteps] = {
        { true, true, false, A2 },   { false, false, false, A2 }, { true, false, true, A3 },
        { true, false, false, A2 },  { true, false, false, C3 },  { false, false, false, A2 },
        { true, true, false, A2 },   { true, false, true, E3 },   { true, false, false, G3 },
        { false, false, false, A2 }, { true, false, false, A2 },  { true, true, true, A3 },
        { false, false, false, A2 }, { true, false, false, D3 },  { true, false, true, A2 },
        { true, false, false, A3 },
    };

    for (int i = 0; i < Pattern::numSteps; ++i)
        pattern.setStep (i, line[i]);
}
} // namespace tekno::core
