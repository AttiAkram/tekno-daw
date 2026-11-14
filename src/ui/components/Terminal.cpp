// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/components/Terminal.h"

#include "core/commands/CommandLine.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    constexpr float padding = 12.0f;
    constexpr float promptWidth = 16.0f;
} // namespace

Terminal::Terminal (TeknoLookAndFeel& teknoLookAndFeel, core::CommandRegistry& registry)
    : lnf (teknoLookAndFeel)
    , commands (registry)
{
    input.setFont (lnf.fonts().font (tokens::type::body));
    input.setReturnKeyStartsNewLine (false);
    input.setTabKeyUsedAsCharacter (false);
    input.setPopupMenuEnabled (false);
    input.setIndents (0, 0);
    input.setBorder ({});
    input.onReturnKey = [this]
    {
        const auto line = input.getText();
        input.clear();
        runLine (line);
    };
    addAndMakeVisible (input);
    applyColours();

    print ("tekno-daw terminal. Type help for the commands, Tab to complete.", Kind::output);
}

void Terminal::applyColours()
{
    input.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::textColourId, lnf.colour (&P::ink));
    input.setColour (juce::TextEditor::highlightColourId, lnf.colour (&P::line));
    input.setColour (juce::CaretComponent::caretColourId, lnf.colour (&P::ink));
    input.applyColourToAllText (lnf.colour (&P::ink));
}

void Terminal::lookAndFeelChanged()
{
    applyColours();
    repaint();
}

void Terminal::focusInput()
{
    input.grabKeyboardFocus();
}

void Terminal::print (const juce::String& text, Kind kind)
{
    for (const auto& line : juce::StringArray::fromLines (text))
        lines.emplace_back (line, kind);
    while (lines.size() > maxLines)
        lines.pop_front();
    repaint();
}

void Terminal::runLine (const juce::String& raw)
{
    const auto line = raw.trim();
    if (line.isEmpty())
        return;

    history.removeString (line);
    history.add (line);
    historyIndex = history.size();
    print ("> " + line, Kind::echo);

    if (line == "clear")
    {
        lines.clear();
        repaint();
        return;
    }
    if (line == "help" || line.startsWith ("help "))
    {
        print (core::CommandLine::help (commands, line.fromFirstOccurrenceOf ("help", false, false).trim()),
               Kind::output);
        return;
    }

    const auto parsed = core::CommandLine::parse (line, commands);
    if (! parsed.command)
    {
        print (parsed.error, Kind::error);
        return;
    }

    const auto outcome = commands.execute (parsed.command->id, parsed.command->args);
    if (! outcome.succeeded())
        print (outcome.error, Kind::error);
    else if (outcome.note.isNotEmpty())
        print (outcome.note, Kind::output);
    if (onCommandRun)
        onCommandRun();
}

void Terminal::complete()
{
    const auto text = input.getText();
    if (text.containsChar (' '))
        return; // only the command name completes

    const auto matches = core::CommandLine::complete (text, commands);
    if (matches.isEmpty())
        return;
    if (matches.size() == 1)
    {
        input.setText (matches[0] + " ", false);
        input.moveCaretToEnd();
        return;
    }

    // Several: extend to the common prefix and show the choices.
    auto common = matches[0];
    for (const auto& m : matches)
        while (! m.startsWith (common))
            common = common.dropLastCharacters (1);
    input.setText (common, false);
    input.moveCaretToEnd();
    print (matches.joinIntoString ("   "), Kind::output);
}

void Terminal::walkHistory (int direction)
{
    if (history.isEmpty())
        return;
    historyIndex = juce::jlimit (0, history.size(), historyIndex + direction);
    input.setText (historyIndex < history.size() ? history[historyIndex] : juce::String(), false);
    input.moveCaretToEnd();
}

bool Terminal::Input::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::tabKey)
    {
        owner.complete();
        return true;
    }
    if (key == juce::KeyPress::upKey)
    {
        owner.walkHistory (-1);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        owner.walkHistory (1);
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (owner.onEscape)
            owner.onEscape();
        return true;
    }
    if (key.getTextCharacter() == '`')
    {
        if (owner.onToggleKey)
            owner.onToggleKey();
        return true;
    }
    return juce::TextEditor::keyPressed (key);
}

void Terminal::resized()
{
    auto area = getLocalBounds().toFloat().reduced (padding);
    input.setBounds (area.removeFromBottom (24.0f).withTrimmedLeft (promptWidth).toNearestInt());
}

void Terminal::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (tokens::shape::lineWidth * 0.5f);
    g.setColour (lnf.colour (&P::surface));
    g.fillRoundedRectangle (bounds, tokens::shape::fieldRadius);
    g.setColour (input.hasKeyboardFocus (false) ? lnf.colour (&P::ink) : lnf.colour (&P::line));
    g.drawRoundedRectangle (bounds, tokens::shape::fieldRadius, tokens::shape::lineWidth);

    const auto font = lnf.fonts().font (tokens::type::meta);
    g.setFont (lnf.fonts().font (tokens::type::body));
    g.setColour (lnf.colour (&P::ink));
    g.drawText (">", input.getBounds().toFloat().withX (padding).withWidth (promptWidth),
                juce::Justification::centredLeft);

    // Newest line just above the input, older ones upwards until there is no room.
    g.setFont (font);
    auto y = static_cast<float> (input.getY()) - lineHeight - 6.0f;
    const auto width = bounds.getWidth() - padding * 2.0f;
    for (auto it = lines.rbegin(); it != lines.rend() && y >= padding - 2.0f; ++it, y -= lineHeight)
    {
        const auto& [text, kind] = *it;
        g.setColour (kind == Kind::output ? lnf.colour (&P::inkMuted) : lnf.colour (&P::ink));
        g.drawText (kind == Kind::error ? "error: " + text : text,
                    juce::Rectangle<float> (padding, y, width, lineHeight), juce::Justification::centredLeft, true);
    }
}
} // namespace tekno::ui
