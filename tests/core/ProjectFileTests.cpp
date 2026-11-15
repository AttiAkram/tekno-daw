// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include "core/commands/SessionCommands.h"
#include "core/engine/AudioEngine.h"
#include "core/io/ProjectFile.h"
#include "core/model/NoteNames.h"

using namespace tekno::core;
namespace project = tekno::core::io::project;

namespace
{
/** A session with something non-default everywhere a file can store it. */
void makeInteresting (Session& s)
{
    s.transport.bpm = 138.5;
    s.transport.swing = 62.0f;
    s.master.volumeDb = -2.5f;
    const auto acid = s.selectedChannel.load();
    auto& synth = s.channel (acid).acid;
    synth.cutoff = 0.12f;
    synth.resonance = 0.95f;
    synth.envMod = 0.33f;
    synth.decay = 0.7f;
    synth.accent = 1.0f;
    synth.drive = 0.4f;
    synth.shape = 0.4f; // a blend: saved as a number, read back the same
    synth.glide = 0.12f;
    s.channel (acid).mixer.pan = -0.25f;
    s.channel (0).sampler.tune = -3.0f;
    s.channel (0).sampler.decay = 0.4f;
    s.channel (1).mixer.mute = true;
    s.channel (3).mixer.solo = true;
    s.project.channelNames[2] = "ride \"open\"";
    s.pattern (0).setStep (acid, 5, Step { true, false, false, 61, true }); // a tie
    s.pattern (0).setStep (0, 3, Step { true, true, false, 67 });           // a kick a fifth up
    s.pattern (2).setLength (13);
    s.pattern (2).setStep (acid, 12, Step { true, true, true, 96 });
    s.pattern (2).setStep (acid, 40, Step { true, false, false, 24 }); // past the length: must survive
    s.project.patternNames[2] = "rumble \"quoted\" / ünïcode";
    s.selected = 2;
}

void requireSameChannel (const ChannelData& a, const ChannelData& b)
{
    REQUIRE (a.kind == b.kind);
    REQUIRE (a.name == b.name);
    REQUIRE (a.sample == b.sample);
    REQUIRE (a.acid == b.acid);
    REQUIRE (a.sampler == b.sampler);
    REQUIRE (a.mixer == b.mixer);
    for (size_t p = 0; p < a.lanes.size(); ++p)
        REQUIRE (a.lanes[p] == b.lanes[p]);
}

void requireSame (const Session& a, const Session& b)
{
    REQUIRE (a.transport.bpm.load() == b.transport.bpm.load());
    REQUIRE (a.transport.swing.load() == b.transport.swing.load());
    REQUIRE (a.master.volumeDb.load() == b.master.volumeDb.load());
    REQUIRE (a.selected.load() == b.selected.load());
    REQUIRE (a.selectedChannel.load() == b.selectedChannel.load());
    REQUIRE (a.channelCount() == b.channelCount());
    for (int c = 0; c < a.channelCount(); ++c)
    {
        CAPTURE (c);
        requireSameChannel (a.captureChannel (c), b.captureChannel (c));
    }
    for (int p = 0; p < Session::maxPatterns; ++p)
    {
        CAPTURE (p);
        REQUIRE (a.project.patternNames[static_cast<size_t> (p)] == b.project.patternNames[static_cast<size_t> (p)]);
        REQUIRE (a.pattern (p).getLength() == b.pattern (p).getLength());
    }
}

/** An M2 project, as M2 wrote it: one synth, one line per pattern. */
const char* version1File = R"({
  "format": "tekno-daw/project",
  "version": 1,
  "tempo": { "bpm": 140, "swing": 58 },
  "synth": { "cutoff": 0.2, "resonance": 0.9, "envMod": 0.5, "decay": 0.3, "accent": 0.8, "drive": 0.25, "waveform": "square" },
  "selectedPattern": 2,
  "patterns": [
    {
      "number": 2,
      "name": "old line",
      "length": 3,
      "steps": [
        { "on": true, "note": "A2", "accent": true, "slide": false, "tie": false },
        { "on": false, "note": "A2", "accent": false, "slide": false, "tie": false },
        { "on": true, "note": "C3", "accent": false, "slide": true, "tie": false }
      ]
    }
  ]
}
)";
} // namespace

TEST_CASE ("note names round-trip and accept flats", "[core][notes]")
{
    for (int n = Step::minNote; n <= Step::maxNote; ++n)
        REQUIRE (parseNoteName (noteName (n)) == n);
    CHECK (noteName (45) == "A2");
    CHECK (noteName (60) == "C4");
    CHECK (parseNoteName ("Db3") == 49);
    CHECK (parseNoteName ("c#3") == 49);
    CHECK_FALSE (parseNoteName ("H2").has_value());
    CHECK_FALSE (parseNoteName ("A").has_value());
    CHECK_FALSE (parseNoteName ("A2x").has_value());
}

TEST_CASE ("a project survives saving and opening, exactly", "[core][project]")
{
    Session original;
    makeInteresting (original);

    const auto file = juce::File::createTempFile (project::extension);
    REQUIRE (project::save (original, file).wasOk());

    Session reopened;
    REQUIRE (project::load (file, reopened).wasOk());
    requireSame (original, reopened);

    // And the text is stable: saving the reopened project gives the same bytes.
    CHECK (project::toText (reopened) == file.loadFileAsString());
    file.deleteFile();
}

TEST_CASE ("the file is readable and diffable", "[core][project]")
{
    Session s;
    const auto text = project::toText (s);
    CHECK (text.contains ("\"format\": \"tekno-daw/project\""));
    CHECK (text.contains ("\"version\": 3"));
    CHECK (text.contains ("\"ticksPerStep\": 24"));
    CHECK (text.contains ("\"sampler\": { \"sample\": \"builtin:kick\""));
    CHECK (text.contains ("\"name\": \"acid 01\""));
    CHECK (text.contains ("{ \"at\": 0, \"length\": 24, \"note\": \"A2\", \"accent\": true, \"slide\": false, \"velocity\": 100 }"));
    // one note per line, and only patterns that hold something
    CHECK (juce::StringArray::fromLines (text).size() > 16);
    CHECK_FALSE (text.contains ("\"number\": 2"));
}

TEST_CASE ("a broken file says where and leaves the session alone", "[core][project]")
{
    Session s;
    makeInteresting (s);
    const auto before = project::toText (s);

    auto broken = project::toText (Session {}).replace ("\"note\": \"A3\"", "\"note\": \"H3\"");
    auto result = project::fromText (broken, s);
    REQUIRE (result.failed());
    CHECK (result.getErrorMessage().contains ("patterns[0].lanes[3].notes[1].note"));
    CHECK (result.getErrorMessage().contains ("H3"));
    CHECK (project::toText (s) == before);

    CHECK (project::fromText ("not json at all", s).failed());
    CHECK (project::fromText ("{ \"hello\": 1 }", s).getErrorMessage().contains ("not a tekno-daw project"));
    CHECK (project::fromText (project::toText (s).replace ("\"version\": 3", "\"version\": 9"), s)
               .getErrorMessage()
               .contains ("newer tekno-daw"));
    CHECK (project::fromText (project::toText (s).replace ("\"bpm\": 138.5", "\"bpm\": 9000"), s)
               .getErrorMessage()
               .contains ("tempo.bpm"));
    CHECK (project::toText (s) == before);
}

TEST_CASE ("missing optional fields fall back to defaults", "[core][project]")
{
    Session s;
    REQUIRE (project::fromText ("{ \"format\": \"tekno-daw/project\", \"version\": 1 }", s).wasOk());
    CHECK (s.transport.bpm.load() == TransportState::defaultBpm);
    CHECK (s.pattern (0).isEmpty());
    CHECK (s.channelCount() == 1);
}

TEST_CASE ("a version 1 project from M2 still opens, as one acid channel", "[core][project]")
{
    Session s;
    REQUIRE (project::fromText (version1File, s).wasOk());
    REQUIRE (s.channelCount() == 1);
    CHECK (s.channel (0).kind.load() == InstrumentKind::acid);
    CHECK (s.channel (0).acid.shape.load() == 1.0f); // "square" in an old file
    CHECK (s.channel (0).acid.drive.load() == 0.25f);
    CHECK (s.transport.bpm.load() == 140.0);
    CHECK (s.selected.load() == 1);
    CHECK (s.project.patternNames[1] == "old line");
    CHECK (s.pattern (1).getLength() == 3);
    CHECK (s.pattern (1).getStep (0, 0) == Step { true, true, false, 45 });
    CHECK (s.pattern (1).getStep (0, 2).slide);

    // Saved again, it is a version 3 file that reads back the same.
    const auto text = project::toText (s);
    CHECK (text.contains ("\"version\": 3"));
    Session again;
    REQUIRE (project::fromText (text, again).wasOk());
    requireSame (s, again);
}

TEST_CASE ("a missing sample is a warning, and the reference survives", "[core][project][sampler]")
{
    const auto folder = juce::File::createTempFile ("tekno-project");
    REQUIRE (folder.createDirectory());
    const auto samplesFolder = folder.getChildFile ("samples");
    REQUIRE (samplesFolder.createDirectory());
    const auto wav = samplesFolder.getChildFile ("snare.wav");

    // A real file next to the project: written relative, so the folder can move.
    {
        juce::AudioBuffer<float> audio (1, 4800);
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample (0, i, std::sin (static_cast<float> (i) * 0.05f) * 0.5f);
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (wav);
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (
            stream,
            juce::AudioFormatWriterOptions {}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (16));
        REQUIRE (writer != nullptr);
        REQUIRE (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()));
    }

    Session s;
    REQUIRE (s.setChannelSample (0, wav.getFullPathName()).isEmpty());
    REQUIRE (s.channel (0).sample.load() != nullptr);
    const auto projectFile = folder.getChildFile ("song.tekno");
    REQUIRE (project::save (s, projectFile).wasOk());
    CHECK (projectFile.loadFileAsString().contains ("\"sample\": \"samples/snare.wav\""));

    Session reopened;
    REQUIRE (project::load (projectFile, reopened).wasOk());
    CHECK (reopened.channel (0).sample.load() != nullptr);
    CHECK (reopened.project.warnings.isEmpty());

    // The drive is unplugged: the project opens, the channel is silent, saving keeps the reference.
    REQUIRE (wav.deleteFile());
    Session unplugged;
    REQUIRE (project::load (projectFile, unplugged).wasOk());
    CHECK (unplugged.channel (0).sample.load() == nullptr);
    REQUIRE (unplugged.project.warnings.size() == 1);
    CHECK (unplugged.project.warnings[0].contains ("channel 1 (kick)"));
    CHECK (unplugged.project.warnings[0].contains ("not found"));
    CHECK (project::toText (unplugged, folder).contains ("\"sample\": \"samples/snare.wav\""));

    folder.deleteRecursively();
}

TEST_CASE ("project commands save, mark changes and open", "[core][project][commands]")
{
    Session s;
    CommandRegistry r;
    registerSessionCommands (r, s);
    const auto file = juce::File::createTempFile (project::extension);

    CHECK (r.execute ("project.save").error.contains ("saveAs"));
    CHECK_FALSE (s.project.hasUnsavedChanges());
    r.execute ("pattern.step.toggle", { { "step", 2 } });
    CHECK (s.project.hasUnsavedChanges());
    r.execute ("transport.play"); // playing is not a change to the project
    REQUIRE (r.execute ("project.saveAs", { { "path", file.getFullPathName() } }).succeeded());
    CHECK_FALSE (s.project.hasUnsavedChanges());
    CHECK (s.project.file == file);

    r.execute ("edit.undo"); // undo is a change too
    CHECK (s.project.hasUnsavedChanges());

    REQUIRE (r.execute ("project.open", { { "path", file.getFullPathName() } }).succeeded());
    CHECK (s.pattern (0).getStep (s.selectedChannel, 1).on); // the saved state, not the undone one
    CHECK_FALSE (s.project.hasUnsavedChanges());
    CHECK_FALSE (r.canUndo()); // history belongs to the previous document
    CHECK_FALSE (s.transport.playing.load());

    CHECK (r.execute ("project.open", { { "path", "/nope/missing.tekno" } }).error.contains ("not found"));
    file.deleteFile();
}

TEST_CASE ("a saved and reopened project sounds identical, sample for sample", "[core][project][engine]")
{
    // M2's done criterion, as a test: write a line, save, reopen, and hear exactly the same thing.
    auto renderSelected = [] (Session& s)
    {
        tekno::core::AudioEngine engine (s);
        engine.prepare (48000.0);
        s.transport.playing = true;
        std::vector<float> out (48000 * 4);
        for (size_t pos = 0; pos < out.size(); pos += 512)
        {
            float* ch = out.data() + pos;
            engine.render (&ch, 1, static_cast<int> (std::min<size_t> (512, out.size() - pos)));
        }
        s.transport.playing = false;
        return out;
    };

    Session original;
    makeInteresting (original);
    original.selected = 0;                   // the loop, with the tie and the tuned kick added by makeInteresting
    original.channel (3).mixer.solo = false; // every channel playing
    const auto file = juce::File::createTempFile (project::extension);
    REQUIRE (project::save (original, file).wasOk());

    Session reopened;
    REQUIRE (project::load (file, reopened).wasOk());

    const auto a = renderSelected (original);
    const auto b = renderSelected (reopened);
    REQUIRE (a.size() == b.size());
    CHECK (std::equal (a.begin(), a.end(), b.begin()));
    file.deleteFile();
}
