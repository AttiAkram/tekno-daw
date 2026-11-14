// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <deque>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/commands/CommandRegistry.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** The built-in terminal: type a command, it runs through the same registry as the mouse and keys.
    Enter runs, Tab completes, Up/Down walk the history, Esc gives the keyboard back to the view,
    ` closes it. Built-ins: help [topic], clear. */
class Terminal final : public juce::Component
{
public:
    Terminal (TeknoLookAndFeel& lookAndFeel, core::CommandRegistry& commands);

    std::function<void()> onCommandRun; // so the view can refresh at once
    std::function<void()> onEscape;     // give the keyboard back
    std::function<void()> onToggleKey;  // ` pressed inside the terminal

    void focusInput();
    void runLine (const juce::String& line); // as if typed and Enter pressed

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    enum class Kind
    {
        echo,
        output,
        error,
    };

    class Input final : public juce::TextEditor
    {
    public:
        explicit Input (Terminal& t)
            : owner (t)
        {
        }
        bool keyPressed (const juce::KeyPress&) override;

    private:
        Terminal& owner;
    };

    void print (const juce::String& text, Kind kind);
    void complete();
    void walkHistory (int direction);
    void applyColours();

    TeknoLookAndFeel& lnf;
    core::CommandRegistry& commands;
    Input input { *this };

    std::deque<std::pair<juce::String, Kind>> lines;
    juce::StringArray history;
    int historyIndex = 0;

    static constexpr size_t maxLines = 400;
    static constexpr float lineHeight = 18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Terminal)
};
} // namespace tekno::ui
