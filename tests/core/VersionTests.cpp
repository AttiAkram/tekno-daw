// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <catch2/catch_test_macros.hpp>

#include "core/BuildInfo.h"
#include "core/Version.h"

using namespace tekno::core;

TEST_CASE ("version matches the CMake project version", "[core][version]")
{
    const auto v = version();
    CHECK (v == Version { build::versionMajor, build::versionMinor, build::versionPatch });
    CHECK (versionString() == juce::String (v.major) + "." + juce::String (v.minor) + "." + juce::String (v.patch));
}

TEST_CASE ("build description names the app and the JUCE version", "[core][version]")
{
    const auto text = buildDescription();
    CHECK (text.startsWith ("tekno-daw " + versionString()));
    CHECK (text.contains ("JUCE"));
}
