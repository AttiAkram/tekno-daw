// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "core/model/Session.h"

namespace tekno::ui
{
/** The interactive side of project files: dialogs, "save changes?" questions, error boxes.
    It only asks; the work is done by the project.* commands, which never ask anything. */
class ProjectActions
{
public:
    ProjectActions (core::Session& session, core::CommandRegistry& commands);

    void newProject();
    void openProject();

    /** Asks for an audio file and loads it into a sampler channel (0-based). */
    void chooseSample (int channel);
    void save (std::function<void()> afterSaving = {});
    void saveAs (std::function<void()> afterSaving = {});

    /** Runs `then` straight away, or after asking to save if there are unsaved changes. */
    void confirmDiscard (std::function<void()> then);

    /** "acid-night.tekno •" — the dot means unsaved changes. */
    juce::String documentTitle() const;
    juce::String documentName() const;

private:
    juce::File defaultFolder() const;
    void showError (const juce::String& what, const juce::String& why) const;

    core::Session& session;
    core::CommandRegistry& commands;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace tekno::ui
