// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The sub bass: its voice measured on its own, then as a channel (steps, live play, commands, files).

#include <cmath>
#include <complex>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/commands/CommandLine.h"
#include "core/commands/CommandRegistry.h"
#include "core/commands/SessionCommands.h"
#include "core/dsp/SubVoice.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"

using namespace tekno::core;

namespace
{
constexpr double rate = 48000.0;

std::vector<float> run (dsp::SubVoice& voice, double seconds)
{
    std::vector<float> out (static_cast<size_t> (seconds * rate));
    for (auto& v : out)
        v = voice.process();
    return out;
}

dsp::SubVoice voiceWith (float drive, float punch, float decay)
{
    dsp::SubVoice voice;
    dsp::SubVoice::Controls c;
    c.drive = drive;
    c.punch = punch;
    c.decay = decay;
    voice.setControls (c);
    voice.prepare (rate);
    return voice;
}

/** Hz, from upward zero crossings (a sine has one per period) between two times. */
double pitchBetween (const std::vector<float>& x, double from, double to)
{
    int crossings = 0;
    for (auto i = static_cast<size_t> (from * rate) + 1; i < static_cast<size_t> (to * rate); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            ++crossings;
    return crossings / (to - from);
}

float peakBetween (const std::vector<float>& x, double from, double to)
{
    float p = 0.0f;
    for (auto i = static_cast<size_t> (from * rate); i < std::min (x.size(), static_cast<size_t> (to * rate)); ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

double harmonic (const std::vector<float>& x, double hz, double from)
{
    std::complex<double> sum {};
    for (auto i = static_cast<size_t> (from * rate); i < x.size(); ++i)
        sum += static_cast<double> (x[i]) * std::polar (1.0, -6.283185307179586 * hz * static_cast<double> (i) / rate);
    return std::abs (sum) / static_cast<double> (x.size() - static_cast<size_t> (from * rate));
}

struct Fixture
{
    Session session;
    CommandRegistry registry;
    Fixture() { registerSessionCommands (registry, session); }

    juce::String line (const juce::String& text)
    {
        const auto parsed = CommandLine::parse (text, registry);
        if (! parsed.command)
            return parsed.error;
        return registry.execute (parsed.command->id, parsed.command->args).error;
    }
};
} // namespace

// --- the voice ----------------------------------------------------------------------------------------

TEST_CASE ("a sub note plays its own pitch once the punch has fallen", "[core][dsp][sub]")
{
    auto voice = voiceWith (0.0f, 0.4f, 1.0f);
    voice.noteOn (33, false); // A1, 55 Hz
    const auto out = run (voice, 1.0);
    CHECK (std::abs (pitchBetween (out, 0.2, 1.0) - 55.0) <= 2.0);
}

TEST_CASE ("punch starts the note higher and lets it fall", "[core][dsp][sub]")
{
    // The first period: the note starts on a zero crossing, so it ends at the first upward one.
    auto firstPeriod = [] (float punch)
    {
        auto voice = voiceWith (0.0f, punch, 1.0f);
        voice.noteOn (33, false);
        const auto out = run (voice, 0.1);
        for (size_t i = 1; i < out.size(); ++i)
            if (out[i - 1] < 0.0f && out[i] >= 0.0f)
                return static_cast<double> (i) / rate;
        return 1.0;
    };
    const auto flat = firstPeriod (0.0f), punched = firstPeriod (1.0f);
    CAPTURE (flat, punched);
    CHECK (std::abs (flat - 1.0 / 55.0) < 0.0005); // A1
    CHECK (punched < flat / 2.0);                  // two octaves up at first, falling in ~30 ms
}

TEST_CASE ("decay: at 0 a sub note is a pluck, at 1 it holds", "[core][dsp][sub]")
{
    auto levelAfterHalfASecond = [] (float decay)
    {
        auto voice = voiceWith (0.0f, 0.0f, decay);
        voice.noteOn (33, false);
        const auto out = run (voice, 0.6);
        return peakBetween (out, 0.5, 0.6) / peakBetween (out, 0.0, 0.1);
    };
    CHECK (levelAfterHalfASecond (0.0f) < 0.01f);
    CHECK (levelAfterHalfASecond (1.0f) > 0.95f);
}

TEST_CASE ("a sub note stops within a few milliseconds of its gate closing, without a click", "[core][dsp][sub]")
{
    auto voice = voiceWith (0.3f, 0.4f, 1.0f);
    voice.noteOn (33, false);
    const auto start = run (voice, 0.3);
    CHECK (std::abs (start.front()) < 0.01f); // from silence it starts on a zero crossing
    voice.gateOff();
    const auto tail = run (voice, 0.2);
    CHECK (peakBetween (tail, 0.1, 0.2) < 0.001f);
    float biggestJump = 0.0f; // the release is a fade, not a step
    for (size_t i = 1; i < tail.size(); ++i)
        biggestJump = std::max (biggestJump, std::abs (tail[i] - tail[i - 1]));
    CHECK (biggestJump < 0.05f);
}

TEST_CASE ("drive adds odd harmonics and keeps the peak", "[core][dsp][sub]")
{
    auto measure = [] (float drive)
    {
        auto voice = voiceWith (drive, 0.0f, 1.0f);
        voice.noteOn (33, false);
        const auto out = run (voice, 1.0);
        return std::pair { harmonic (out, 165.0, 0.2) / harmonic (out, 55.0, 0.2), peakBetween (out, 0.2, 1.0) };
    };
    const auto [cleanThird, cleanPeak] = measure (0.0f);
    const auto [drivenThird, drivenPeak] = measure (1.0f);
    CAPTURE (cleanThird, drivenThird, cleanPeak, drivenPeak);
    CHECK (cleanThird < 0.01);   // a pure sine
    CHECK (drivenThird > 0.2);   // squarer: what a laptop speaker can play
    CHECK (drivenPeak <= 0.71f); // saturation doesn't make it peak higher
    CHECK (drivenPeak >= cleanPeak * 0.95f);
}

TEST_CASE ("a sub slide glides to the new note", "[core][dsp][sub]")
{
    auto voice = voiceWith (0.0f, 0.0f, 1.0f);
    voice.noteOn (33, false);
    run (voice, 0.2);
    voice.glideTo (45); // an octave up, 110 Hz
    CHECK (std::abs (pitchBetween (run (voice, 0.6), 0.2, 0.6) - 110.0) <= 3.0);
}

// --- as a channel ----------------------------------------------------------------------------------------

TEST_CASE ("a sub channel is added, plays its steps, and starts new steps on A1", "[core][sub]")
{
    Fixture f;
    f.session.loadDefaultProject();
    while (f.session.channelCount() > 0)
        f.session.removeChannel (0);
    REQUIRE (f.line ("channel.add sub").isEmpty());
    REQUIRE (f.session.channel (0).kind.load() == InstrumentKind::sub);
    CHECK (f.session.project.channelNames[0] == "sub");
    REQUIRE (f.line ("toggle 1").isEmpty());
    CHECK (f.session.pattern (0).getStep (0, 0).note == 33);

    REQUIRE (f.line ("sub.decay 1").isEmpty());
    f.session.transport.playing = true;
    AudioEngine engine (f.session);
    engine.prepare (rate);
    std::vector<float> out (static_cast<size_t> (rate * 0.1));
    float* channel = out.data();
    engine.render (&channel, 1, static_cast<int> (out.size()));
    CHECK (peakBetween (out, 0.0, 0.1) > 0.1f);
}

TEST_CASE ("sub commands set its knobs, and acid commands refuse a sub channel", "[core][sub][commands]")
{
    Fixture f;
    REQUIRE (f.line ("channel.add bass name=low").isEmpty());
    const auto index = f.session.selectedChannel.load();
    REQUIRE (f.line ("sub.drive 0.8").isEmpty());
    REQUIRE (f.line ("sub.punch 0.1").isEmpty());
    REQUIRE (f.line ("sub.glide 0.2").isEmpty());
    const auto s = f.session.channel (index).sub.load();
    CHECK (s.drive == 0.8f);
    CHECK (s.punch == 0.1f);
    CHECK (s.glide == 0.2f);
    CHECK (f.line ("synth.cutoff 0.5").contains ("is a sub bass: this command is for acid channels"));
}

TEST_CASE ("a sub channel survives the project file", "[core][sub][io]")
{
    Fixture f;
    REQUIRE (f.line ("channel.add sub name=rolling").isEmpty());
    REQUIRE (f.line ("sub.drive 0.65").isEmpty());
    REQUIRE (f.line ("sub.decay 0.2").isEmpty());
    const auto index = f.session.selectedChannel.load();
    const auto text = io::project::toText (f.session);
    CHECK (text.contains ("\"instrument\": \"sub\""));

    Session reopened;
    REQUIRE (io::project::fromText (text, reopened).wasOk());
    REQUIRE (reopened.channel (index).kind.load() == InstrumentKind::sub);
    CHECK (reopened.project.channelNames[static_cast<size_t> (index)] == "rolling");
    CHECK (reopened.channel (index).sub.load() == f.session.channel (index).sub.load());

    Session refused;
    CHECK (io::project::fromText (text.replace ("\"instrument\": \"sub\"", "\"instrument\": \"organ\""), refused)
               .getErrorMessage()
               .contains ("must be \"acid\", \"sub\" or \"sampler\""));
}

TEST_CASE ("live play on a sub channel is mono legato, like the acid", "[core][engine][live][sub]")
{
    Fixture f;
    f.session.loadDefaultProject();
    while (f.session.channelCount() > 0)
        f.session.removeChannel (0);
    REQUIRE (f.line ("channel.add sub").isEmpty());
    REQUIRE (f.line ("sub.drive 0").isEmpty());
    REQUIRE (f.line ("sub.punch 0").isEmpty());
    REQUIRE (f.line ("sub.decay 1").isEmpty());

    AudioEngine engine (f.session);
    engine.prepare (rate);
    auto key = [&engine] (LiveInput::Event::Type type, int pitch)
    {
        LiveInput::Event e;
        e.type = type;
        e.pitch = static_cast<std::uint8_t> (pitch);
        e.timeMs = juce::Time::getMillisecondCounterHiRes();
        REQUIRE (engine.liveInput().push (LiveInput::Source::ui, e));
    };
    auto play = [&engine] (double seconds)
    {
        std::vector<float> out (static_cast<size_t> (seconds * rate));
        for (size_t pos = 0; pos < out.size(); pos += 256)
        {
            float* channel = out.data() + pos;
            engine.render (&channel, 1, static_cast<int> (std::min<size_t> (256, out.size() - pos)));
        }
        return out;
    };

    key (LiveInput::Event::Type::noteOn, 33);
    CHECK (std::abs (pitchBetween (play (0.4), 0.2, 0.4) - 55.0) <= 6.0);
    key (LiveInput::Event::Type::noteOn, 45); // held together: slides up
    CHECK (std::abs (pitchBetween (play (0.4), 0.2, 0.4) - 110.0) <= 6.0);
    key (LiveInput::Event::Type::noteOff, 45); // back to the key still down
    CHECK (std::abs (pitchBetween (play (0.4), 0.2, 0.4) - 55.0) <= 6.0);
    key (LiveInput::Event::Type::noteOff, 33);
    CHECK (peakBetween (play (0.3), 0.1, 0.3) < 0.001f);
}

TEST_CASE ("channel.add can give a drum channel its sound in the same undo step", "[core][commands]")
{
    Fixture f;
    const auto before = f.session.channelCount();
    REQUIRE (f.line ("channel.add sampler name=perc sample=builtin:clap").isEmpty());
    const auto index = f.session.selectedChannel.load();
    CHECK (f.session.channelCount() == before + 1);
    CHECK (f.session.channel (index).sample.load() != nullptr);
    CHECK (f.session.project.channelSamples[static_cast<size_t> (index)] == "builtin:clap");
    REQUIRE (f.registry.undo()); // one step takes the channel and its sound away
    CHECK (f.session.channelCount() == before);

    CHECK (f.line ("channel.add sub sample=builtin:kick").contains ("only a sampler takes a sample"));
    CHECK (f.session.channelCount() == before);
    CHECK_FALSE (f.line ("channel.add sampler sample=builtin:nothing").isEmpty()); // unknown: nothing added
    CHECK (f.session.channelCount() == before);
}
