// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <juce_core/juce_core.h>

namespace tekno::core::io
{
/** The demo project, built into the program from examples/demo.tekno: an original freetekno loop
    set that opens at start, so there is something to play without a file to find. */
juce::String demoProjectText();
} // namespace tekno::core::io
