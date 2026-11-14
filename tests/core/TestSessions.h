// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include "core/model/Session.h"

namespace tekno::test
{
/** The default project without the drums: only the acid channel (now channel 0) and its line in
    pattern 1. The setup the synth and engine timing tests measure, so drums don't get in the way. */
inline void acidOnly (core::Session& session)
{
    session.loadDefaultProject();
    while (session.channelCount() > 1)
        session.removeChannel (0);
}
} // namespace tekno::test
