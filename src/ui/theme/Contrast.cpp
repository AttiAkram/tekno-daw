// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/theme/Contrast.h"

#include <algorithm>
#include <cmath>

namespace tekno::ui
{
namespace
{
    double linearise (std::uint32_t channel) noexcept
    {
        const auto c = static_cast<double> (channel & 0xffu) / 255.0;
        return c <= 0.03928 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4);
    }
} // namespace

double relativeLuminance (std::uint32_t argb) noexcept
{
    return 0.2126 * linearise (argb >> 16) + 0.7152 * linearise (argb >> 8) + 0.0722 * linearise (argb);
}

double contrastRatio (std::uint32_t a, std::uint32_t b) noexcept
{
    const auto la = relativeLuminance (a);
    const auto lb = relativeLuminance (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}
} // namespace tekno::ui
