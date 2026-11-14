// tekno-daw — pattern-based DAW for acid / mental tekno
// Copyright (C) 2026 tekno-daw contributors
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU Affero General Public License as published by the Free Software Foundation, either version 3
// of the License, or (at your option) any later version. See the LICENSE file for details.

#include "core/io/ProjectFile.h"

#include <array>
#include <vector>

#include "core/model/NoteNames.h"

namespace tekno::core::io::project
{
namespace
{
    // --- writing --------------------------------------------------------------------------------

    juce::String number (double value)
    {
        // Fixed precision, then trimmed: stable across runs and readable ("0.35", "130").
        auto text = juce::String (value, 4);
        if (text.contains ("."))
            text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return text == "-0" ? "0" : text;
    }

    juce::String quoted (const juce::String& text)
    {
        return juce::JSON::toString (juce::var (text));
    }

    juce::String boolean (bool value)
    {
        return value ? "true" : "false";
    }

    juce::String stepLine (const Step& s)
    {
        return "{ \"on\": " + boolean (s.on) + ", \"note\": " + quoted (noteName (s.note)) +
               ", \"accent\": " + boolean (s.accent) + ", \"slide\": " + boolean (s.slide) +
               ", \"tie\": " + boolean (s.tie) + " }";
    }

    // --- reading --------------------------------------------------------------------------------

    /** Plain copy of everything a file holds, filled and validated before touching the session. */
    struct PatternData
    {
        bool present = false;
        juce::String name;
        int length = Pattern::defaultLength;
        std::array<Step, Pattern::maxSteps> steps {};
    };

    struct ProjectData
    {
        double bpm = TransportState::defaultBpm;
        float swing = TransportState::minSwing;
        float cutoff = AcidParams::defaultCutoff, resonance = AcidParams::defaultResonance,
              envMod = AcidParams::defaultEnvMod, decay = AcidParams::defaultDecay, accent = AcidParams::defaultAccent,
              drive = AcidParams::defaultDrive;
        bool square = false;
        int selected = 0;
        std::array<PatternData, Session::maxPatterns> patterns {};
    };

    /** Reads typed values out of a juce::var, remembering the first error and where it happened. */
    class Reader
    {
    public:
        juce::String error;

        bool ok() const noexcept { return error.isEmpty(); }

        void fail (const juce::String& path, const juce::String& what)
        {
            if (error.isEmpty())
                error = path + ": " + what;
        }

        double number (const juce::var& obj, const char* key, const juce::String& path, double fallback, double min,
                       double max)
        {
            const auto v = obj[key];
            if (v.isVoid())
                return fallback;
            if (! (v.isInt() || v.isInt64() || v.isDouble()))
            {
                fail (path + "." + key, "must be a number");
                return fallback;
            }
            const auto d = static_cast<double> (v);
            if (d < min || d > max)
                fail (path + "." + key,
                      juce::String (d) + " is outside " + juce::String (min) + ".." + juce::String (max));
            return d;
        }

        bool boolean (const juce::var& obj, const char* key, const juce::String& path, bool fallback)
        {
            const auto v = obj[key];
            if (v.isVoid())
                return fallback;
            if (! v.isBool())
            {
                fail (path + "." + key, "must be true or false");
                return fallback;
            }
            return static_cast<bool> (v);
        }

        juce::String text (const juce::var& obj, const char* key, const juce::String& path,
                           const juce::String& fallback)
        {
            const auto v = obj[key];
            if (v.isVoid())
                return fallback;
            if (! v.isString())
            {
                fail (path + "." + key, "must be text");
                return fallback;
            }
            return v.toString();
        }
    };

    juce::Result parse (const juce::String& text, ProjectData& data)
    {
        juce::var root;
        if (const auto parsed = juce::JSON::parse (text, root); parsed.failed())
            return juce::Result::fail ("not valid JSON: " + parsed.getErrorMessage());
        if (! root.isObject())
            return juce::Result::fail ("not a tekno-daw project");

        Reader r;
        if (r.text (root, "format", "project", {}) != formatId)
            return juce::Result::fail ("not a tekno-daw project (missing \"format\": \"" + juce::String (formatId) +
                                       "\")");

        const auto version = static_cast<int> (r.number (root, "version", "project", 0, 0, 1'000'000));
        if (version > formatVersion)
            return juce::Result::fail ("this project was saved by a newer tekno-daw (format version " +
                                       juce::String (version) + ", this one reads up to " +
                                       juce::String (formatVersion) + ")");

        const auto tempo = root["tempo"];
        data.bpm = r.number (tempo, "bpm", "tempo", data.bpm, TransportState::minBpm, TransportState::maxBpm);
        data.swing = static_cast<float> (
            r.number (tempo, "swing", "tempo", data.swing, TransportState::minSwing, TransportState::maxSwing));

        const auto synth = root["synth"];
        auto knob = [&] (const char* key, float fallback)
        { return static_cast<float> (r.number (synth, key, "synth", fallback, 0.0, 1.0)); };
        data.cutoff = knob ("cutoff", data.cutoff);
        data.resonance = knob ("resonance", data.resonance);
        data.envMod = knob ("envMod", data.envMod);
        data.decay = knob ("decay", data.decay);
        data.accent = knob ("accent", data.accent);
        data.drive = knob ("drive", data.drive);
        const auto waveform = r.text (synth, "waveform", "synth", "saw");
        if (waveform != "saw" && waveform != "square")
            r.fail ("synth.waveform", "'" + waveform + "' must be \"saw\" or \"square\"");
        data.square = waveform == "square";

        data.selected =
            static_cast<int> (r.number (root, "selectedPattern", "project", 1, 1, Session::maxPatterns)) - 1;

        const auto patterns = root["patterns"];
        if (! patterns.isVoid() && ! patterns.isArray())
            r.fail ("patterns", "must be a list");

        for (int i = 0; patterns.isArray() && i < patterns.size(); ++i)
        {
            const auto p = patterns[i];
            const auto path = "patterns[" + juce::String (i) + "]";
            const auto numberInFile = static_cast<int> (r.number (p, "number", path, 0, 1, Session::maxPatterns));
            if (! r.ok())
                break;
            if (numberInFile == 0)
            {
                r.fail (path + ".number", "is missing");
                break;
            }

            auto& out = data.patterns[static_cast<size_t> (numberInFile - 1)];
            if (out.present)
            {
                r.fail (path + ".number", "pattern " + juce::String (numberInFile) + " appears twice");
                break;
            }
            out.present = true;
            out.name = r.text (p, "name", path, Session::defaultPatternName (numberInFile - 1));
            out.length = static_cast<int> (r.number (p, "length", path, Pattern::defaultLength, 1, Pattern::maxSteps));

            const auto steps = p["steps"];
            if (! steps.isVoid() && (! steps.isArray() || steps.size() > Pattern::maxSteps))
                r.fail (path + ".steps", "must be a list of at most " + juce::String (Pattern::maxSteps) + " steps");

            for (int s = 0; steps.isArray() && s < steps.size() && s < Pattern::maxSteps; ++s)
            {
                const auto step = steps[s];
                const auto stepPath = path + ".steps[" + juce::String (s) + "]";
                Step& st = out.steps[static_cast<size_t> (s)];
                st.on = r.boolean (step, "on", stepPath, false);
                st.accent = r.boolean (step, "accent", stepPath, false);
                st.slide = r.boolean (step, "slide", stepPath, false);
                st.tie = r.boolean (step, "tie", stepPath, false);
                const auto name = r.text (step, "note", stepPath, noteName (st.note));
                const auto note = parseNoteName (name);
                if (! note || *note < Step::minNote || *note > Step::maxNote)
                    r.fail (stepPath + ".note", "'" + name + "' is not a note between " + noteName (Step::minNote) +
                                                    " and " + noteName (Step::maxNote));
                else
                    st.note = *note;
            }
        }

        return r.ok() ? juce::Result::ok() : juce::Result::fail (r.error);
    }

    void apply (const ProjectData& data, Session& session)
    {
        session.transport.playing = false;
        session.transport.bpm = data.bpm;
        session.transport.swing = data.swing;

        auto& synth = session.synth;
        synth.cutoff = data.cutoff;
        synth.resonance = data.resonance;
        synth.envMod = data.envMod;
        synth.decay = data.decay;
        synth.accent = data.accent;
        synth.drive = data.drive;
        synth.square = data.square;

        for (int i = 0; i < Session::maxPatterns; ++i)
        {
            const auto& in = data.patterns[static_cast<size_t> (i)];
            auto& pattern = session.pattern (i);
            pattern.clear();
            session.project.patternNames[static_cast<size_t> (i)] =
                in.present ? in.name : Session::defaultPatternName (i);
            if (! in.present)
                continue;
            pattern.setLength (in.length);
            for (int s = 0; s < Pattern::maxSteps; ++s)
                pattern.setStep (s, in.steps[static_cast<size_t> (s)]);
        }

        session.selected = data.selected;
        session.playing = data.selected;
    }
} // namespace

juce::String toText (const Session& session)
{
    juce::StringArray lines;
    lines.add ("{");
    lines.add ("  \"format\": " + quoted (formatId) + ",");
    lines.add ("  \"version\": " + juce::String (formatVersion) + ",");
    lines.add ("  \"tempo\": { \"bpm\": " + number (session.transport.bpm.load()) +
               ", \"swing\": " + number (session.transport.swing.load()) + " },");

    const auto& p = session.synth;
    lines.add ("  \"synth\": {");
    lines.add ("    \"cutoff\": " + number (p.cutoff.load()) + ",");
    lines.add ("    \"resonance\": " + number (p.resonance.load()) + ",");
    lines.add ("    \"envMod\": " + number (p.envMod.load()) + ",");
    lines.add ("    \"decay\": " + number (p.decay.load()) + ",");
    lines.add ("    \"accent\": " + number (p.accent.load()) + ",");
    lines.add ("    \"drive\": " + number (p.drive.load()) + ",");
    lines.add ("    \"waveform\": " + quoted (p.square.load() ? "square" : "saw"));
    lines.add ("  },");
    lines.add ("  \"selectedPattern\": " + juce::String (session.selected.load() + 1) + ",");

    // Only patterns with something in them; each step on its own line.
    std::vector<int> used;
    for (int i = 0; i < Session::maxPatterns; ++i)
        if (! session.pattern (i).isEmpty() ||
            session.project.patternNames[static_cast<size_t> (i)] != Session::defaultPatternName (i))
            used.push_back (i);

    lines.add ("  \"patterns\": [");
    for (size_t u = 0; u < used.size(); ++u)
    {
        const auto i = used[u];
        const auto& pattern = session.pattern (i);

        // Keep steps past the length if they hold notes, so shortening a pattern never loses work.
        auto lastToWrite = pattern.getLength();
        for (int s = Pattern::maxSteps - 1; s >= lastToWrite; --s)
            if (pattern.getStep (s) != Step {})
            {
                lastToWrite = s + 1;
                break;
            }

        lines.add ("    {");
        lines.add ("      \"number\": " + juce::String (i + 1) + ",");
        lines.add ("      \"name\": " + quoted (session.project.patternNames[static_cast<size_t> (i)]) + ",");
        lines.add ("      \"length\": " + juce::String (pattern.getLength()) + ",");
        lines.add ("      \"steps\": [");
        for (int s = 0; s < lastToWrite; ++s)
            lines.add ("        " + stepLine (pattern.getStep (s)) + (s + 1 < lastToWrite ? "," : ""));
        lines.add ("      ]");
        lines.add (u + 1 < used.size() ? "    }," : "    }");
    }
    lines.add ("  ]");
    lines.add ("}");
    return lines.joinIntoString ("\n") + "\n";
}

juce::Result fromText (const juce::String& text, Session& session)
{
    ProjectData data;
    if (const auto result = parse (text, data); result.failed())
        return result;
    apply (data, session);
    return juce::Result::ok();
}

juce::Result save (const Session& session, const juce::File& file)
{
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create the folder " + file.getParentDirectory().getFullPathName());

    const juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (toText (session), false, false, "\n"))
        return juce::Result::fail ("cannot write " + file.getFullPathName() + " (disk full or no permission?)");
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result load (const juce::File& file, Session& session)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("file not found: " + file.getFullPathName());
    const auto result = fromText (file.loadFileAsString(), session);
    return result.failed() ? juce::Result::fail (file.getFileName() + ": " + result.getErrorMessage()) : result;
}
} // namespace tekno::core::io::project
