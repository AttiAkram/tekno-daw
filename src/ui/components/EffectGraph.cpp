// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/components/EffectGraph.h"

#include "core/dsp/Distortion.h"
#include "core/dsp/PingPongDelay.h"
#include "core/dsp/PlateReverb.h"
#include "core/dsp/Shaper.h"

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::EffectType;

    constexpr int curveSteps = 160; // segments the curves are drawn with
    constexpr float echoBars = 2.0f;

    std::array<float, core::shaperPoints> pointsOf (const std::array<float, core::maxEffectParams>& params)
    {
        std::array<float, core::shaperPoints> points {};
        for (size_t k = 0; k < points.size(); ++k)
            points[k] = params[static_cast<size_t> (core::dsp::Shaper::firstPoint) + k];
        return points;
    }
} // namespace

EffectGraph::EffectGraph (TeknoLookAndFeel& teknoLookAndFeel)
    : lnf (teknoLookAndFeel)
{
    setInterceptsMouseClicks (true, false);
}

void EffectGraph::show (EffectType newType, const std::array<float, core::maxEffectParams>& newParams, float newLevel)
{
    if (newType == type && newParams == params && std::abs (newLevel - level) < 0.005f)
        return;
    type = newType;
    params = newParams;
    level = newLevel;
    setMouseCursor (type == EffectType::shaper ? juce::MouseCursor::UpDownResizeCursor
                                               : juce::MouseCursor::NormalCursor);
    repaint();
}

juce::String EffectGraph::caption (EffectType t)
{
    switch (t)
    {
    case EffectType::distortion:
        return juce::String::fromUTF8 ("in → out after drive; grey = your sound");
    case EffectType::shaper:
        return "drag the points; grey = your sound";
    case EffectType::delay:
        return "echoes over 2 bars: L up, R down";
    case EffectType::reverb:
        return "the tail; dashed = the highs";
    case EffectType::none:
        break;
    }
    return {};
}

juce::Rectangle<float> EffectGraph::plot() const
{
    return getLocalBounds().toFloat().reduced (space::unit * 2.0f);
}

juce::Point<float> EffectGraph::toScreen (double x, double y) const
{
    const auto area = plot();
    return { area.getX() + static_cast<float> ((x + 1.0) * 0.5) * area.getWidth(),
             area.getY() + static_cast<float> ((1.0 - y) * 0.5) * area.getHeight() };
}

float EffectGraph::valueAt (float screenY) const
{
    const auto area = plot();
    return std::clamp (1.0f - 2.0f * (screenY - area.getY()) / std::max (1.0f, area.getHeight()), -1.0f, 1.0f);
}

int EffectGraph::pointNear (juce::Point<float> position) const
{
    if (type != EffectType::shaper)
        return -1;
    const auto column = plot().getWidth() / static_cast<float> (core::shaperPoints - 1);
    for (int k = 0; k < core::shaperPoints; ++k)
    {
        const auto x = -1.0 + core::dsp::Shaper::spacing * k;
        if (std::abs (toScreen (x, 0.0).x - position.x) <= column * 0.5f)
            return k;
    }
    return -1;
}

// --- drawing ------------------------------------------------------------------------------------------

void EffectGraph::paint (juce::Graphics& g)
{
    if (type == EffectType::none)
        return;
    const auto area = plot();
    g.setColour (lnf.colour (&P::line));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (shape::lineWidth * 0.5f), shape::contentRadius,
                            shape::lineWidth);
    if (type == EffectType::distortion || type == EffectType::shaper)
        paintCurve (g, area);
    else if (type == EffectType::delay)
        paintEchoes (g, area);
    else if (type == EffectType::reverb)
        paintTail (g, area);
}

void EffectGraph::paintCurve (juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto ink = lnf.colour (&P::ink), line = lnf.colour (&P::line);

    // The band: the part of the curve the sound has reached lately. Past the end it is clipping.
    const auto reach = std::min (level, 1.0f);
    if (reach > 0.001f)
    {
        const auto left = toScreen (-reach, 0.0).x, right = toScreen (reach, 0.0).x;
        g.setColour (ink.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (left, area.getY(), right - left, area.getHeight()));
        if (level > 1.0f)
        {
            g.setColour (lnf.colour (&P::warn));
            g.fillRect (area.withWidth (shape::lineWidth * 2.0f));
            g.fillRect (area.withLeft (area.getRight() - shape::lineWidth * 2.0f));
        }
    }

    // Axes, and the straight line a clean signal would follow.
    g.setColour (line);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    g.drawVerticalLine (juce::roundToInt (area.getCentreX()), area.getY(), area.getBottom());
    const float dashes[] { shape::dashLength, shape::dashGap };
    g.drawDashedLine ({ toScreen (-1.0, -1.0), toScreen (1.0, 1.0) }, dashes, 2, shape::lineWidth);

    const auto curve = core::dsp::Shaper::Curve::through (pointsOf (params));
    const auto mode = static_cast<int> (std::lround (params[core::dsp::Distortion::mode]));
    auto y = [&] (double x)
    {
        if (type == EffectType::shaper)
            return curve.at (x);
        return core::dsp::Distortion::transfer (mode, params[core::dsp::Distortion::drive],
                                                params[core::dsp::Distortion::bits], x);
    };
    juce::Path path;
    for (int i = 0; i <= curveSteps; ++i)
    {
        const auto x = -1.0 + 2.0 * i / curveSteps;
        const auto point = toScreen (x, std::clamp (y (x), -1.0, 1.0));
        if (i == 0)
            path.startNewSubPath (point);
        else
            path.lineTo (point);
    }
    g.setColour (ink);
    g.strokePath (path, juce::PathStrokeType (shape::iconStroke));

    if (type != EffectType::shaper)
        return;
    for (int k = 0; k < core::shaperPoints; ++k)
    {
        const auto x = -1.0 + core::dsp::Shaper::spacing * k;
        const auto centre = toScreen (x, curve.at (x));
        const auto size = space::unit * (k == dragging || k == hover ? 3.0f : 2.0f);
        g.setColour (ink);
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (centre));
    }
}

void EffectGraph::paintEchoes (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using Delay = core::dsp::PingPongDelay;
    const auto ink = lnf.colour (&P::ink), line = lnf.colour (&P::line), muted = lnf.colour (&P::inkMuted);
    const auto steps = echoBars * 16.0f;
    auto xOf = [&] (float step) { return area.getX() + area.getWidth() * step / steps; };

    // Beats and bars, then the middle line between left and right.
    for (int beat = 0; beat <= static_cast<int> (steps) / 4; ++beat)
    {
        g.setColour (beat % 4 == 0 ? muted : line);
        g.drawVerticalLine (juce::roundToInt (xOf (static_cast<float> (beat) * 4.0f)), area.getY(), area.getBottom());
    }
    g.setColour (line);
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());
    g.setFont (lnf.fonts().font (type::meta));
    g.setColour (muted);
    g.drawText ("L", area.withHeight (16.0f).reduced (space::unit, 0.0f), juce::Justification::centredRight, false);
    g.drawText ("R", area.withTop (area.getBottom() - 16.0f).reduced (space::unit, 0.0f),
                juce::Justification::centredRight, false);

    // The dry hit, then each echo: the feedback scales it, ping-pong sends it to alternate sides.
    const auto time = std::max (0.25f, params[Delay::time]);
    const auto feedback = std::clamp (params[Delay::feedback], 0.0f, 1.1f);
    const auto cross = std::clamp (params[Delay::pingpong], 0.0f, 1.0f);
    const auto wet = core::dsp::mixGains (params[Delay::mix]).second;
    const auto half = area.getHeight() * 0.5f - space::unit;
    const auto barWidth = shape::lineWidth * 3.0f;
    g.setColour (muted);
    g.fillRect (juce::Rectangle<float> (xOf (0.0f), area.getCentreY() - half, barWidth, half * 2.0f));
    auto amplitude = 1.0f;
    for (int n = 1; static_cast<float> (n) * time <= steps && amplitude > 0.03f; ++n)
    {
        const auto x = xOf (static_cast<float> (n) * time);
        const auto shown = std::min (1.0f, amplitude) * wet;
        const auto onLeft = n % 2 == 1;
        const auto leftHeight = half * shown * (onLeft ? 1.0f : 1.0f - cross);
        const auto rightHeight = half * shown * (onLeft ? 1.0f - cross : 1.0f);
        g.setColour (ink);
        g.fillRect (juce::Rectangle<float> (x, area.getCentreY() - leftHeight, barWidth, leftHeight));
        g.fillRect (juce::Rectangle<float> (x, area.getCentreY(), barWidth, rightHeight));
        amplitude *= feedback;
    }
}

void EffectGraph::paintTail (juce::Graphics& g, juce::Rectangle<float> area) const
{
    using Reverb = core::dsp::PlateReverb;
    const auto ink = lnf.colour (&P::ink), line = lnf.colour (&P::line), muted = lnf.colour (&P::inkMuted);
    const auto tail = Reverb::tailSeconds (params[Reverb::decay]);
    const auto predelay = std::clamp (params[Reverb::predelay], 0.0f, 200.0f) * 0.001f;
    const auto highs = tail * (1.0f - 0.8f * std::clamp (params[Reverb::damping], 0.0f, 1.0f));
    const auto span = std::clamp (predelay + tail * 0.6f, 1.0f, 8.0f); // most of the audible tail
    auto xOf = [&] (float seconds) { return area.getX() + area.getWidth() * seconds / span; };

    // One line per second, labelled.
    g.setFont (lnf.fonts().font (type::meta));
    for (int s = 0; static_cast<float> (s) <= span; ++s)
    {
        g.setColour (line);
        g.drawVerticalLine (juce::roundToInt (xOf (static_cast<float> (s))), area.getY(), area.getBottom());
        g.setColour (muted);
        g.drawText (
            juce::String (s) + " s",
            juce::Rectangle<float> (xOf (static_cast<float> (s)) + space::unit, area.getBottom() - 16.0f, 40.0f, 16.0f),
            juce::Justification::centredLeft, false);
    }

    // The tail falls 60 dB in `tail` seconds; drawn as amplitude, as the ear meets it.
    auto envelope = [&] (float rt60)
    {
        juce::Path path;
        path.startNewSubPath (area.getX(), area.getBottom());
        path.lineTo (xOf (predelay), area.getBottom());
        for (int i = 0; i <= curveSteps; ++i)
        {
            const auto t = span * static_cast<float> (i) / curveSteps;
            if (t < predelay)
                continue;
            const auto a = std::pow (10.0f, -3.0f * (t - predelay) / std::max (0.05f, rt60));
            path.lineTo (xOf (t), area.getBottom() - a * area.getHeight());
        }
        return path;
    };
    auto body = envelope (tail);
    auto filled = body;
    filled.lineTo (area.getRight(), area.getBottom());
    filled.closeSubPath();
    g.setColour (ink.withAlpha (0.08f));
    g.fillPath (filled);
    g.setColour (ink);
    g.strokePath (body, juce::PathStrokeType (shape::iconStroke));
    if (highs < tail * 0.98f)
    {
        juce::Path dashed;
        const float dashes[] { shape::dashLength, shape::dashGap };
        juce::PathStrokeType (shape::lineWidth).createDashedStroke (dashed, envelope (highs), dashes, 2);
        g.setColour (muted);
        g.fillPath (dashed);
    }
}

// --- the waveshaper's points ---------------------------------------------------------------------------

void EffectGraph::mouseMove (const juce::MouseEvent& e)
{
    const auto now = pointNear (e.position);
    if (now != hover)
    {
        hover = now;
        repaint();
    }
}

void EffectGraph::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0)
    {
        hover = -1;
        repaint();
    }
}

void EffectGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = pointNear (e.position);
    if (dragging < 0)
        return;
    if (onDragStart)
        onDragStart();
    mouseDrag (e);
}

void EffectGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging >= 0 && onPointMoved)
        onPointMoved (dragging, valueAt (e.position.y));
}

void EffectGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    dragging = -1;
    if (onDragEnd)
        onDragEnd();
    repaint();
}
} // namespace tekno::ui
