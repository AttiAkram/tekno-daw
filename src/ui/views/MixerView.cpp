// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "ui/views/MixerView.h"

#include <cmath>

namespace tekno::ui
{
namespace
{
    using P = tokens::Palette;
    namespace type = tokens::type;
    namespace shape = tokens::shape;
    namespace space = tokens::space;
    using core::MixerSettings;

    constexpr float nameHeight = 24.0f;
    constexpr float toggleSize = 20.0f;
    constexpr float panSize = 36.0f;
    constexpr float readoutHeight = 22.0f;
    constexpr float clipSize = 8.0f;

    juce::String formatDb (float db)
    {
        if (db <= MixerSettings::minVolumeDb)
            return "-inf";
        return (db > 0.0f ? "+" : "") + juce::String (db, 1);
    }

    juce::String formatPan (float pan)
    {
        const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);
        if (amount == 0)
            return "C";
        return (pan < 0.0f ? "L" : "R") + juce::String (amount);
    }

    /** One strip's areas, in the coordinates of whatever paints it. */
    struct StripLayout
    {
        juce::Rectangle<float> name, mute, solo, pan, panText, effects, fader, meter, clip, readout;

        static StripLayout make (juce::Rectangle<float> strip)
        {
            StripLayout l;
            auto a = strip.reduced (space::unit * 2.0f, 0.0f);
            l.name = a.removeFromTop (nameHeight);
            a.removeFromTop (space::unit * 2.0f);
            const auto toggles = a.removeFromTop (toggleSize);
            l.mute = toggles.withWidth (toggleSize).withX (toggles.getCentreX() - toggleSize - space::unit * 0.5f);
            l.solo = l.mute.translated (toggleSize + space::unit, 0.0f);
            a.removeFromTop (space::unit * 3.0f);
            l.pan = a.removeFromTop (panSize).withSizeKeepingCentre (panSize, panSize);
            l.panText = a.removeFromTop (18.0f);
            l.effects = a.removeFromTop (18.0f); // the inserts' short names
            a.removeFromTop (space::unit * 3.0f);
            l.readout = a.removeFromBottom (readoutHeight);
            a.removeFromBottom (space::unit);
            l.clip = a.removeFromTop (clipSize + space::unit);
            const auto columnWidth = shape::faderWidth + space::unit * 2.0f + shape::meterWidth;
            const auto column = a.withSizeKeepingCentre (columnWidth, a.getHeight());
            l.fader = column.withWidth (shape::faderWidth);
            l.meter = column.withLeft (column.getRight() - shape::meterWidth);
            l.clip = l.clip.withX (l.meter.getX())
                         .withWidth (shape::meterWidth)
                         .withSizeKeepingCentre (shape::meterWidth, clipSize);
            return l;
        }
    };

    /** The fader's travel inside its bounds, matching JUCE's linear slider (thumb radius 6). */
    juce::Rectangle<float> travel (juce::Rectangle<float> fader)
    {
        return fader.reduced (0.0f, 6.0f);
    }

    /** "dist delay": the effects on a strip, short, in order; bypassed ones in brackets. */
    juce::String effectNames (const core::EffectChain& chain)
    {
        juce::StringArray names;
        for (const auto& slot : chain.slots)
        {
            const auto type = slot.type.load();
            if (type == core::EffectType::none)
                continue;
            const auto name = type == core::EffectType::distortion ? juce::String ("dist")
                              : type == core::EffectType::reverb   ? juce::String ("verb")
                                                                   : juce::String ("delay");
            names.add (slot.bypass.load() ? "(" + name + ")" : name);
        }
        return names.joinIntoString (" ");
    }

    void paintMeter (juce::Graphics& g, const TeknoLookAndFeel& lnf, juce::Rectangle<float> area,
                     const LevelMeters::Level& level, juce::Rectangle<float> clipBox)
    {
        const auto track = travel (area);
        g.setColour (lnf.colour (&P::line));
        g.fillRect (track);

        const auto height = track.getHeight() * MixerView::faderProportion (level.db);
        if (level.db > LevelMeters::minDb)
        {
            g.setColour (lnf.colour (&P::ink));
            g.fillRect (track.withTop (track.getBottom() - height));
        }
        if (level.holdDb > LevelMeters::minDb)
        {
            const auto y = track.getBottom() - track.getHeight() * MixerView::faderProportion (level.holdDb);
            g.setColour (lnf.colour (&P::ink));
            g.fillRect (track.getX(), y - 1.0f, track.getWidth(), 2.0f);
        }

        if (level.clipped) // only when it happened: an empty box would read as a zero
        {
            g.setColour (lnf.colour (&P::warn));
            g.fillRoundedRectangle (clipBox, 2.0f);
        }
    }

    void drawText (juce::Graphics& g, const TeknoLookAndFeel& lnf, const juce::String& text,
                   const tokens::TextStyle& style, juce::Colour colour, juce::Rectangle<float> area,
                   juce::Justification justification = juce::Justification::centred)
    {
        g.setFont (lnf.fonts().font (style));
        g.setColour (colour);
        g.drawText (Fonts::format (style, text), area, justification, true);
    }
} // namespace

float MixerView::faderProportion (float db) noexcept
{
    constexpr auto min = MixerSettings::minVolumeDb, max = MixerSettings::maxVolumeDb;
    const auto skew = std::log (0.5f) / std::log ((midPointDb - min) / (max - min));
    const auto linear = std::clamp ((db - min) / (max - min), 0.0f, 1.0f);
    return std::pow (linear, skew);
}

// --- the channel strips ---------------------------------------------------------------------------

class MixerView::Strips final : public juce::Component
{
public:
    explicit Strips (MixerView& ownerView)
        : owner (ownerView)
    {
        for (size_t c = 0; c < faders.size(); ++c)
        {
            const auto channel = static_cast<int> (c);
            auto& fader = faders[c];
            owner.setUpFader (fader);
            fader.onValueChange = [this, channel, &fader]
            { owner.run ("mixer.volume", { { "db", fader.getValue() }, { "channel", channel + 1 } }); };
            addChildComponent (fader);

            auto& pan = pans[c];
            pan.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            pan.setRange (-1.0, 1.0);
            pan.setMouseDragSensitivity (200);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setMouseClickGrabsKeyboardFocus (false);
            pan.onValueChange = [this, channel, &pan]
            { owner.run ("mixer.pan", { { "pan", pan.getValue() }, { "channel", channel + 1 } }); };
            pan.onDragStart = [this] { owner.commands.beginGesture(); };
            pan.onDragEnd = [this] { owner.commands.endGesture(); };
            addChildComponent (pan);
        }
    }

    int count() const noexcept { return shownCount; }

    void sync()
    {
        const auto& session = owner.session;
        const auto channels = session.channelCount();
        if (channels != shownCount)
        {
            shownCount = channels;
            for (size_t c = 0; c < faders.size(); ++c)
            {
                faders[c].setVisible (static_cast<int> (c) < channels);
                pans[c].setVisible (static_cast<int> (c) < channels);
            }
            owner.resized(); // the width depends on the count
        }
        for (int c = 0; c < channels; ++c)
        {
            const auto& mixer = session.channel (c).mixer;
            auto& fader = faders[static_cast<size_t> (c)];
            auto& pan = pans[static_cast<size_t> (c)];
            if (! fader.isMouseButtonDown())
                fader.setValue (mixer.volumeDb.load(), juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (mixer.pan.load(), juce::dontSendNotification);
        }
    }

    void resized() override
    {
        for (int c = 0; c < shownCount; ++c)
        {
            const auto l = layout (c);
            faders[static_cast<size_t> (c)].setBounds (l.fader.toNearestInt());
            pans[static_cast<size_t> (c)].setBounds (l.pan.toNearestInt());
        }
    }

    StripLayout layout (int channel) const
    {
        return StripLayout::make (juce::Rectangle<float> (stripWidth * static_cast<float> (channel), 0.0f, stripWidth,
                                                          static_cast<float> (getHeight())));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& lnf = owner.lnf;
        const auto& session = owner.session;
        const auto ink = lnf.colour (&P::ink), bg = lnf.colour (&P::bg), line = lnf.colour (&P::line),
                   muted = lnf.colour (&P::inkMuted);
        const auto selected = session.selectedChannel.load();

        for (int c = 0; c < shownCount; ++c)
        {
            const auto l = layout (c);
            const auto& mixer = session.channel (c).mixer;
            const auto isSelected = c == selected;

            if (c > 0) // thin divider between strips
            {
                g.setColour (line);
                g.fillRect (stripWidth * static_cast<float> (c), 0.0f, shape::lineWidth,
                            static_cast<float> (getHeight()));
            }

            const auto pill = l.name.reduced (shape::lineWidth * 0.5f);
            if (isSelected)
            {
                g.setColour (ink);
                g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            }
            g.setColour (isSelected ? ink : line);
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, shape::lineWidth);
            drawText (g, lnf, session.project.channelNames[static_cast<size_t> (c)], type::meta, isSelected ? bg : ink,
                      pill.reduced (space::unit, 0.0f));

            auto toggle = [&] (juce::Rectangle<float> area, bool on, const char* letter)
            {
                const auto box = area.reduced (shape::lineWidth * 0.5f);
                if (on)
                {
                    g.setColour (ink);
                    g.fillRoundedRectangle (box, shape::stepRadius);
                }
                g.setColour (on ? ink : line);
                g.drawRoundedRectangle (box, shape::stepRadius, shape::lineWidth);
                drawText (g, lnf, letter, type::meta, on ? bg : muted, box);
            };
            toggle (l.mute, mixer.mute.load(), "M");
            toggle (l.solo, mixer.solo.load(), "S");

            drawText (g, lnf, formatPan (mixer.pan.load()), type::meta, muted, l.panText);
            drawText (g, lnf, effectNames (session.channel (c).effects), type::meta, ink, l.effects);
            paintMeter (g, lnf, l.meter, owner.meters.channel (c), l.clip);
            drawText (g, lnf, formatDb (mixer.volumeDb.load()), type::meta, ink, l.readout);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int c = 0; c < shownCount; ++c)
        {
            const auto l = layout (c);
            const auto& mixer = owner.session.channel (c).mixer;
            const core::CommandArgs target { { "channel", c + 1 } };
            if (l.name.contains (e.position))
                owner.run ("channel.select", target);
            else if (l.mute.contains (e.position))
                owner.run ("mixer.mute", { { "on", ! mixer.mute.load() }, { "channel", c + 1 } });
            else if (l.solo.contains (e.position))
                owner.run ("mixer.solo", { { "on", ! mixer.solo.load() }, { "channel", c + 1 } });
            else if (l.meter.contains (e.position) || l.clip.expanded (4.0f).contains (e.position))
                owner.meters.resetClip (c);
            else
                continue;
            repaint();
            return;
        }
    }

private:
    MixerView& owner;
    std::array<juce::Slider, core::Session::maxChannels> faders, pans;
    int shownCount = -1;
};

// --- the view -------------------------------------------------------------------------------------

MixerView::MixerView (TeknoLookAndFeel& teknoLookAndFeel, core::Session& sessionToShow, core::CommandRegistry& registry,
                      LevelMeters& levelMeters)
    : lnf (teknoLookAndFeel)
    , session (sessionToShow)
    , commands (registry)
    , meters (levelMeters)
{
    strips = std::make_unique<Strips> (*this);
    viewport.setViewedComponent (strips.get(), false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (static_cast<int> (space::unit * 2.0f));
    addAndMakeVisible (viewport);

    setUpFader (masterFader);
    masterFader.onValueChange = [this] { run ("mixer.master", { { "db", masterFader.getValue() } }); };
    addAndMakeVisible (masterFader);

    strips->sync();
    startTimerHz (30);
}

MixerView::~MixerView()
{
    viewport.setViewedComponent (nullptr, false);
}

void MixerView::setUpFader (juce::Slider& fader)
{
    fader.setSliderStyle (juce::Slider::LinearVertical);
    fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    fader.setRange (MixerSettings::minVolumeDb, MixerSettings::maxVolumeDb);
    fader.setSkewFactorFromMidPoint (midPointDb);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.setMouseClickGrabsKeyboardFocus (false);
    fader.onDragStart = [this] { commands.beginGesture(); }; // a whole drag = one undo step
    fader.onDragEnd = [this] { commands.endGesture(); };
}

void MixerView::run (const juce::String& command, const core::CommandArgs& args)
{
    const auto outcome = commands.execute (command, args);
    if (! outcome.succeeded())
        juce::Logger::writeToLog ("command " + command + " failed: " + outcome.error);
}

void MixerView::resized()
{
    auto area = getLocalBounds().toFloat();
    masterArea = area.removeFromRight (stripWidth * 1.5f);
    area.removeFromRight (space::gutter);
    viewport.setBounds (area.toNearestInt());
    const auto contentWidth = stripWidth * static_cast<float> (std::max (0, strips->count()));
    const auto needsScroll = contentWidth > static_cast<float> (viewport.getWidth());
    strips->setSize (static_cast<int> (contentWidth),
                     viewport.getHeight() - (needsScroll ? viewport.getScrollBarThickness() : 0));
    strips->resized();

    // Master: the same column as a strip, a stereo meter, no pan.
    const auto l = StripLayout::make (masterArea);
    masterFader.setBounds (l.fader.toNearestInt());
    masterMeterArea = l.meter.withLeft (l.meter.getX() - shape::meterWidth - space::unit);
    masterReadoutArea = l.readout;
    masterClipArea = l.clip.withX (masterMeterArea.getX()).withWidth (masterMeterArea.getWidth());
}

void MixerView::paint (juce::Graphics& g)
{
    g.setColour (lnf.colour (&P::line));
    g.fillRect (masterArea.withWidth (shape::lineWidth).translated (-space::gutter * 0.5f, 0.0f));

    const auto l = StripLayout::make (masterArea);
    drawText (g, lnf, "master", type::sectionTitle, lnf.colour (&P::ink), l.name);

    const auto half = (masterMeterArea.getWidth() - space::unit) * 0.5f;
    const auto clipHalf = masterClipArea.withWidth (half);
    paintMeter (g, lnf, masterMeterArea.withWidth (half), meters.masterLeft(), clipHalf);
    paintMeter (g, lnf, masterMeterArea.withTrimmedLeft (half + space::unit), meters.masterRight(),
                clipHalf.translated (half + space::unit, 0.0f));
    drawText (g, lnf, formatDb (session.master.volumeDb.load()), type::meta, lnf.colour (&P::ink), masterReadoutArea);
    drawText (g, lnf, effectNames (session.master.effects), type::meta, lnf.colour (&P::ink), l.effects);
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    if (masterMeterArea.contains (e.position) || masterClipArea.expanded (4.0f).contains (e.position))
    {
        meters.resetClip (-1);
        repaint();
    }
}

void MixerView::metersChanged()
{
    strips->repaint();
    repaint (masterArea.toNearestInt());
}

void MixerView::timerCallback()
{
    if (! isShowing())
        return;
    strips->sync();
    if (! masterFader.isMouseButtonDown())
        masterFader.setValue (session.master.volumeDb.load(), juce::dontSendNotification);
    strips->repaint(); // names, mute/solo, readouts: cheap, and the view is only on while visible
    repaint (masterReadoutArea.toNearestInt());
}
} // namespace tekno::ui
