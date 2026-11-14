// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// The mixer view's numbers: meter ballistics and the fader taper. The drawing itself is checked on
// the snapshot (app.snapshot.mixer).

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ui/components/LevelMeters.h"
#include "ui/theme/Tokens.h"
#include "ui/views/MixerView.h"
#include "ui/views/PianoRollView.h"

using tekno::ui::LevelMeters;
using tekno::ui::MixerView;

TEST_CASE ("a meter rises at once and falls at a steady rate", "[ui][meters]")
{
    LevelMeters::Level level;
    level = LevelMeters::advance (level, 0.5f, 1.0 / 60.0, 0.0); // -6 dB
    CHECK (level.db == Catch::Approx (-6.02f).margin (0.01f));
    CHECK (level.holdDb == Catch::Approx (-6.02f).margin (0.01f));

    // Half a second of silence: it falls by half the rate, the peak line stays.
    level = LevelMeters::advance (level, 0.0f, 0.5, 500.0);
    CHECK (level.db == Catch::Approx (-6.02f - tekno::ui::tokens::motion::meterFallDbPerSecond * 0.5f).margin (0.01f));
    CHECK (level.holdDb == Catch::Approx (-6.02f).margin (0.01f));

    // After the hold time, the line comes down with the bar; long silence ends at the bottom.
    level = LevelMeters::advance (level, 0.0f, 0.5, tekno::ui::tokens::motion::meterHoldMs + 600.0);
    CHECK (level.holdDb == Catch::Approx (level.db));
    level = LevelMeters::advance (level, 0.0f, 10.0, 20000.0);
    CHECK (level.db == LevelMeters::minDb);
}

TEST_CASE ("the clip light stays on until reset", "[ui][meters]")
{
    LevelMeters::Level level;
    level = LevelMeters::advance (level, 0.9f, 0.016, 0.0);
    CHECK_FALSE (level.clipped);
    level = LevelMeters::advance (level, 1.0f, 0.016, 16.0);
    CHECK (level.clipped);
    level = LevelMeters::advance (level, 0.0f, 5.0, 5000.0);
    CHECK (level.clipped);
    CHECK (level.db <= LevelMeters::maxDb);
}

TEST_CASE ("the fader puts -12 dB halfway, the ends at -60 and +6", "[ui][mixer]")
{
    CHECK (MixerView::faderProportion (-60.0f) == 0.0f);
    CHECK (MixerView::faderProportion (6.0f) == Catch::Approx (1.0f));
    CHECK (MixerView::faderProportion (-12.0f) == Catch::Approx (0.5f));
    CHECK (MixerView::faderProportion (0.0f) > 0.75f); // unity sits high: the useful range gets the travel
    CHECK (MixerView::faderProportion (-100.0f) == 0.0f);

    // Same taper as a JUCE slider skewed from the same midpoint, so meter and fader line up.
    juce::Slider fader;
    fader.setRange (-60.0, 6.0);
    fader.setSkewFactorFromMidPoint (MixerView::midPointDb);
    for (const auto db : { -48.0, -24.0, -6.0, 0.0, 3.0 })
        CHECK (fader.valueToProportionOfLength (db) ==
               Catch::Approx (MixerView::faderProportion (static_cast<float> (db))).margin (1.0e-4));
}

TEST_CASE ("piano roll snaps: names and sizes in ticks", "[ui][pianoroll]")
{
    using tekno::ui::PianoRollView;
    using Snap = PianoRollView::Snap;
    for (const auto snap : { Snap::step, Snap::thirtySecond, Snap::triplet, Snap::free })
        CHECK (PianoRollView::snapFromName (PianoRollView::snapName (snap)) == snap);
    CHECK (PianoRollView::snapTicks (Snap::step) == 24);
    CHECK (PianoRollView::snapTicks (Snap::thirtySecond) == 12);
    CHECK (PianoRollView::snapTicks (Snap::triplet) == 16); // three in two 1/16s
    CHECK (PianoRollView::snapTicks (Snap::free) == 1);
    CHECK_FALSE (PianoRollView::snapFromName ("1/7").has_value());
}

#include "ui/Preferences.h"

TEST_CASE ("preferences are kept between runs, and unknown values fall back", "[ui][preferences]")
{
    using tekno::ui::Preferences;
    const auto file = juce::File::createTempFile (".xml");
    {
        Preferences prefs (file);
        CHECK (prefs.noteNaming() == tekno::core::NoteNaming::letters); // the default
        CHECK (prefs.snap() == "step");
        prefs.setNoteNaming (tekno::core::NoteNaming::latin);
        prefs.setSnap ("1/3");
        prefs.setTheme (tekno::ui::tokens::Theme::light);
    }
    {
        Preferences again (file);
        CHECK (again.noteNaming() == tekno::core::NoteNaming::latin);
        CHECK (again.snap() == "1/3");
        CHECK (again.theme() == tekno::ui::tokens::Theme::light);
    }
    CHECK (Preferences::namingFromName ("doremi") == tekno::core::NoteNaming::latin);
    CHECK (Preferences::namingFromName ("ABC") == tekno::core::NoteNaming::letters);
    CHECK_FALSE (Preferences::namingFromName ("klingon").has_value());
    file.deleteFile();
}
