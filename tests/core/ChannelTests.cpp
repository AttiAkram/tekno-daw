// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// M3-A: channels, the drum sampler, samples, the mixer. Measured offline.

#include <cmath>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include "TestSessions.h"
#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"
#include "core/dsp/BuiltinKit.h"
#include "core/dsp/Sampler.h"
#include "core/engine/AudioEngine.h"

using namespace tekno::core;

namespace
{
constexpr double sampleRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

Stereo renderStereo (Session& session, int numSamples, int blockSize = 256)
{
    AudioEngine engine (session);
    engine.prepare (sampleRate);
    Stereo out { std::vector<float> (static_cast<size_t> (numSamples)),
                 std::vector<float> (static_cast<size_t> (numSamples)) };
    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        float* channels[] { out.left.data() + pos, out.right.data() + pos };
        engine.render (channels, 2, std::min (blockSize, numSamples - pos));
    }
    return out;
}

float peak (const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0)
        to = x.size();
    float p = 0.0f;
    for (auto i = from; i < to; ++i)
        p = std::max (p, std::abs (x[i]));
    return p;
}

/** First sample louder than `threshold`, or -1. */
int onset (const std::vector<float>& x, float threshold = 1.0e-4f)
{
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs (x[i]) > threshold)
            return static_cast<int> (i);
    return -1;
}

/** Only the kick channel, with one hit on step 1 of pattern 1. */
void kickOnly (Session& s, Step hit = { true, false, false, Step::samplerRootNote })
{
    s.loadDefaultProject();
    while (s.channelCount() > 1)
        s.removeChannel (1);
    for (int i = 0; i < Pattern::maxSteps; ++i)
        s.pattern (0).setStep (0, i, {});
    s.pattern (0).setStep (0, 0, hit);
    s.channel (0).mixer.volumeDb = 0.0f;
    s.transport.bpm = 120.0;
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

// --- built-in kit and sampler -------------------------------------------------------------------

TEST_CASE ("the built-in kit is made by code, the same every time", "[core][sampler]")
{
    for (const auto& name : SampleLibrary::builtinNames())
    {
        CAPTURE (name);
        const auto a = dsp::builtinKit::make (name), b = dsp::builtinKit::make (name);
        REQUIRE (a.getNumSamples() > 1000);
        CHECK (a.getMagnitude (0, 0, a.getNumSamples()) == Catch::Approx (0.9f).margin (1.0e-4f));
        for (int i = 0; i < a.getNumSamples(); ++i)
            REQUIRE (a.getSample (0, i) == b.getSample (0, i));
        CHECK (std::abs (a.getSample (0, a.getNumSamples() - 1)) < 0.01f); // ends near silence: no click
    }
    CHECK (dsp::builtinKit::make ("cowbell").getNumSamples() == 0);
}

TEST_CASE ("the sampler plays C4 at the sample's pitch and an octave up twice as fast", "[core][sampler]")
{
    Sample sample;
    sample.audio.setSize (1, 4800);
    sample.audio.clear();
    for (int i = 0; i < 4800; ++i)
        sample.audio.setSample (0, i, 0.5f);

    auto lengthAt = [&] (int note, float tune)
    {
        dsp::Sampler sampler;
        sampler.prepare (sampleRate);
        sampler.setSample (&sample);
        sampler.setSettings ({ tune, 1.0f, 0.5f });
        sampler.trigger (note, false);
        int length = 0;
        for (; length < 20000; ++length)
        {
            float l = 0.0f, r = 0.0f;
            sampler.process (l, r);
            if (sampler.activeVoices() == 0)
                break;
        }
        return length;
    };

    CHECK (std::abs (lengthAt (60, 0.0f) - 4800) <= 2);
    CHECK (std::abs (lengthAt (72, 0.0f) - 2400) <= 2);
    CHECK (std::abs (lengthAt (60, 12.0f) - 2400) <= 2); // tune does the same as the note
    CHECK (std::abs (lengthAt (48, 0.0f) - 9600) <= 2);
}

TEST_CASE ("sampler decay shortens the tail, accent hits harder", "[core][sampler]")
{
    Sample sample;
    sample.audio = dsp::builtinKit::make ("openhat");

    auto energyAfter = [&] (float decay, int from)
    {
        dsp::Sampler sampler;
        sampler.prepare (sampleRate);
        sampler.setSample (&sample);
        sampler.setSettings ({ 0.0f, decay, 0.5f });
        sampler.trigger (60, false);
        double sum = 0.0;
        for (int i = 0; i < 20000; ++i)
        {
            float l = 0.0f, r = 0.0f;
            sampler.process (l, r);
            if (i >= from)
                sum += static_cast<double> (l) * l;
        }
        return sum;
    };
    CHECK (energyAfter (0.2f, 4800) < energyAfter (1.0f, 4800) * 0.05);

    auto firstPeak = [&] (bool accented)
    {
        dsp::Sampler sampler;
        sampler.prepare (sampleRate);
        sampler.setSample (&sample);
        sampler.setSettings ({ 0.0f, 1.0f, 0.5f });
        sampler.trigger (60, accented);
        float p = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            float l = 0.0f, r = 0.0f;
            sampler.process (l, r);
            p = std::max (p, std::abs (l));
        }
        return p;
    };
    CHECK (firstPeak (true) == Catch::Approx (firstPeak (false) * 1.5f).epsilon (0.01));
}

TEST_CASE ("the sampler never plays more than 8 voices, and a new sample stops the old one", "[core][sampler]")
{
    Sample sample;
    sample.audio = dsp::builtinKit::make ("kick");
    dsp::Sampler sampler;
    sampler.prepare (sampleRate);
    sampler.setSample (&sample);
    for (int hit = 0; hit < 20; ++hit)
    {
        sampler.trigger (60, false);
        for (int i = 0; i < 48; ++i) // 1 ms apart
        {
            float l = 0.0f, r = 0.0f;
            sampler.process (l, r);
            REQUIRE (std::isfinite (l));
        }
    }
    // 8 sounding plus at most the 2 fading out after being stolen.
    CHECK (sampler.activeVoices() <= dsp::Sampler::maxVoices + 2);
    for (int i = 0; i < 200; ++i) // 4 ms: the stolen ones are gone
    {
        float l = 0.0f, r = 0.0f;
        sampler.process (l, r);
    }
    CHECK (sampler.activeVoices() == dsp::Sampler::maxVoices);

    Sample other;
    other.audio = dsp::builtinKit::make ("clap");
    sampler.setSample (&other);
    CHECK (sampler.activeVoices() == 0);
}

// --- engine with channels -------------------------------------------------------------------------

TEST_CASE ("a drum hit lands on the exact sample of its step", "[core][engine][sampler]")
{
    Session session;
    kickOnly (session);
    session.pattern (0).setStep (0, 0, {});
    session.pattern (0).setStep (0, 4, { true, false, false, Step::samplerRootNote }); // beat 2
    session.transport.playing = true;
    const auto out = renderStereo (session, 48000, 333);
    const auto expected = static_cast<int> (std::ceil (AudioEngine::samplesPerStep (sampleRate, 120.0) * 4.0));
    // The attack ramps over half a millisecond, so the first audible sample is right after the step.
    CHECK (std::abs (onset (out.left) - expected) <= 2);
}

TEST_CASE ("the default loop plays every channel and stays below full scale", "[core][engine][mixer]")
{
    Session session; // kit + acid, as a new project opens
    session.transport.playing = true;
    const auto out = renderStereo (session, 48000 * 4);
    CHECK (peak (out.left) > 0.3f);
    CHECK (peak (out.left) < 1.0f);
    CHECK (peak (out.right) < 1.0f);
    for (int c = 0; c < session.channelCount(); ++c)
    {
        CAPTURE (c);
        if (c == 2)
            continue;                                   // the open hat has no steps in the default loop
        CHECK (session.channel (c).peak.load() > 0.0f); // published for the meters
    }
    CHECK (session.master.peakLeft.load() == Catch::Approx (peak (out.left)));
}

TEST_CASE ("mute, solo and pan shape the mix", "[core][mixer]")
{
    auto renderWith = [] (const std::function<void (Session&)>& setup)
    {
        Session s;
        s.transport.playing = true;
        setup (s);
        return renderStereo (s, 48000);
    };

    const auto everything = renderWith ([] (Session&) {});
    const auto kickMuted = renderWith ([] (Session& s) { s.channel (0).mixer.mute = true; });
    const auto acidSolo = renderWith ([] (Session& s) { s.channel (4).mixer.solo = true; });

    Session acidAlone;
    tekno::test::acidOnly (acidAlone);
    acidAlone.channel (0).mixer.volumeDb = -9.0f;
    acidAlone.transport.playing = true;
    const auto reference = renderStereo (acidAlone, 48000);
    CHECK (acidSolo.left == reference.left); // solo = only that channel, bit for bit
    CHECK (kickMuted.left != everything.left);

    const auto hardLeft = renderWith (
        [] (Session& s)
        {
            s.channel (4).mixer.solo = true;
            s.channel (4).mixer.pan = -1.0f;
        });
    CHECK (peak (hardLeft.right) < 1.0e-6f);
    CHECK (peak (hardLeft.left) > peak (reference.left) * 1.3f); // +3 dB on the side it goes to

    const auto [l, r] = AudioEngine::panGains (0.0f);
    CHECK (l == Catch::Approx (1.0f));
    CHECK (r == Catch::Approx (1.0f));
    CHECK (AudioEngine::volumeGain (MixerSettings::minVolumeDb) == 0.0f);
    CHECK (AudioEngine::volumeGain (-6.0f) == Catch::Approx (0.501f).epsilon (0.001));
}

TEST_CASE ("moving a fader never jumps: gain ramps across the block", "[core][mixer]")
{
    Session session;
    tekno::test::acidOnly (session);
    for (int i = 0; i < Pattern::defaultLength; ++i) // one held note: the level only depends on the fader
        session.pattern (0).setStep (0, i, Step { true, false, true, 45 });
    session.channel (0).acid.envMod = 0.0f;
    session.transport.playing = true;
    AudioEngine engine (session);
    engine.prepare (sampleRate);

    std::vector<float> a (512), b (512);
    float* pa = a.data();
    float* pb = b.data();
    for (int i = 0; i < 20; ++i)
        engine.render (&pa, 1, 512);
    session.channel (0).mixer.volumeDb = MixerSettings::minVolumeDb; // slam the fader down
    engine.render (&pb, 1, 512);
    // The block fades to silence instead of stopping dead: it starts loud and ends at zero.
    CHECK (peak (b, 0, 64) > 0.01f);
    CHECK (std::abs (b.back()) < 0.01f);
    CHECK (peak (b, 0, 512) <= peak (a, 0, 512) * 1.5f);
}

TEST_CASE ("removing a channel while playing is safe", "[core][engine][channels]")
{
    Session session;
    session.transport.playing = true;
    AudioEngine engine (session);
    engine.prepare (sampleRate);
    std::vector<float> l (256), r (256);
    float* out[] { l.data(), r.data() };
    for (int block = 0; block < 400; ++block)
    {
        if (block == 100)
            session.removeChannel (0);
        if (block == 200)
            session.addChannel (InstrumentKind::sampler, "new");
        if (block == 300)
            session.removeChannel (session.channelCount() - 1);
        engine.render (out, 2, 256);
        for (auto v : l)
            REQUIRE (std::isfinite (v));
    }
    CHECK (session.channelCount() == 4);
}

// --- samples ----------------------------------------------------------------------------------------

TEST_CASE ("a replaced sample is freed only after the audio thread has moved on", "[core][sampler]")
{
    Session session;
    const auto before = session.samples.size();

    // Load a file-like sample: write a tiny WAV.
    const auto wav = juce::File::createTempFile (".wav");
    {
        juce::AudioBuffer<float> audio (1, 480);
        audio.clear();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wav);
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (
            stream,
            juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, 480);
    }
    REQUIRE (session.setChannelSample (0, wav.getFullPathName()).isEmpty());
    CHECK (session.samples.size() == before + 1);

    session.setChannelSample (0, "builtin:kick"); // the file is no longer used...
    session.collectSampleGarbage();
    CHECK (session.samples.size() == before + 1); // ...but the audio thread may still hold it
    session.audioBlocks += 1;
    session.collectSampleGarbage();
    CHECK (session.samples.size() == before + 1);
    session.audioBlocks += 1;
    session.collectSampleGarbage();
    CHECK (session.samples.size() == before); // two blocks later it is gone
    wav.deleteFile();

    CHECK (session.setChannelSample (0, "builtin:cowbell").contains ("no built-in sound"));
    CHECK (session.setChannelSample (0, "/nope/missing.wav").contains ("not found"));
}

// --- commands ---------------------------------------------------------------------------------------

TEST_CASE ("redo acts on the pattern and channel it was done on, not on the current selection",
           "[core][commands][channels]")
{
    Fixture f;
    const auto acid = f.session.selectedChannel.load();
    REQUIRE (f.registry.execute ("pattern.step.toggle", { { "step", 2 } }).succeeded());
    const auto edited = f.session.pattern (0).getStep (acid, 1);
    f.registry.undo();

    f.registry.execute ("pattern.select", { { "pattern", 2 } });
    f.registry.execute ("channel.select", { { "channel", 1 } });
    REQUIRE (f.registry.redo());
    CHECK (f.session.pattern (0).getStep (acid, 1) == edited);
    CHECK (f.session.pattern (1).lane (0).isBlank()); // the old bug: redo landed here
}

TEST_CASE ("channel commands add, rename, remove, and undo brings everything back", "[core][commands][channels]")
{
    Fixture f;
    const auto originalCount = f.session.channelCount();
    const auto kickLane = f.session.captureChannel (0);

    CHECK (f.line ("channel.add sampler name=snare").isEmpty());
    CHECK (f.session.channelCount() == originalCount + 1);
    CHECK (f.session.selectedChannel.load() == originalCount);
    CHECK (f.session.project.channelNames[static_cast<size_t> (originalCount)] == "snare");
    CHECK (f.line ("sample builtin:clap").isEmpty());
    CHECK (f.line ("toggle 5").isEmpty());
    CHECK (f.session.pattern (0).getStep (originalCount, 4).note == Step::samplerRootNote); // C4 for drums

    CHECK (f.line ("channel.remove channel=1").isEmpty());
    CHECK (f.session.channelCount() == originalCount);
    CHECK (f.session.project.channelNames[0] == "hat"); // everything moved up
    f.registry.undo();
    CHECK (f.session.channelCount() == originalCount + 1);
    CHECK (f.session.project.channelNames[0] == "kick");
    const auto restored = f.session.captureChannel (0);
    CHECK (restored.lanes == kickLane.lanes);
    CHECK (restored.sample == "builtin:kick");
    CHECK (f.session.channel (0).sample.load() != nullptr);

    f.registry.undo(); // the toggle
    f.registry.undo(); // the sample
    f.registry.undo(); // the add
    CHECK (f.session.channelCount() == originalCount);

    CHECK (f.line ("channel.add theremin").contains ("unknown instrument"));
}

TEST_CASE ("commands check the channel exists and plays the right instrument", "[core][commands][channels]")
{
    Fixture f;
    CHECK (f.line ("cutoff 0.5 channel=1").contains ("is a sampler"));
    CHECK (f.line ("tune 3 channel=5").contains ("is an acid synth"));
    CHECK (f.line ("toggle 1 channel=9").contains ("there is no channel 9"));
    CHECK (f.line ("tune 3 channel=1").isEmpty());
    CHECK (f.session.channel (0).sampler.tune.load() == 3.0f);
    CHECK (f.line ("cutoff 0.25").isEmpty()); // the selected channel: the acid
    CHECK (f.session.channel (4).acid.cutoff.load() == 0.25f);
}

TEST_CASE ("mixer commands from the terminal", "[core][commands][mixer]")
{
    Fixture f;
    CHECK (f.line ("vol -6 channel=2").isEmpty());
    CHECK (f.session.channel (1).mixer.volumeDb.load() == -6.0f);
    CHECK (f.line ("pan 0.5").isEmpty());
    CHECK (f.session.channel (4).mixer.pan.load() == 0.5f);
    CHECK (f.line ("mute on channel=1").isEmpty());
    CHECK (f.session.channel (0).mixer.mute.load());
    CHECK (f.line ("solo on").isEmpty());
    CHECK (f.session.channel (4).mixer.solo.load());
    CHECK (f.line ("mixer.master -3").isEmpty());
    CHECK (f.session.master.volumeDb.load() == -3.0f);
    CHECK (f.line ("vol 12").contains ("between"));
    f.registry.undo();
    CHECK (f.session.master.volumeDb.load() == 0.0f);
}

TEST_CASE ("optional targets are named, never positional", "[core][commands][terminal]")
{
    Fixture f;
    CHECK (f.line ("toggle 3 2").contains ("too many arguments"));
    CHECK (f.line ("rename acid night pattern=3").isEmpty());
    CHECK (f.session.project.patternNames[2] == "acid night");
    CHECK (CommandLine::help (f.registry, "pattern.step.toggle").contains ("[channel=]"));
}

TEST_CASE ("a sample loaded from the terminal can be undone and redone", "[core][commands][sampler]")
{
    Fixture f;
    CHECK (f.line ("sample builtin:clap channel=1").isEmpty());
    CHECK (f.session.project.channelSamples[0] == "builtin:clap");
    f.registry.undo();
    CHECK (f.session.project.channelSamples[0] == "builtin:kick");
    f.registry.redo();
    CHECK (f.session.project.channelSamples[0] == "builtin:clap");
    CHECK (f.line ("sample builtin:cowbell channel=1").contains ("no built-in sound"));
    CHECK (f.line ("sample /nope/missing.wav channel=1").contains ("not found"));
    CHECK (f.session.project.channelSamples[0] == "builtin:clap"); // a failed load changes nothing
}
