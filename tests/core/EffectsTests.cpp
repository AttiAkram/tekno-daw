// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// M4-A: native effects and the inserts that hold them. Measured offline, like the synth.

#include <cmath>
#include <complex>
#include <set>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestSessions.h"
#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"
#include "core/dsp/AcidVoice.h"
#include "core/dsp/Distortion.h"
#include "core/dsp/PingPongDelay.h"
#include "core/dsp/PlateReverb.h"
#include "core/dsp/Shaper.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"

using namespace tekno::core;

namespace
{
constexpr double rate = 48000.0;

std::array<float, maxEffectParams> defaultsOf (EffectType type)
{
    return EffectSettings::defaults (type).values;
}

/** Energy of the spectrum outside the harmonics of `fundamental` (aliasing), relative to the total, in dB. */
double aliasingDb (const std::vector<float>& x, double fundamental)
{
    const auto n = x.size();
    double total = 0.0, harmonic = 0.0;
    for (size_t bin = 1; bin < n / 2; ++bin)
    {
        std::complex<double> sum {};
        if (bin % 4 != 0 && bin > 600) // a coarse DFT: every 4th bin above the low end is plenty
            continue;
        for (size_t i = 0; i < n; ++i)
            sum += static_cast<double> (x[i]) *
                   std::polar (1.0, -6.283185307179586 * static_cast<double> (bin * i) / static_cast<double> (n));
        const auto power = std::norm (sum);
        const auto hz = static_cast<double> (bin) * rate / static_cast<double> (n);
        const auto nearest = std::round (hz / fundamental) * fundamental;
        total += power;
        if (std::abs (hz - nearest) < rate / static_cast<double> (n) * 1.5)
            harmonic += power;
    }
    return 10.0 * std::log10 ((total - harmonic) / total);
}

struct Commands
{
    Session session;
    CommandRegistry registry;
    Commands() { registerSessionCommands (registry, session); }

    juce::String line (const juce::String& text)
    {
        const auto parsed = CommandLine::parse (text, registry);
        if (! parsed.command)
            return parsed.error;
        return registry.execute (parsed.command->id, parsed.command->args).error;
    }
};
} // namespace

// --- distortion -------------------------------------------------------------------------------------

TEST_CASE ("the shapers and their antiderivatives agree", "[core][effects]")
{
    for (double x = -9.0; x < 9.0; x += 0.0137)
    {
        const double h = 1.0e-5;
        CAPTURE (x);
        CHECK ((dsp::Distortion::hardClipIntegral (x + h) - dsp::Distortion::hardClipIntegral (x - h)) / (2 * h) ==
               Catch::Approx (dsp::Distortion::hardClip (x)).margin (1.0e-4));
        CHECK ((dsp::Distortion::foldbackIntegral (x + h) - dsp::Distortion::foldbackIntegral (x - h)) / (2 * h) ==
               Catch::Approx (dsp::Distortion::foldback (x)).margin (1.0e-4));
        CHECK (std::abs (dsp::Distortion::foldback (x)) <= 1.0);
    }
    CHECK (dsp::Distortion::foldback (1.5) == Catch::Approx (0.5)); // past full scale it folds back down
}

TEST_CASE ("hard clip with ADAA aliases much less than a naive clipper", "[core][effects]")
{
    // A loud 2.5 kHz sine, driven 24 dB into the clipper.
    const auto fundamental = 2500.0;
    std::vector<float> naive (4800), smooth (4800), right (4800);
    for (size_t i = 0; i < naive.size(); ++i)
    {
        const auto s =
            0.8f * static_cast<float> (std::sin (6.283185307179586 * fundamental * static_cast<double> (i) / rate));
        smooth[i] = right[i] = s;
        naive[i] = static_cast<float> (dsp::Distortion::hardClip (s * 15.85));
    }
    dsp::Distortion distortion;
    distortion.prepare (rate);
    auto params = defaultsOf (EffectType::distortion);
    params[dsp::Distortion::drive] = 24.0f;
    params[dsp::Distortion::output] = 0.0f;
    distortion.process (smooth.data(), right.data(), static_cast<int> (smooth.size()), params);
    for (auto& v : smooth) // the DC blocker's slow start doesn't matter here
        v = static_cast<float> (v);

    const auto naiveAliasing = aliasingDb ({ naive.begin() + 2400, naive.end() }, fundamental);
    const auto adaaAliasing = aliasingDb ({ smooth.begin() + 2400, smooth.end() }, fundamental);
    CAPTURE (naiveAliasing, adaaAliasing);
    CHECK (adaaAliasing < naiveAliasing - 6.0);
}

TEST_CASE ("fold stays bounded, crush has the levels it promises, clipping is reported", "[core][effects]")
{
    dsp::Distortion distortion;
    distortion.prepare (rate);
    auto params = defaultsOf (EffectType::distortion);
    params[dsp::Distortion::drive] = 48.0f;
    params[dsp::Distortion::output] = 0.0f;
    params[dsp::Distortion::mode] = dsp::Distortion::fold;
    std::vector<float> l (4800), r (4800);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (std::sin (6.283185307179586 * 55.0 * static_cast<double> (i) / rate));
    const auto meters = distortion.process (l.data(), r.data(), 4800, params);
    for (auto v : l)
        REQUIRE (std::abs (v) <= 2.05f); // ±1, plus what the DC blocker can swing
    CHECK (meters.clip > 100.0f);        // 48 dB into a full-scale sine

    // Crush: a slow ramp through 3 bits (4 levels each side) jumps a handful of times, in steps of a
    // quarter of full scale; through 8 bits it moves in steps too small to see. (The DC blocker after it
    // lets each held level sag slowly, so count the big jumps, not the distinct values.)
    auto bigJumps = [&] (float bits, float holdEvery)
    {
        params[dsp::Distortion::mode] = dsp::Distortion::crush;
        params[dsp::Distortion::drive] = 0.0f;
        params[dsp::Distortion::bits] = bits;
        params[dsp::Distortion::rate] = holdEvery;
        params[dsp::Distortion::tone] = 1.0f;
        distortion.reset();
        std::vector<float> a (64), b (64);
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = b[i] = -0.99f + 1.98f * static_cast<float> (i) / 63.0f;
        distortion.process (a.data(), b.data(), 64, params);
        int jumps = 0;
        for (size_t i = 1; i < a.size(); ++i)
            jumps += std::abs (a[i] - a[i - 1]) > 0.1f ? 1 : 0;
        return jumps;
    };
    const auto coarse = bigJumps (3.0f, 1.0f);
    CHECK (coarse >= 6);
    CHECK (coarse <= 9);
    CHECK (bigJumps (8.0f, 1.0f) == 0);
    CHECK (bigJumps (16.0f, 16.0f) <= 64 / 16 + 1); // held 16 samples at a time: few, bigger steps
}

// --- delay ------------------------------------------------------------------------------------------

TEST_CASE ("the delay echoes on the beat and bounces left and right", "[core][effects]")
{
    dsp::PingPongDelay delay;
    delay.prepare (rate);
    auto params = defaultsOf (EffectType::delay);
    params[dsp::PingPongDelay::time] = 4.0f; // a quarter note
    params[dsp::PingPongDelay::feedback] = 0.5f;
    params[dsp::PingPongDelay::tone] = 1.0f;
    params[dsp::PingPongDelay::lowcut] = 0.0f;
    params[dsp::PingPongDelay::mix] = 1.0f; // wet only
    const auto bpm = 120.0;
    const auto beat = static_cast<int> (rate * 60.0 / bpm);

    std::vector<float> l (beat * 4, 0.0f), r (beat * 4, 0.0f);
    l[0] = r[0] = 1.0f;
    delay.process (l.data(), r.data(), static_cast<int> (l.size()), params, bpm);

    auto loudestNear = [] (const std::vector<float>& x, int at)
    {
        float best = 0.0f;
        for (int i = at - 3; i <= at + 3; ++i)
            best = std::max (best, std::abs (x[static_cast<size_t> (i)]));
        return best;
    };
    CHECK (loudestNear (l, beat) > 0.5f); // first echo: left (the input enters on the left)
    CHECK (loudestNear (r, beat) < 0.01f);
    CHECK (loudestNear (r, 2 * beat) > 0.1f); // second: right
    CHECK (loudestNear (l, 2 * beat) < 0.01f);
    CHECK (loudestNear (l, 3 * beat) < loudestNear (l, beat)); // dying away
}

TEST_CASE ("feedback above 1 howls but stays bounded", "[core][effects]")
{
    dsp::PingPongDelay delay;
    delay.prepare (rate);
    auto params = defaultsOf (EffectType::delay);
    params[dsp::PingPongDelay::feedback] = 1.1f;
    params[dsp::PingPongDelay::mix] = 1.0f;
    std::vector<float> l (512), r (512);
    float peak = 0.0f;
    for (int block = 0; block < 1000; ++block) // more than 10 s
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (block == 0)
            l[0] = r[0] = 1.0f;
        delay.process (l.data(), r.data(), 512, params, 170.0);
        for (size_t i = 0; i < l.size(); ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            peak = std::max (peak, std::abs (l[i]));
        }
    }
    CHECK (peak > 0.3f);  // it really does run away...
    CHECK (peak <= 1.5f); // ...into a limit
}

// --- reverb -----------------------------------------------------------------------------------------

TEST_CASE ("the plate has a stereo tail that a longer decay makes longer", "[core][effects]")
{
    auto tailEnergy = [] (float decay, std::vector<float>* rightOut = nullptr)
    {
        dsp::PlateReverb reverb;
        reverb.prepare (rate);
        auto params = defaultsOf (EffectType::reverb);
        params[dsp::PlateReverb::decay] = decay;
        params[dsp::PlateReverb::mix] = 1.0f;
        params[dsp::PlateReverb::predelay] = 0.0f;
        std::vector<float> l (96000, 0.0f), r (96000, 0.0f);
        l[0] = r[0] = 1.0f;
        reverb.process (l.data(), r.data(), static_cast<int> (l.size()), params);
        double late = 0.0;
        for (size_t i = 48000; i < l.size(); ++i)
        {
            REQUIRE (std::isfinite (l[i]));
            late += static_cast<double> (l[i]) * l[i];
        }
        if (rightOut != nullptr)
            *rightOut = r;
        return std::make_pair (late, l);
    };
    std::vector<float> right;
    const auto [shortTail, left] = tailEnergy (0.2f);
    const auto [longTail, unused] = tailEnergy (0.9f, &right);
    CHECK (longTail > shortTail * 10.0);
    CHECK (left != right); // a wide, decorrelated plate, not a mono one
}

TEST_CASE ("with mix at 0 an effect passes the dry signal untouched", "[core][effects]")
{
    dsp::PlateReverb reverb;
    reverb.prepare (rate);
    auto params = defaultsOf (EffectType::reverb);
    params[dsp::PlateReverb::mix] = 0.0f;
    std::vector<float> l (2048), r (2048);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (std::sin (0.01 * static_cast<double> (i)));
    const auto original = l;
    reverb.process (l.data(), r.data(), 2048, params);
    CHECK (l == original);
}

// --- inserts in the engine, commands, files ---------------------------------------------------------

TEST_CASE ("an insert changes the channel it is on, and a bypassed one changes nothing", "[core][effects][engine]")
{
    auto renderWith = [] (const juce::String& setup)
    {
        Commands f;
        for (const auto& line : juce::StringArray::fromTokens (setup, ";", ""))
            if (line.trim().isNotEmpty())
                REQUIRE (f.line (line.trim()).isEmpty());
        f.session.transport.playing = true;
        AudioEngine engine (f.session);
        engine.prepare (rate);
        std::vector<float> l (48000), r (48000);
        for (int pos = 0; pos < 48000; pos += 300) // not a multiple of 512: chunking must not matter
        {
            float* ch[] { l.data() + pos, r.data() + pos };
            engine.render (ch, 2, std::min (300, 48000 - pos));
        }
        return std::make_pair (l, r);
    };
    const auto plain = renderWith ("");
    const auto bypassed = renderWith ("fx.add distortion channel=1; fx.bypass 1 on channel=1");
    const auto distorted = renderWith ("fx.add distortion channel=1; fx.set 1 drive 36 channel=1");
    const auto echoed = renderWith ("fx.add delay; fx.set 1 mix 1");
    CHECK (bypassed == plain);
    CHECK (distorted.first != plain.first);
    CHECK (echoed.first != echoed.second); // ping-pong: the two sides differ
}

TEST_CASE ("fx commands add, set, move and remove, each one undo step", "[core][effects][commands]")
{
    Commands f;
    const auto& chain = f.session.selectedChannelRef().effects; // the acid
    CHECK (f.line ("fx.add delay").isEmpty());
    CHECK (f.line ("fx.add dist").isEmpty()); // short names work
    CHECK (chain.slots[0].type.load() == EffectType::delay);
    CHECK (chain.slots[1].type.load() == EffectType::distortion);
    CHECK (f.line ("fx.set 2 mode crush").isEmpty());
    CHECK (chain.slots[1].values[0].load() == 2.0f);
    CHECK (f.line ("fx.set 1 feedback 2").contains ("between"));
    CHECK (f.line ("fx.set 1 sparkle 1").contains ("it has time, feedback"));
    CHECK (f.line ("fx.move 2 1").isEmpty()); // distortion before the delay
    CHECK (chain.slots[0].type.load() == EffectType::distortion);
    f.registry.undo();
    CHECK (chain.slots[0].type.load() == EffectType::delay);
    CHECK (f.line ("fx.remove 1").isEmpty());
    CHECK (chain.slots[0].type.load() == EffectType::none);
    CHECK (f.line ("fx.remove 1").contains ("empty"));
    CHECK (f.line ("fx.add chorus").contains ("unknown effect"));
    CHECK (f.line ("fx.add reverb master=on").isEmpty());
    CHECK (f.session.master.effects.slots[0].type.load() == EffectType::reverb);
}

TEST_CASE ("the pool limits big effects per project, with a clear error", "[core][effects][commands]")
{
    Commands f;
    for (int c = 1; c <= 4; ++c)
        for (int s = 0; s < 2; ++s)
            REQUIRE (f.line ("fx.add reverb channel=" + juce::String (c)).isEmpty());
    CHECK (f.line ("fx.add reverb master=on").contains ("at most 8 reverbs"));
    CHECK (f.line ("fx.add delay master=on").isEmpty()); // delays have their own 8
}

TEST_CASE ("effects are saved by name and read back; unknown ones are skipped with a warning",
           "[core][effects][project]")
{
    Commands f;
    REQUIRE (f.line ("fx.add distortion channel=1").isEmpty());
    REQUIRE (f.line ("fx.set 1 mode fold channel=1").isEmpty());
    REQUIRE (f.line ("fx.add reverb master=on").isEmpty());
    REQUIRE (f.line ("fx.bypass 1 on master=on").isEmpty());
    const auto text = io::project::toText (f.session);
    CHECK (text.contains ("\"type\": \"distortion\", \"bypass\": false, \"mode\": \"fold\""));
    CHECK (text.contains ("\"type\": \"reverb\", \"bypass\": true"));

    Session again;
    REQUIRE (io::project::fromText (text, again).wasOk());
    CHECK (again.channel (0).effects.load() == f.session.channel (0).effects.load());
    CHECK (again.master.effects.load() == f.session.master.effects.load());
    CHECK (io::project::toText (again) == text);

    Session newer;
    REQUIRE (
        io::project::fromText (text.replace ("\"type\": \"distortion\"", "\"type\": \"granulator\""), newer).wasOk());
    CHECK (newer.channel (0).effects.empty());
    REQUIRE (newer.project.warnings.size() == 1);
    CHECK (newer.project.warnings[0].contains ("granulator"));
    CHECK (io::project::fromText (text.replace ("\"mode\": \"fold\"", "\"mode\": \"melt\""), newer)
               .getErrorMessage()
               .contains ("mode"));
}

// --- waveshaper ------------------------------------------------------------------------------------

TEST_CASE ("the waveshaper's curve goes through its points, straight between them, flat beyond",
           "[core][effects][shaper]")
{
    const auto points = *shaperCurve ("tube");
    const auto curve = dsp::Shaper::Curve::through (points);
    for (int k = 0; k < shaperPoints; ++k)
        CHECK (std::abs (curve.at (-1.0 + dsp::Shaper::spacing * k) - points[static_cast<size_t> (k)]) < 1.0e-6);
    CHECK (std::abs (curve.at (-0.875) - 0.5 * (points[0] + points[1])) < 1.0e-6); // halfway: the average
    CHECK (curve.at (-3.0) == static_cast<double> (points.front()));
    CHECK (curve.at (2.0) == static_cast<double> (points.back()));

    // Its antiderivative: the slope of the integral is the curve, across the joints and past the ends.
    for (auto x = -1.4; x <= 1.4; x += 0.05)
    {
        const auto h = 1.0e-6;
        const auto slope = (curve.integralAt (x + h) - curve.integralAt (x - h)) / (2.0 * h);
        CAPTURE (x);
        CHECK (std::abs (slope - curve.at (x)) < 1.0e-4);
    }
}

TEST_CASE ("a waveshaper with the clean curve passes the sound unchanged", "[core][effects][shaper]")
{
    dsp::Shaper shaper;
    shaper.prepare (rate);
    auto params = defaultsOf (EffectType::shaper);
    const auto clean = *shaperCurve ("clean");
    for (size_t k = 0; k < clean.size(); ++k)
        params[static_cast<size_t> (dsp::Shaper::firstPoint) + k] = clean[k];
    params[dsp::Shaper::drive] = 0.0f;
    params[dsp::Shaper::output] = 0.0f;
    std::vector<float> left (9600), right (9600), input (9600);
    for (size_t i = 0; i < left.size(); ++i)
        input[i] = left[i] = right[i] =
            0.5f * static_cast<float> (std::sin (6.283185307179586 * 1000.0 * static_cast<double> (i) / rate));
    shaper.process (left.data(), right.data(), static_cast<int> (left.size()), params);
    double in = 0.0, out = 0.0;
    for (size_t i = 4800; i < left.size(); ++i) // after the DC blocker has settled
        in += static_cast<double> (input[i]) * input[i], out += static_cast<double> (left[i]) * left[i];
    CHECK (std::abs (std::sqrt (out / in) - 1.0) < 0.01);
}

TEST_CASE ("the waveshaper's ADAA aliases much less than reading the curve sample by sample", "[core][effects][shaper]")
{
    // The distortion's test: a loud 2.5 kHz sine, driven 24 dB into the hard curve.
    const auto fundamental = 2500.0;
    const auto hard = *shaperCurve ("hard");
    const auto curve = dsp::Shaper::Curve::through (hard);
    std::vector<float> naive (4800), smooth (4800), right (4800);
    for (size_t i = 0; i < naive.size(); ++i)
    {
        const auto s =
            0.8f * static_cast<float> (std::sin (6.283185307179586 * fundamental * static_cast<double> (i) / rate));
        smooth[i] = right[i] = s;
        naive[i] = static_cast<float> (curve.at (s * 15.85));
    }
    dsp::Shaper shaper;
    shaper.prepare (rate);
    auto params = defaultsOf (EffectType::shaper);
    for (size_t k = 0; k < hard.size(); ++k)
        params[static_cast<size_t> (dsp::Shaper::firstPoint) + k] = hard[k];
    params[dsp::Shaper::drive] = 24.0f;
    params[dsp::Shaper::output] = 0.0f;
    shaper.process (smooth.data(), right.data(), static_cast<int> (smooth.size()), params);

    const auto naiveAliasing = aliasingDb ({ naive.begin() + 2400, naive.end() }, fundamental);
    const auto adaaAliasing = aliasingDb ({ smooth.begin() + 2400, smooth.end() }, fundamental);
    CAPTURE (naiveAliasing, adaaAliasing);
    CHECK (adaaAliasing < naiveAliasing - 6.0);
}

TEST_CASE ("distortion and waveshaper tell how far their input reaches, for the graph's band",
           "[core][effects][shaper]")
{
    auto reach = [] (EffectType type, float driveDb)
    {
        std::vector<float> left (4800), right (4800);
        for (size_t i = 0; i < left.size(); ++i)
            left[i] = right[i] =
                0.5f * static_cast<float> (std::sin (6.283185307179586 * 220.0 * static_cast<double> (i) / rate));
        auto params = defaultsOf (type);
        if (type == EffectType::shaper)
        {
            dsp::Shaper shaper;
            shaper.prepare (rate);
            params[dsp::Shaper::drive] = driveDb;
            return shaper.process (left.data(), right.data(), static_cast<int> (left.size()), params).level;
        }
        dsp::Distortion distortion;
        distortion.prepare (rate);
        params[dsp::Distortion::drive] = driveDb;
        return distortion.process (left.data(), right.data(), static_cast<int> (left.size()), params).level;
    };
    CHECK (std::abs (reach (EffectType::distortion, 24.0f) - 0.5f) < 0.001f);    // the dry input: drive is in the curve
    CHECK (std::abs (reach (EffectType::shaper, 6.0f) - 0.5f * 1.995f) < 0.01f); // after the drive
}

TEST_CASE ("ready-made curves: fx.curve sets every point in one undo step", "[core][effects][commands][shaper]")
{
    Commands f;
    const auto& slot = f.session.selectedChannelRef().effects.slots[0];
    REQUIRE (f.line ("fx.add shaper").isEmpty());
    const auto before = slot.load();
    REQUIRE (f.line ("fx.curve 1 fold").isEmpty());
    const auto first = static_cast<size_t> (effectInfo (EffectType::shaper).paramIndex ("p1"));
    const auto fold = *shaperCurve ("fold");
    for (size_t k = 0; k < fold.size(); ++k)
        CHECK (slot.values[first + k].load() == fold[k]);
    REQUIRE (f.registry.undo());
    CHECK (slot.load() == before);

    CHECK (f.line ("fx.curve 1 jazz").contains ("there are soft, hard, fold, tube, rectify, clean"));
    REQUIRE (f.line ("fx.add delay").isEmpty());
    CHECK (f.line ("fx.curve 2 soft").contains ("not a waveshaper"));
    REQUIRE (f.line ("fx.set 1 p5 0.3").isEmpty()); // one point, by name
    CHECK (slot.values[first + 4].load() == 0.3f);
}

TEST_CASE ("the waveshaper's points are on its graph, not knobs, and its file keeps them", "[core][effects][shaper]")
{
    const auto& info = effectInfo (EffectType::shaper);
    REQUIRE (info.params.size() <= static_cast<size_t> (maxEffectParams));
    for (const auto& p : info.params)
        CHECK (p.graph == juce::String (p.name).startsWith ("p"));

    Commands f;
    REQUIRE (f.line ("fx.add shaper").isEmpty());
    REQUIRE (f.line ("fx.curve 1 tube").isEmpty());
    REQUIRE (f.line ("fx.set 1 p3 -0.2").isEmpty());
    const auto text = io::project::toText (f.session);
    Session reopened;
    REQUIRE (io::project::fromText (text, reopened).wasOk());
    CHECK (reopened.selectedChannelRef().effects.slots[0].load() ==
           f.session.selectedChannelRef().effects.slots[0].load());
}

TEST_CASE ("the reverb's drawn tail lasts about as long as the real one", "[core][effects]")
{
    for (const auto decay : { 0.3f, 0.6f })
    {
        dsp::PlateReverb reverb;
        reverb.prepare (rate);
        auto params = defaultsOf (EffectType::reverb);
        params[dsp::PlateReverb::decay] = decay;
        params[dsp::PlateReverb::damping] = 0.0f; // the drawn tail is the lows', which damping spares
        params[dsp::PlateReverb::lowcut] = 0.0f;
        params[dsp::PlateReverb::predelay] = 0.0f;
        params[dsp::PlateReverb::mix] = 1.0f;
        std::vector<float> left (static_cast<size_t> (rate * 8.0)), right (left.size());
        left[0] = right[0] = 1.0f;
        reverb.process (left.data(), right.data(), static_cast<int> (left.size()), params);

        // Level in 20 ms windows; the time from the loudest one to 30 dB below it, doubled, is RT60.
        const auto window = static_cast<size_t> (rate * 0.02);
        std::vector<double> db;
        for (size_t start = 0; start + window <= left.size(); start += window)
        {
            double sum = 0.0;
            for (size_t i = start; i < start + window; ++i)
                sum += static_cast<double> (left[i]) * left[i] + static_cast<double> (right[i]) * right[i];
            db.push_back (10.0 * std::log10 (sum / static_cast<double> (window) + 1.0e-30));
        }
        const auto loudest = static_cast<size_t> (std::max_element (db.begin(), db.end()) - db.begin());
        auto quiet = loudest;
        while (quiet < db.size() && db[quiet] > db[loudest] - 30.0)
            ++quiet;
        const auto measured = 2.0 * static_cast<double> (quiet - loudest) * 0.02;
        const auto drawn = static_cast<double> (dsp::PlateReverb::tailSeconds (decay));
        CAPTURE (decay, measured, drawn);
        CHECK (drawn > measured * 0.6);
        CHECK (drawn < measured * 1.6);
    }
}

// --- the 303's shape and glide ----------------------------------------------------------------------

TEST_CASE ("shape morphs from saw to square: the even harmonics fade out", "[core][dsp][acid]")
{
    // The 2nd harmonic belongs to the saw only (a square has odd ones), so its level falls in a straight
    // line as the shape goes from 0 to 1. (Its ratio to the fundamental doesn't: the two waves'
    // fundamentals are in opposite phase and partly cancel in the middle.)
    auto secondHarmonic = [] (float shape)
    {
        dsp::AcidVoice voice;
        voice.prepare (rate);
        dsp::AcidVoice::Controls c;
        c.cutoff = 1.0f;
        c.resonance = 0.0f;
        c.envMod = 0.0f;
        c.shape = shape;
        voice.setControls (c);
        voice.reset();
        voice.noteOn (45, false); // 110 Hz
        std::vector<float> x (48000);
        for (auto& v : x)
            v = voice.process();
        std::complex<double> second {};
        for (size_t i = 24000; i < x.size(); ++i)
            second += static_cast<double> (x[i]) *
                      std::polar (1.0, -6.283185307179586 * 220.0 * static_cast<double> (i) / rate);
        return std::abs (second);
    };
    const auto saw = secondHarmonic (0.0f), half = secondHarmonic (0.5f), square = secondHarmonic (1.0f);
    CAPTURE (saw, half, square);
    CHECK (square < saw * 0.01);
    // About half way, not exactly: the 303's filter and output stage saturate a little, and on a lopsided
    // wave that adds some even harmonics of its own.
    CHECK (half > saw * 0.35);
    CHECK (half < saw * 0.75);
}

TEST_CASE ("a longer glide takes longer to reach the new note", "[core][dsp][acid]")
{
    // The pitch 60 ms into a slide up an octave (110 -> 220 Hz), from the zero crossings of a clean saw.
    auto pitchAfterSlide = [] (float glideSeconds)
    {
        dsp::AcidVoice voice;
        dsp::AcidVoice::Controls c;
        c.cutoff = 1.0f;
        c.resonance = 0.0f;
        c.envMod = 0.0f;
        c.glide = glideSeconds;
        voice.setControls (c);
        voice.prepare (rate);
        voice.noteOn (45, false);
        voice.glideTo (57);
        const auto from = static_cast<int> (0.06 * rate), to = static_cast<int> (0.1 * rate);
        int crossings = 0;
        float previous = 0.0f;
        for (int i = 0; i < to; ++i)
        {
            const auto v = voice.process();
            if (i >= from && previous > 0.0f && v <= 0.0f) // the saw's falling edge, once per period
                ++crossings;
            previous = v;
        }
        return crossings / ((to - from) / rate);
    };
    const auto quick = pitchAfterSlide (0.035f), slow = pitchAfterSlide (0.2f);
    CAPTURE (quick, slow);
    CHECK (quick >= 190.0); // nearly there (counted in whole periods: 25 Hz steps)
    CHECK (slow < 170.0);   // still on its way
}
