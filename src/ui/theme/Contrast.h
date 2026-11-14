// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <cstdint>

namespace tekno::ui
{
/** WCAG 2.x relative luminance of an 0xAARRGGBB colour (alpha ignored), 0..1. */
double relativeLuminance (std::uint32_t argb) noexcept;

/** WCAG 2.x contrast ratio between two colours, 1..21. Order does not matter. */
double contrastRatio (std::uint32_t a, std::uint32_t b) noexcept;

namespace wcag
{
    inline constexpr double normalText = 4.5; // AA, body-size text
    inline constexpr double graphics = 3.0;   // AA, non-text shapes and large text
} // namespace wcag
} // namespace tekno::ui
