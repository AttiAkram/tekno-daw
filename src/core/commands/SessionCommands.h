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
/** Registers every command that acts on a Session: transport, patterns, synth, project files and
    undo/redo. Pattern commands act on the selected pattern.

    Undoable: everything saved in the project. Knobs, tempo and swing coalesce: a whole drag between
    beginGesture()/endGesture() is one undo step. Not undoable: play/stop, pattern selection.

    Project commands never ask questions (unsaved changes, overwrite): asking is the job of the
    caller (the UI today, a terminal or remote client tomorrow). */
void registerSessionCommands (CommandRegistry& registry, Session& session);
} // namespace tekno::core
