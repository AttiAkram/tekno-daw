// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/model/Pattern.h"

#include <algorithm>

namespace tekno::core
{
std::uint32_t Step::pack() const noexcept
{
    const auto clampedNote = static_cast<std::uint32_t> (std::clamp (note, minNote, maxNote));
    return (on ? 1u : 0u) | (accent ? 2u : 0u) | (slide ? 4u : 0u) | (clampedNote << 8);
}

Step Step::unpack (std::uint32_t bits) noexcept
{
    return { (bits & 1u) != 0, (bits & 2u) != 0, (bits & 4u) != 0, static_cast<int> ((bits >> 8) & 0x7fu) };
}

Pattern::Pattern() noexcept
{
    for (auto& step : steps)
        step.store (Step {}.pack(), std::memory_order_relaxed);
}

Step Pattern::getStep (int index) const noexcept
{
    if (! isValidIndex (index))
        return {};
    return Step::unpack (steps[static_cast<size_t> (index)].load (std::memory_order_relaxed));
}

void Pattern::setStep (int index, const Step& step) noexcept
{
    if (isValidIndex (index))
        steps[static_cast<size_t> (index)].store (step.pack(), std::memory_order_relaxed);
}
} // namespace tekno::core
