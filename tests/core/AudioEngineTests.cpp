// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The engine is tested by rendering offline and measuring the result: we can't listen in CI.

#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include "core/engine/AudioEngine.h"
#include "core/engine/OfflineRender.h"

using namespace tekno::core;

namespace
{
constexpr double sampleRate = 48000.0;

std::vector<float> render (Session& session, int numSamples, int blockSize = 256)
{
    AudioEngine engine (session);
    engine.prepare (sampleRate);
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int pos = 0; pos < numSamples; pos += blockSize)
    {
        float* channel = out.data() + pos;
        engine.render (&channel, 1, std::min (blockSize, numSamples - pos));
    }
    return out;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (auto i = from; i < to; ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return std::sqrt (sum / static_cast<double> (to - from));
}

float peak (const std::vector<float>& x)
{
    float p = 0.0f;
    for (auto v : x)
        p = std::max (p, std::abs (v));
    return p;
}

void onlySteps (Session& s, std::initializer_list<int> on, bool accent = false)
{
    for (int i = 0; i < Pattern::defaultLength; ++i)
        s.pattern (0).setStep (i, Step { false, false, false, 45 });
    for (auto i : on)
        s.pattern (0).setStep (i, Step { true, accent, false, 45 });
}
} // namespace

TEST_CASE ("stopped means silent", "[core][engine]")
{
    Session session;
    const auto out = render (session, 48000);
    CHECK (peak (out) == 0.0f);
}

TEST_CASE ("playing the default pattern makes sound that stays finite and bounded", "[core][engine]")
{
    Session session;
    session.transport.playing = true;
    const auto out = render (session, 96000);

    for (auto v : out)
        REQUIRE (std::isfinite (v));
    CHECK (peak (out) <= 1.0f);
    CHECK (rms (out, 0, out.size()) > 0.02);
}

TEST_CASE ("steps start on the exact sample, whatever the buffer size", "[core][engine]")
{
    // 120 bpm at 48 kHz: a 16th note is exactly 6000 samples, so step 4 starts at sample 24000.
    const auto blockSize = GENERATE (1, 64, 512, 4096);
    Session session;
    session.transport.bpm = 120.0;
    onlySteps (session, { 0, 4 });
    session.transport.playing = true;

    const auto out = render (session, 30000, blockSize);
    CAPTURE (blockSize);

    CHECK (std::abs (out[0]) < 1e-4f);       // the attack begins at sample 0...
    CHECK (rms (out, 0, 480) > 0.005);       // ...and is sounding 10 ms later
    CHECK (rms (out, 23000, 24000) < 1e-4);  // released well before step 4
    CHECK (rms (out, 24000, 24480) > 0.005); // step 4 sounds again
    // the first audible sample after the gap is right at 24000 (attack is a few samples)
    size_t firstLoud = 23000;
    while (firstLoud < out.size() && std::abs (out[firstLoud]) < 1e-3f)
        ++firstLoud;
    CHECK (firstLoud >= 24000);
    CHECK (firstLoud < 24000 + 48);
}

TEST_CASE ("the audio thread publishes the playhead", "[core][engine]")
{
    Session session;
    session.transport.bpm = 120.0;
    session.transport.playing = true;
    render (session, 6000 * 3 + 10); // into step 3
    CHECK (session.transport.currentStep.load() == 3);
}

TEST_CASE ("accented steps hit harder", "[core][engine]")
{
    Session plain, accented;
    for (auto* s : { &plain, &accented })
    {
        s->transport.bpm = 120.0;
        s->transport.playing = true;
    }
    onlySteps (plain, { 0 }, false);
    onlySteps (accented, { 0 }, true);
    const auto a = render (plain, 6000), b = render (accented, 6000);
    CHECK (rms (b, 0, 3000) > rms (a, 0, 3000) * 1.3);
}

TEST_CASE ("a low cutoff sounds darker than a high one", "[core][engine]")
{
    auto brightness = [] (float cutoff)
    {
        Session s;
        s.transport.bpm = 120.0;
        s.synth.envMod = 0.0f;
        s.synth.resonance = 0.0f;
        s.synth.cutoff = cutoff;
        onlySteps (s, { 0 });
        s.transport.playing = true;
        const auto out = render (s, 3000);
        // The second difference grows with frequency squared: a crude but robust treble meter.
        double treble = 0.0, level = 0.0;
        for (size_t i = 2; i < out.size(); ++i)
        {
            treble += std::abs (out[i] - 2.0f * out[i - 1] + out[i - 2]);
            level += std::abs (out[i]);
        }
        return treble / level;
    };
    CHECK (brightness (0.2f) * 10.0 < brightness (0.9f));
}

TEST_CASE ("stopping lets the note die out quickly", "[core][engine]")
{
    Session session;
    session.transport.playing = true;
    AudioEngine engine (session);
    engine.prepare (sampleRate);
    std::vector<float> out (9600);
    float* channel = out.data();
    engine.render (&channel, 1, 4800);
    session.transport.playing = false;
    channel = out.data() + 4800;
    engine.render (&channel, 1, 4800);
    CHECK (rms (out, 9600 - 960, 9600) < 1e-4); // silent 80 ms after stop
    CHECK (session.transport.currentStep.load() == -1);
}

TEST_CASE ("offline render writes a WAV of the right length", "[core][engine]")
{
    Session session;
    session.transport.bpm = 120.0;
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tekno-render-test.wav");
    REQUIRE (renderToWav (session, file, 2).wasOk());
    CHECK_FALSE (session.transport.playing.load()); // transport left as it was

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::FileInputStream (file), true));
    REQUIRE (reader != nullptr);
    CHECK (reader->numChannels == 2);
    CHECK (reader->bitsPerSample == 24);
    CHECK (reader->lengthInSamples == 6000 * 32 + 24000); // 2 bars + 0.5 s tail
    file.deleteFile();
}

TEST_CASE ("an odd pattern length wraps on its own length", "[core][engine]")
{
    Session session;
    session.transport.bpm = 120.0; // 6000 samples per step
    onlySteps (session, { 0 });
    session.pattern (0).setLength (3);
    session.transport.playing = true;
    const auto out = render (session, 6000 * 3 + 2000);
    CHECK (rms (out, 18000, 18480) > 0.005); // step 0 again after 3 steps, not 16
    CHECK (rms (out, 17000, 18000) < 1e-4);
}

TEST_CASE ("swing delays the off-beat 16ths by the exact amount", "[core][engine]")
{
    Session session;
    session.transport.bpm = 120.0;
    session.transport.swing = 66.0f; // first 16th of each pair: 66% of 12000 = 7920 samples
    onlySteps (session, { 0, 1 });
    session.transport.playing = true;
    const auto out = render (session, 9000);
    size_t firstLoud = 7000;
    while (firstLoud < out.size() && std::abs (out[firstLoud]) < 1e-3f)
        ++firstLoud;
    CHECK (firstLoud >= 7920);
    CHECK (firstLoud < 7920 + 48);
}

TEST_CASE ("a tie holds the note without a new attack", "[core][engine]")
{
    auto levelAcrossBoundary = [] (bool tie)
    {
        Session s;
        s.transport.bpm = 120.0;
        onlySteps (s, { 0 });
        s.pattern (0).setStep (1, Step { true, false, false, 45, tie });
        s.transport.playing = true;
        const auto out = render (s, 12000);
        return rms (out, 5000, 6000); // the end of step 0: released without a tie, still sounding with it
    };
    CHECK (levelAcrossBoundary (true) > 0.02);
    CHECK (levelAcrossBoundary (false) < 1e-3);
}

TEST_CASE ("a newly selected pattern starts at the next bar, not mid-pattern", "[core][engine]")
{
    Session session;
    session.transport.bpm = 120.0;
    onlySteps (session, { 0, 1, 2, 3, 4, 5, 6, 7 });
    session.pattern (1).setLength (4);
    session.transport.playing = true;

    AudioEngine engine (session);
    engine.prepare (48000.0);
    std::vector<float> out (6000 * 20);
    float* ch = out.data();
    engine.render (&ch, 1, 6000 * 2); // two steps into pattern 1
    session.selected = 1;             // the user picks pattern 2
    ch = out.data() + 6000 * 2;
    engine.render (&ch, 1, 6000 * 13);
    CHECK (session.playing.load() == 0); // still pattern 1 at step 14
    ch = out.data() + 6000 * 15;
    engine.render (&ch, 1, 6000 * 2);
    CHECK (session.playing.load() == 1);               // switched on the wrap...
    CHECK (session.transport.currentStep.load() == 0); // ...and started from its first step
}
