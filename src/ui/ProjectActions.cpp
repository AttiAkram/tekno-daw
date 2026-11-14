// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/ProjectActions.h"

#include "core/io/ProjectFile.h"

namespace tekno::ui
{
namespace
{
    constexpr int resultCancel = 0, resultSave = 1, resultDiscard = 2;
    const juce::String filePattern = juce::String ("*") + core::io::project::extension;
} // namespace

ProjectActions::ProjectActions (core::Session& s, core::CommandRegistry& c)
    : session (s)
    , commands (c)
{
}

juce::String ProjectActions::documentName() const
{
    return session.project.file == juce::File() ? "untitled" : session.project.file.getFileName();
}

juce::String ProjectActions::documentTitle() const
{
    return documentName() + (session.project.hasUnsavedChanges() ? "  \xe2\x80\xa2" : "");
}

juce::File ProjectActions::defaultFolder() const
{
    if (session.project.file != juce::File())
        return session.project.file.getParentDirectory();
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("tekno-daw");
}

void ProjectActions::showError (const juce::String& what, const juce::String& why) const
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (what)
                                      .withMessage (why)
                                      .withButton ("OK", 1),
                                  nullptr);
}

void ProjectActions::confirmDiscard (std::function<void()> then)
{
    if (! session.project.hasUnsavedChanges())
    {
        then();
        return;
    }

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Save the changes to " + documentName() + " first?")
                                      .withButton ("Save", resultSave)
                                      .withButton ("Don't save", resultDiscard)
                                      .withButton ("Cancel", resultCancel),
                                  [this, then] (int result)
                                  {
                                      if (result == resultSave)
                                          save (then);
                                      else if (result == resultDiscard)
                                          then();
                                  });
}

void ProjectActions::newProject()
{
    confirmDiscard ([this] { commands.execute ("project.new"); });
}

void ProjectActions::openProject()
{
    confirmDiscard (
        [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Open a project", defaultFolder(), filePattern);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto file = fc.getResult();
                                      if (file == juce::File())
                                          return; // cancelled
                                      const auto outcome =
                                          commands.execute ("project.open", { { "path", file.getFullPathName() } });
                                      if (! outcome.succeeded())
                                          showError ("Could not open the project", outcome.error);
                                  });
        });
}

void ProjectActions::save (std::function<void()> afterSaving)
{
    if (session.project.file == juce::File())
    {
        saveAs (std::move (afterSaving));
        return;
    }

    const auto outcome = commands.execute ("project.save");
    if (! outcome.succeeded())
        showError ("Could not save the project", outcome.error);
    else if (afterSaving)
        afterSaving();
}

void ProjectActions::saveAs (std::function<void()> afterSaving)
{
    const auto folder = defaultFolder();
    folder.createDirectory();
    const auto suggested =
        session.project.file != juce::File()
            ? session.project.file
            : folder.getChildFile (session.project.patternNames[0].replaceCharacters (" /\\", "---") +
                                   core::io::project::extension);

    chooser = std::make_unique<juce::FileChooser> ("Save the project", suggested, filePattern);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, afterSaving] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return; // cancelled
                              const auto outcome =
                                  commands.execute ("project.saveAs", { { "path", file.getFullPathName() } });
                              if (! outcome.succeeded())
                                  showError ("Could not save the project", outcome.error);
                              else if (afterSaving)
                                  afterSaving();
                          });
}
} // namespace tekno::ui
