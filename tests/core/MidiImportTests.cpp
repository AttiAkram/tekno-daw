// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

// Help for people who don't read letters: Do Re Mi names, and melodies imported from MIDI files.

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_basics/juce_audio_basics.h>

#include "core/commands/CommandLine.h"
#include "core/commands/SessionCommands.h"
#include "core/io/MidiImport.h"
#include "core/io/ProjectFile.h"
#include "core/model/NoteNames.h"

using namespace tekno::core;

namespace
{
/** Restores the process-wide naming when a test changes it. */
struct NamingGuard
{
    NoteNaming before = getNoteNaming();
    ~NamingGuard() { setNoteNaming (before); }
};

/** Writes a .mid file: a tempo-only track first (as many files have), then one track per melody. */
juce::File writeMidi (const std::vector<std::vector<std::pair<double, int>>>& tracks, int ppq = 480,
                      double beatsPerNote = 1.0)
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    juce::MidiMessageSequence tempo;
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0);
    midi.addTrack (tempo);
    for (const auto& notes : tracks)
    {
        juce::MidiMessageSequence seq;
        for (const auto& [beat, pitch] : notes)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (90)), beat * ppq);
            seq.addEvent (juce::MidiMessage::noteOff (1, pitch), (beat + beatsPerNote * 0.5) * ppq);
        }
        seq.updateMatchedPairs();
        midi.addTrack (seq);
    }
    const auto file = juce::File::createTempFile (".mid");
    juce::FileOutputStream out (file);
    REQUIRE (out.openedOk());
    REQUIRE (midi.writeTo (out));
    return file;
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

TEST_CASE ("notes can be named Do Re Mi, with the same octave numbers", "[core][notes]")
{
    CHECK (noteName (60, NoteNaming::latin) == "Do4");
    CHECK (noteName (45, NoteNaming::latin) == "La2");
    CHECK (noteName (56, NoteNaming::latin) == "Sol#3");
    CHECK (noteName (71, NoteNaming::latin) == "Si4");
    for (int n = Step::minNote; n <= Step::maxNote; ++n)
    {
        REQUIRE (parseNoteName (noteName (n, NoteNaming::latin)) == n);
        REQUIRE (parseNoteName (noteName (n, NoteNaming::letters)) == n);
    }
    CHECK (parseNoteName ("la2") == 45);
    CHECK (parseNoteName ("SOL3") == 55);
    CHECK (parseNoteName ("Sib2") == 46); // Si flat
    CHECK (parseNoteName ("Reb3") == 49);
    CHECK_FALSE (parseNoteName ("Re").has_value());
    CHECK_FALSE (parseNoteName ("Ut3").has_value());
}

TEST_CASE ("the preferred naming changes what people see, never the file", "[core][notes][project]")
{
    NamingGuard guard;
    Fixture f;
    setNoteNaming (NoteNaming::latin);
    CHECK (noteName (45) == "La2");
    CHECK (f.line ("note.remove 7 Do2").contains ("there is no Do2"));
    CHECK (f.line ("note 3 Mi3").isEmpty()); // the terminal reads both
    CHECK (f.session.pattern (0).getStep (4, 2).note == 52);

    const auto text = io::project::toText (f.session);
    CHECK (text.contains ("\"note\": \"E3\""));
    CHECK_FALSE (text.contains ("Mi3"));
}

TEST_CASE ("a MIDI melody becomes a lane, timing kept, empty bars dropped", "[core][midi]")
{
    // Track 2 (after the tempo track): a bar of silence, then 4 quarter notes and an eighth off the beat.
    const auto file = writeMidi ({ { { 4.0, 60 }, { 5.0, 62 }, { 6.0, 64 }, { 7.0, 65 }, { 7.5, 67 } } });
    io::MidiMelody melody;
    REQUIRE (io::readMidiMelody (file, -1, melody).wasOk());
    CHECK (melody.track == 1);
    CHECK (melody.tracksWithNotes == 1);
    REQUIRE (melody.notes.size() == 5);
    CHECK (melody.notes[0] == Note { 0, 48, 60, false, false, 90 }); // half a beat, its velocity from the file
    CHECK (melody.notes[1].start == 96);
    CHECK (melody.notes[4].start == 3 * 96 + 48); // off the beat, not quantised
    CHECK (melody.lengthInSteps == 16);
    CHECK (melody.summary().contains ("imported 5 notes from track 2"));
    file.deleteFile();
}

TEST_CASE ("MIDI import picks a track, fits the range, and keeps to 4 bars", "[core][midi]")
{
    std::vector<std::pair<double, int>> low, high, longOne;
    for (int i = 0; i < 4; ++i)
        low.push_back ({ i, 12 + i }); // below C1: moved up an octave
    for (int i = 0; i < 4; ++i)
        high.push_back ({ i, 100 + i }); // above C7: moved down
    for (int i = 0; i < 24; ++i)
        longOne.push_back ({ i, 48 }); // 6 bars of quarters: 2 bars too many
    const auto file = writeMidi ({ low, high, longOne });

    io::MidiMelody melody;
    REQUIRE (io::readMidiMelody (file, -1, melody).wasOk());
    CHECK (melody.tracksWithNotes == 3);
    CHECK (melody.octavesShifted == 1);
    CHECK (melody.notes[0].pitch == 24);

    REQUIRE (io::readMidiMelody (file, 2, melody).wasOk());
    CHECK (melody.octavesShifted == -1);
    CHECK (melody.notes[0].pitch == 88);

    REQUIRE (io::readMidiMelody (file, 3, melody).wasOk());
    CHECK (melody.notes.size() == 16);
    CHECK (melody.droppedNotes == 8);
    CHECK (melody.lengthInSteps == 64);
    CHECK (melody.summary().contains ("8 notes left out"));

    CHECK (io::readMidiMelody (file, 0, melody).getErrorMessage().contains ("has no notes"));
    file.deleteFile();

    const auto notMidi = juce::File::createTempFile (".mid");
    notMidi.replaceWithText ("hello");
    CHECK (io::readMidiMelody (notMidi, -1, melody).getErrorMessage().contains ("not a MIDI file"));
    notMidi.deleteFile();
}

TEST_CASE ("midi.import replaces a lane and is one undo step", "[core][midi][commands]")
{
    Fixture f;
    const auto file = writeMidi ({ { { 0.0, 60 }, { 4.0, 62 }, { 8.0, 64 } } });
    const auto before = f.session.pattern (0).lane (4).notes();

    const auto outcome = f.registry.execute ("midi.import", { { "path", file.getFullPathName() } });
    REQUIRE (outcome.succeeded());
    CHECK (outcome.note.contains ("3 notes"));
    CHECK (f.session.pattern (0).lane (4).notes().size() == 3);
    CHECK (f.session.pattern (0).getLength() == 48); // 3 bars

    f.registry.undo();
    CHECK (f.session.pattern (0).lane (4).notes() == before);
    CHECK (f.session.pattern (0).getLength() == 16);

    CHECK (f.line ("midi.import /nope/missing.mid").contains ("cannot read"));
    file.deleteFile();
}
