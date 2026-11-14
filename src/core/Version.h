// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_core/juce_core.h>

namespace tekno::core
{
struct Version
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    friend constexpr bool operator== (const Version&, const Version&) = default;
};

/** The version of this build, as set by `project(VERSION ...)` in the top-level CMakeLists. */
Version version() noexcept;

/** "0.0.1" */
juce::String versionString();

/** One line for logs and bug reports: "tekno-daw 0.0.1 (Debug, JUCE 9.0.5)". */
juce::String buildDescription();
} // namespace tekno::core
