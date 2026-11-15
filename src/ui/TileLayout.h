// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <juce_graphics/juce_graphics.h>

namespace tekno::ui
{
/** Where the work panels sit: a binary tree, Hyprland's "dwindle". Each leaf is a panel; each inner
    node splits its space in two. Opening a panel splits the active one along its longer side;
    closing one gives its space to its neighbour. Plain logic, no window: the shell asks it for
    rectangles. */
class TileLayout
{
public:
    enum class Panel
    {
        pattern,
        pianoRoll,
        mixer,
        keys,
        effects,
        keyboard, // play live
    };
    static juce::String panelName (Panel panel); // "pattern", "pianoroll", "mixer", "keys"
    static std::optional<Panel> panelFromName (const juce::String& name);

    explicit TileLayout (Panel only = Panel::pattern);
    TileLayout (const TileLayout& other);
    TileLayout& operator= (const TileLayout& other);
    ~TileLayout();

    Panel active() const noexcept { return activePanel; }
    bool isOpen (Panel panel) const;
    int count() const;
    std::vector<Panel> panels() const; // left to right, top to bottom

    /** Makes an open panel the active one. False when it isn't open. */
    bool focus (Panel panel);
    void focusNext (int direction); // +1 or -1, wrapping

    /** Puts `panel` where the active one is (or just activates it, if it is open elsewhere). */
    void show (Panel panel);

    /** Opens `panel` beside the active one, splitting it along its longer side inside `area`.
        If it is already open it just becomes active. */
    void open (Panel panel, juce::Rectangle<float> area, float gap);

    /** Closes the active panel; its neighbour takes the space. False when it is the last one. */
    bool close();

    /** Only the active panel stays. */
    void single();

    struct Tile
    {
        Panel panel;
        juce::Rectangle<float> bounds;
    };
    std::vector<Tile> tiles (juce::Rectangle<float> area, float gap) const;

    /** The gaps between tiles, which can be dragged. `id` names the split for setRatio(). */
    struct Divider
    {
        juce::String id;
        juce::Rectangle<float> bounds; // the gap, including the space parent area of the split
        juce::Rectangle<float> splitArea;
        bool vertical; // true: the panels are side by side, the divider runs top to bottom
    };
    std::vector<Divider> dividers (juce::Rectangle<float> area, float gap) const;
    void setRatio (const juce::String& id, float ratio); // kept within 0.2 .. 0.8

    /** "h(0.55,pattern,v(0.6,pianoroll,mixer))", and back. Unusable text gives nothing. */
    juce::String toString() const;
    static std::optional<TileLayout> fromString (const juce::String& text);

private:
    struct Node;
    std::unique_ptr<Node> root;
    Panel activePanel;
};
} // namespace tekno::ui
