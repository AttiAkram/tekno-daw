// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/model/Effects.h"
#include "ui/theme/TeknoLookAndFeel.h"

namespace tekno::ui
{
/** What an effect does, drawn with its own DSP's functions, so the picture can't lie:
    - distortion: input across, output up, after the drive; a band shows how far the sound reaches;
    - waveshaper: its curve, whose nine points can be dragged up and down; the same band;
    - delay: the echoes over two bars, left above the line and right below, fading with the feedback;
    - reverb: the tail after the predelay, and dashed, how much sooner the highs die with damping. */
class EffectGraph final : public juce::Component
{
public:
    explicit EffectGraph (TeknoLookAndFeel& teknoLookAndFeel);

    /** What to draw: the effect, its parameters, and how far into its curve the input reached. */
    void show (core::EffectType type, const std::array<float, core::maxEffectParams>& params, float level);

    // The waveshaper's points: a drag is one gesture (one undo step), each move a value of -1..1.
    std::function<void (int point, float value)> onPointMoved;
    std::function<void()> onDragStart, onDragEnd;

    /** A short line under the graph saying how to read it. */
    static juce::String caption (core::EffectType type);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> plot() const;
    juce::Point<float> toScreen (double x, double y) const; // the curves' -1..1 square
    int pointNear (juce::Point<float> position) const;      // a waveshaper point, or -1
    float valueAt (float screenY) const;

    void paintCurve (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintEchoes (juce::Graphics&, juce::Rectangle<float> area) const;
    void paintTail (juce::Graphics&, juce::Rectangle<float> area) const;

    TeknoLookAndFeel& lnf;
    core::EffectType type = core::EffectType::none;
    std::array<float, core::maxEffectParams> params {};
    float level = 0.0f;
    int hover = -1, dragging = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectGraph)
};
} // namespace tekno::ui
