// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/Version.h"

#include "core/BuildInfo.h"

namespace tekno::core
{
Version version() noexcept
{
    return { build::versionMajor, build::versionMinor, build::versionPatch };
}

juce::String versionString()
{
    return build::versionString;
}

juce::String buildDescription()
{
    juce::String buildType (build::buildType);
    if (buildType.isEmpty())
        buildType = "unknown build type";

    return "tekno-daw " + versionString() + " (" + buildType + ", " + juce::SystemStats::getJUCEVersion() + ")";
}
} // namespace tekno::core
