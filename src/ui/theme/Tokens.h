// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <cstdint>

// Design tokens: the ONLY place in the code base where colours, font sizes, radii, spacing and
// durations are written down. Components read from here and never hard-code values.
// Change values here only together with the design spec they implement.
//
// Pure data on purpose: no JUCE types, so it can be read by tests, tools and a future web client.

// The token tables are aligned by hand so they read like a spec sheet: keep the formatter out.
// clang-format off
namespace tekno::ui::tokens
{
// --- colour -------------------------------------------------------------------------------------

/** Colours are 0xAARRGGBB, the layout juce::Colour takes. */
struct Palette
{
    std::uint32_t bg;            // page background
    std::uint32_t surface;       // secondary panels
    std::uint32_t ink;           // text, active borders, fills
    std::uint32_t inkMuted;      // secondary text, metadata
    std::uint32_t line;          // borders and dividers, always 1px
    std::uint32_t accent;        // the primary action ONLY - never decoration
    std::uint32_t onAccent;      // text and icons on top of accent
    std::uint32_t accentOutline; // edge of accent shapes: yellow on white is 1.41:1, it needs one
    std::uint32_t ok;            // semantic, small sizes only
    std::uint32_t onOk;          // letter on an ok badge (white on green is only 2.28:1)
    std::uint32_t warn;          // semantic, small sizes only
    std::uint32_t onWarn;
};

inline constexpr Palette lightPalette {
    .bg            = 0xffffffff,
    .surface       = 0xfff4f4f4,
    .ink           = 0xff111111,
    .inkMuted      = 0xff6b6b6b,
    .line          = 0xffd9d9d9,
    .accent        = 0xffffd60a,
    .onAccent      = 0xff111111,
    .accentOutline = 0xff111111,
    .ok            = 0xff22c55e,
    .onOk          = 0xff111111,
    .warn          = 0xffe11d2e,
    .onWarn        = 0xffffffff,
};

// bg, ink and accent come from the spec; surface, inkMuted and line are derived to keep the
// light theme's ratios (design-system.md, section 2) and still need sign-off.
inline constexpr Palette darkPalette {
    .bg            = 0xff111111,
    .surface       = 0xff1c1c1c,
    .ink           = 0xfff4f4f4,
    .inkMuted      = 0xff9a9a9a,
    .line          = 0xff2e2e2e,
    .accent        = 0xffffd60a,
    .onAccent      = 0xff111111,
    .accentOutline = 0xffffd60a, // same as the fill: on dark the yellow has its own edge
    .ok            = 0xff22c55e,
    .onOk          = 0xff111111,
    .warn          = 0xffe11d2e,
    .onWarn        = 0xffffffff,
};

enum class Theme
{
    dark,
    light,
};

/** Provisional default until the open question in design-system.md is closed. */
inline constexpr Theme defaultTheme = Theme::dark;

constexpr const Palette& palette (Theme theme) noexcept
{
    return theme == Theme::light ? lightPalette : darkPalette;
}

// --- typography ---------------------------------------------------------------------------------

enum class Family
{
    condensed, // Barlow Condensed - page titles only
    sans,      // Inter - everything else
};

/** Sizes are CSS-like point sizes in logical pixels. */
struct TextStyle
{
    Family family;
    float size;
    int weight;      // 400 regular, 600 semibold, 700 bold, 800 extra-bold
    float tracking;  // extra letter spacing, as a fraction of the size
    bool uppercase;
};

namespace type
{
    inline constexpr TextStyle pageTitle    { Family::condensed, 84.0f, 800,  0.0f,  true  }; // spec: 72-96
    inline constexpr TextStyle viewTitle    { Family::condensed, 44.0f, 800,  0.0f,  true  }; // proposal: 40-48 in work views
    inline constexpr TextStyle sectionTitle { Family::sans,      17.0f, 600,  0.0f,  false };
    inline constexpr TextStyle body         { Family::sans,      15.0f, 400,  0.0f,  false };
    inline constexpr TextStyle meta         { Family::sans,      13.0f, 400,  0.0f,  false };
    inline constexpr TextStyle control      { Family::sans,      13.0f, 600,  0.02f, true  }; // pills, nav
    inline constexpr TextStyle wordmark     { Family::sans,      20.0f, 700, -0.04f, false }; // "tekno-daw"
} // namespace type

// --- shape --------------------------------------------------------------------------------------

namespace shape
{
    inline constexpr float lineWidth         = 1.0f;
    inline constexpr float iconStroke        = 1.5f;
    inline constexpr float pillHeight        = 36.0f;
    inline constexpr float pillHeightCompact = 24.0f;  // proposal: dense editing views only
    inline constexpr float iconButtonSize    = 40.0f;
    inline constexpr float fieldRadius       = 10.0f;  // spec: 10-12, fields and panels
    inline constexpr float contentRadius     = 3.0f;   // spec: 2-4, content areas
    inline constexpr float badgeSize         = 18.0f;
    inline constexpr float badgeRadius       = 4.0f;
    inline constexpr float stepRadius        = 4.0f;
    inline constexpr float navDotSize        = 4.0f;
    inline constexpr float dashLength        = 4.0f;
    inline constexpr float dashGap           = 4.0f;
    inline constexpr float meterWidth        = 6.0f;
    inline constexpr float faderWidth        = 28.0f;
} // namespace shape

// --- space --------------------------------------------------------------------------------------

namespace space
{
    inline constexpr float unit        = 4.0f;   // everything is a multiple of this
    inline constexpr float gap         = 12.0f;  // grid gap
    inline constexpr float gapCompact  = 8.0f;
    inline constexpr float gutter      = 24.0f;  // page margin: "tanta aria"
    inline constexpr float leftColumn  = 250.0f;
    inline constexpr float topBar      = 64.0f;  // not in the spec: provisional
    inline constexpr int   gridColumns = 4;      // also the 4 beats of a 16-step bar
} // namespace space

// --- motion -------------------------------------------------------------------------------------

namespace motion
{
    inline constexpr int fastMs   = 120;
    inline constexpr int normalMs = 150;

    // Level meters: rise at once, fall at this rate; the peak line holds before it falls.
    inline constexpr float meterFallDbPerSecond = 24.0f;
    inline constexpr int   meterHoldMs          = 1500;
} // namespace motion
} // namespace tekno::ui::tokens
// clang-format on
