// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/TileLayout.h"

namespace tekno::ui
{
struct TileLayout::Node
{
    bool leaf = true;
    Panel panel = Panel::pattern;
    bool sideBySide = true; // inner nodes: true = a | b, false = a over b
    float ratio = 0.5f;     // share of the first child
    std::unique_ptr<Node> a, b;

    std::unique_ptr<Node> clone() const
    {
        auto n = std::make_unique<Node>();
        n->leaf = leaf;
        n->panel = panel;
        n->sideBySide = sideBySide;
        n->ratio = ratio;
        if (a)
            n->a = a->clone();
        if (b)
            n->b = b->clone();
        return n;
    }
};

namespace
{
    constexpr std::pair<TileLayout::Panel, const char*> names[] {
        { TileLayout::Panel::pattern, "pattern" },
        { TileLayout::Panel::pianoRoll, "pianoroll" },
        { TileLayout::Panel::mixer, "mixer" },
        { TileLayout::Panel::keys, "keys" },
    };

    /** The two halves of `area` for a split, with `gap` between them. */
    std::pair<juce::Rectangle<float>, juce::Rectangle<float>> halves (juce::Rectangle<float> area, bool sideBySide,
                                                                      float ratio, float gap)
    {
        if (sideBySide)
        {
            const auto first = std::round ((area.getWidth() - gap) * ratio);
            auto rest = area;
            const auto a = rest.removeFromLeft (first);
            rest.removeFromLeft (gap);
            return { a, rest };
        }
        const auto first = std::round ((area.getHeight() - gap) * ratio);
        auto rest = area;
        const auto a = rest.removeFromTop (first);
        rest.removeFromTop (gap);
        return { a, rest };
    }
} // namespace

juce::String TileLayout::panelName (Panel panel)
{
    for (const auto& [p, name] : names)
        if (p == panel)
            return name;
    return "pattern";
}

std::optional<TileLayout::Panel> TileLayout::panelFromName (const juce::String& raw)
{
    const auto name = raw.trim().toLowerCase();
    if (name == "rack" || name == "steps" || name == "channels")
        return Panel::pattern;
    if (name == "piano" || name == "roll")
        return Panel::pianoRoll;
    for (const auto& [p, n] : names)
        if (name == n)
            return p;
    return std::nullopt;
}

TileLayout::TileLayout (Panel only)
    : root (std::make_unique<Node>())
    , activePanel (only)
{
    root->panel = only;
}

TileLayout::TileLayout (const TileLayout& other)
    : root (other.root->clone())
    , activePanel (other.activePanel)
{
}

TileLayout& TileLayout::operator= (const TileLayout& other)
{
    root = other.root->clone();
    activePanel = other.activePanel;
    return *this;
}

TileLayout::~TileLayout() = default;

std::vector<TileLayout::Panel> TileLayout::panels() const
{
    std::vector<Panel> list;
    std::function<void (const Node&)> walk = [&] (const Node& n)
    {
        if (n.leaf)
            list.push_back (n.panel);
        else
            walk (*n.a), walk (*n.b);
    };
    walk (*root);
    return list;
}

bool TileLayout::isOpen (Panel panel) const
{
    const auto list = panels();
    return std::find (list.begin(), list.end(), panel) != list.end();
}

int TileLayout::count() const
{
    return static_cast<int> (panels().size());
}

bool TileLayout::focus (Panel panel)
{
    if (! isOpen (panel))
        return false;
    activePanel = panel;
    return true;
}

void TileLayout::focusNext (int direction)
{
    const auto list = panels();
    const auto at = static_cast<int> (std::find (list.begin(), list.end(), activePanel) - list.begin());
    const auto n = static_cast<int> (list.size());
    activePanel = list[static_cast<size_t> (((at + direction) % n + n) % n)];
}

namespace
{
    /** The leaf holding `panel`, and its parent (null for the root). */
    template <typename NodeT>
    std::pair<NodeT*, NodeT*> findLeaf (NodeT* node, TileLayout::Panel panel, NodeT* parent = nullptr)
    {
        if (node->leaf)
            return node->panel == panel ? std::pair { node, parent } : std::pair<NodeT*, NodeT*> { nullptr, nullptr };
        if (auto found = findLeaf (node->a.get(), panel, node); found.first != nullptr)
            return found;
        return findLeaf (node->b.get(), panel, node);
    }
} // namespace

void TileLayout::show (Panel panel)
{
    if (focus (panel))
        return;
    findLeaf (root.get(), activePanel).first->panel = panel;
    activePanel = panel;
}

void TileLayout::open (Panel panel, juce::Rectangle<float> area, float gap)
{
    if (focus (panel))
        return;

    // Split along the longer side of the active tile.
    auto activeArea = area;
    for (const auto& t : tiles (area, gap))
        if (t.panel == activePanel)
            activeArea = t.bounds;

    auto* leaf = findLeaf (root.get(), activePanel).first;
    leaf->a = std::make_unique<Node>();
    leaf->a->panel = leaf->panel;
    leaf->b = std::make_unique<Node>();
    leaf->b->panel = panel;
    leaf->leaf = false;
    // Side by side only when clearly wide: near-square tiles split top/bottom, so nothing gets thin.
    leaf->sideBySide = activeArea.getWidth() >= activeArea.getHeight() * 1.3f;
    leaf->ratio = 0.5f;
    activePanel = panel;
}

bool TileLayout::close()
{
    if (root->leaf)
        return false;
    auto [leaf, parent] = findLeaf (root.get(), activePanel);
    // The sibling takes the parent's place.
    auto sibling = parent->a.get() == leaf ? std::move (parent->b) : std::move (parent->a);
    *parent = std::move (*sibling);
    // The new active panel: the first leaf of what took the space.
    auto* n = parent;
    while (! n->leaf)
        n = n->a.get();
    activePanel = n->panel;
    return true;
}

void TileLayout::single()
{
    root = std::make_unique<Node>();
    root->panel = activePanel;
}

std::vector<TileLayout::Tile> TileLayout::tiles (juce::Rectangle<float> area, float gap) const
{
    std::vector<Tile> list;
    std::function<void (const Node&, juce::Rectangle<float>)> walk = [&] (const Node& n, juce::Rectangle<float> r)
    {
        if (n.leaf)
        {
            list.push_back ({ n.panel, r });
            return;
        }
        const auto [a, b] = halves (r, n.sideBySide, n.ratio, gap);
        walk (*n.a, a);
        walk (*n.b, b);
    };
    walk (*root, area);
    return list;
}

std::vector<TileLayout::Divider> TileLayout::dividers (juce::Rectangle<float> area, float gap) const
{
    std::vector<Divider> list;
    std::function<void (const Node&, juce::Rectangle<float>, const juce::String&)> walk =
        [&] (const Node& n, juce::Rectangle<float> r, const juce::String& id)
    {
        if (n.leaf)
            return;
        const auto [a, b] = halves (r, n.sideBySide, n.ratio, gap);
        const auto between =
            n.sideBySide ? juce::Rectangle<float> (a.getRight(), r.getY(), b.getX() - a.getRight(), r.getHeight())
                         : juce::Rectangle<float> (r.getX(), a.getBottom(), r.getWidth(), b.getY() - a.getBottom());
        list.push_back ({ id, between, r, n.sideBySide });
        walk (*n.a, a, id + "a");
        walk (*n.b, b, id + "b");
    };
    walk (*root, area, {});
    return list;
}

void TileLayout::setRatio (const juce::String& id, float ratio)
{
    auto* n = root.get();
    for (const auto c : id)
    {
        if (n->leaf)
            return;
        n = c == 'a' ? n->a.get() : n->b.get();
    }
    if (! n->leaf)
        n->ratio = std::clamp (ratio, 0.2f, 0.8f);
}

juce::String TileLayout::toString() const
{
    std::function<juce::String (const Node&)> write = [&] (const Node& n) -> juce::String
    {
        if (n.leaf)
            return panelName (n.panel);
        return juce::String (n.sideBySide ? "h(" : "v(") + juce::String (n.ratio, 3) + "," + write (*n.a) + "," +
               write (*n.b) + ")";
    };
    return write (*root) + ";" + panelName (activePanel);
}

std::optional<TileLayout> TileLayout::fromString (const juce::String& text)
{
    const auto tree = text.upToFirstOccurrenceOf (";", false, false).removeCharacters (" ");
    int pos = 0;
    std::function<std::unique_ptr<Node>()> read = [&]() -> std::unique_ptr<Node>
    {
        auto n = std::make_unique<Node>();
        if (tree.substring (pos).startsWith ("h(") || tree.substring (pos).startsWith ("v("))
        {
            n->leaf = false;
            n->sideBySide = tree[pos] == 'h';
            pos += 2;
            const auto comma = tree.indexOfChar (pos, ',');
            if (comma < 0)
                return nullptr;
            n->ratio = std::clamp (tree.substring (pos, comma).getFloatValue(), 0.2f, 0.8f);
            pos = comma + 1;
            n->a = read();
            if (n->a == nullptr || tree[pos] != ',')
                return nullptr;
            ++pos;
            n->b = read();
            if (n->b == nullptr || tree[pos] != ')')
                return nullptr;
            ++pos;
            return n;
        }
        auto end = pos;
        while (end < tree.length() && juce::CharacterFunctions::isLetter (tree[end]))
            ++end;
        const auto panel = panelFromName (tree.substring (pos, end));
        if (! panel)
            return nullptr;
        n->panel = *panel;
        pos = end;
        return n;
    };

    auto root = read();
    if (root == nullptr || pos != tree.length())
        return std::nullopt;

    TileLayout layout;
    layout.root = std::move (root);
    const auto list = layout.panels();
    for (size_t i = 0; i < list.size(); ++i) // each panel at most once
        for (size_t j = i + 1; j < list.size(); ++j)
            if (list[i] == list[j])
                return std::nullopt;
    layout.activePanel = list.front();
    if (const auto active = panelFromName (text.fromFirstOccurrenceOf (";", false, false)))
        layout.focus (*active);
    return layout;
}
} // namespace tekno::ui
