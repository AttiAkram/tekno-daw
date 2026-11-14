// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"

namespace tekno::core
{
/** Registers every command that acts on a Session: transport, pattern, synth, undo/redo.

    Undoable: pattern edits. Not undoable (yet): transport, tempo and synth knobs, because a knob drag
    produces dozens of changes and history merging does not exist yet. */
void registerSessionCommands (CommandRegistry& registry, Session& session);
} // namespace tekno::core
