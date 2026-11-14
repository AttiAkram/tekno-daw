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

    juce::String acidLine (const AcidSettings& a)
    {
        return "{ \"cutoff\": " + number (a.cutoff) + ", \"resonance\": " + number (a.resonance) +
               ", \"envMod\": " + number (a.envMod) + ", \"decay\": " + number (a.decay) +
               ", \"accent\": " + number (a.accent) + ", \"drive\": " + number (a.drive) +
               ", \"waveform\": " + quoted (a.square ? "square" : "saw") + " }";
    }

    juce::String samplePath (const juce::String& origin, const juce::File& projectFolder)
    {
        if (origin.isEmpty() || origin.startsWith (SampleLibrary::builtinPrefix))
            return origin;
        const juce::File file (origin);
        if (projectFolder != juce::File() && file.isAChildOf (projectFolder))
            return file.getRelativePathFrom (projectFolder);
        return origin;
    }

    juce::String samplerLine (const SamplerSettings& s, const juce::String& sample)
    {
        return "{ \"sample\": " + quoted (sample) + ", \"tune\": " + number (s.tune) +
               ", \"decay\": " + number (s.decay) + ", \"accent\": " + number (s.accent) + " }";
    }

    juce::String mixerLine (const MixerSettings& m)
    {
        return "{ \"volume\": " + number (m.volumeDb) + ", \"pan\": " + number (m.pan) +
               ", \"mute\": " + boolean (m.mute) + ", \"solo\": " + boolean (m.solo) + " }";
    }

    // --- reading --------------------------------------------------------------------------------

    /** Plain copy of everything a file holds, filled and validated before touching the session. */
    struct PatternData
    {
        bool present = false;
        juce::String name;
        int length = Pattern::defaultLength;
    };

    struct ProjectData
    {
        double bpm = TransportState::defaultBpm;
        float swing = TransportState::minSwing;
        float masterDb = 0.0f;
        int selected = 0;
        int selectedChannel = 0;
        std::vector<ChannelData> channels; // lanes included
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

        float unit (const juce::var& obj, const char* key, const juce::String& path, float fallback)
        {
            return static_cast<float> (number (obj, key, path, fallback, 0.0, 1.0));
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

        AcidSettings acid (const juce::var& obj, const juce::String& path)
        {
            AcidSettings a;
            a.cutoff = unit (obj, "cutoff", path, a.cutoff);
            a.resonance = unit (obj, "resonance", path, a.resonance);
            a.envMod = unit (obj, "envMod", path, a.envMod);
            a.decay = unit (obj, "decay", path, a.decay);
            a.accent = unit (obj, "accent", path, a.accent);
            a.drive = unit (obj, "drive", path, a.drive);
            const auto waveform = text (obj, "waveform", path, "saw");
            if (waveform != "saw" && waveform != "square")
                fail (path + ".waveform", "'" + waveform + "' must be \"saw\" or \"square\"");
            a.square = waveform == "square";
            return a;
        }

        /** A list of steps into `out`. Missing notes default to `defaultNote`. */
        void steps (const juce::var& list, const juce::String& path, std::array<Step, Pattern::maxSteps>& out,
                    int defaultNote)
        {
            if (! list.isVoid() && (! list.isArray() || list.size() > Pattern::maxSteps))
                fail (path, "must be a list of at most " + juce::String (Pattern::maxSteps) + " steps");

            for (int s = 0; list.isArray() && s < list.size() && s < Pattern::maxSteps; ++s)
            {
                const auto step = list[s];
                const auto stepPath = path + "[" + juce::String (s) + "]";
                Step& st = out[static_cast<size_t> (s)];
                st.on = boolean (step, "on", stepPath, false);
                st.accent = boolean (step, "accent", stepPath, false);
                st.slide = boolean (step, "slide", stepPath, false);
                st.tie = boolean (step, "tie", stepPath, false);
                const auto name = text (step, "note", stepPath, noteName (defaultNote));
                const auto note = parseNoteName (name);
                if (! note || *note < Step::minNote || *note > Step::maxNote)
                    fail (stepPath + ".note", "'" + name + "' is not a note between " + noteName (Step::minNote) +
                                                  " and " + noteName (Step::maxNote));
                else
                    st.note = *note;
            }
        }
    };

    /** The pattern header (number, name, length); returns the index, or -1 on error. */
    int readPatternHeader (Reader& r, const juce::var& p, const juce::String& path, ProjectData& data)
    {
        const auto numberInFile = static_cast<int> (r.number (p, "number", path, 0, 1, Session::maxPatterns));
        if (! r.ok())
            return -1;
        if (numberInFile == 0)
        {
            r.fail (path + ".number", "is missing");
            return -1;
        }
        auto& out = data.patterns[static_cast<size_t> (numberInFile - 1)];
        if (out.present)
        {
            r.fail (path + ".number", "pattern " + juce::String (numberInFile) + " appears twice");
            return -1;
        }
        out.present = true;
        out.name = r.text (p, "name", path, Session::defaultPatternName (numberInFile - 1));
        out.length = static_cast<int> (r.number (p, "length", path, Pattern::defaultLength, 1, Pattern::maxSteps));
        return numberInFile - 1;
    }

    /** Version 1 (M2): one synth and one line of steps per pattern. It becomes channel 1, acid. */
    void parseVersion1 (Reader& r, const juce::var& root, ProjectData& data)
    {
        ChannelData acid;
        acid.kind = InstrumentKind::acid;
        acid.name = "acid";
        acid.acid = r.acid (root["synth"], "synth");

        const auto patterns = root["patterns"];
        if (! patterns.isVoid() && ! patterns.isArray())
            r.fail ("patterns", "must be a list");
        for (int i = 0; patterns.isArray() && i < patterns.size() && r.ok(); ++i)
        {
            const auto p = patterns[i];
            const auto path = "patterns[" + juce::String (i) + "]";
            const auto index = readPatternHeader (r, p, path, data);
            if (index >= 0)
                r.steps (p["steps"], path + ".steps", acid.lanes[static_cast<size_t> (index)], Step {}.note);
        }
        data.channels.push_back (acid);
    }

    void parseVersion2 (Reader& r, const juce::var& root, ProjectData& data, const juce::File& folder)
    {
        data.masterDb = static_cast<float> (
            r.number (root["master"], "volume", "master", 0.0, MixerSettings::minVolumeDb, MixerSettings::maxVolumeDb));

        const auto channels = root["channels"];
        if (! channels.isArray() || channels.size() == 0 || channels.size() > Session::maxChannels)
        {
            r.fail ("channels", "must be a list of 1 to " + juce::String (Session::maxChannels) + " channels");
            return;
        }
        for (int i = 0; i < channels.size() && r.ok(); ++i)
        {
            const auto c = channels[i];
            const auto path = "channels[" + juce::String (i) + "]";
            ChannelData out;
            const auto instrument = r.text (c, "instrument", path, "acid");
            if (instrument != "acid" && instrument != "sampler")
                r.fail (path + ".instrument", "'" + instrument + "' must be \"acid\" or \"sampler\"");
            out.kind = instrument == "sampler" ? InstrumentKind::sampler : InstrumentKind::acid;
            out.name = r.text (c, "name", path, instrument).trim().substring (0, 40);
            out.acid = r.acid (c["acid"], path + ".acid");

            const auto s = c["sampler"];
            const auto samplerPath = path + ".sampler";
            out.sampler.tune = static_cast<float> (
                r.number (s, "tune", samplerPath, 0.0, SamplerSettings::minTune, SamplerSettings::maxTune));
            out.sampler.decay = r.unit (s, "decay", samplerPath, out.sampler.decay);
            out.sampler.accent = r.unit (s, "accent", samplerPath, out.sampler.accent);
            const auto sample = r.text (s, "sample", samplerPath, {});
            out.sample = sample.isEmpty() || sample.startsWith (SampleLibrary::builtinPrefix) ||
                                 juce::File::isAbsolutePath (sample)
                             ? sample
                             : (folder != juce::File() ? folder : juce::File::getCurrentWorkingDirectory())
                                   .getChildFile (sample)
                                   .getFullPathName();

            const auto m = c["mixer"];
            const auto mixerPath = path + ".mixer";
            out.mixer.volumeDb = static_cast<float> (
                r.number (m, "volume", mixerPath, 0.0, MixerSettings::minVolumeDb, MixerSettings::maxVolumeDb));
            out.mixer.pan = static_cast<float> (r.number (m, "pan", mixerPath, 0.0, -1.0, 1.0));
            out.mixer.mute = r.boolean (m, "mute", mixerPath, false);
            out.mixer.solo = r.boolean (m, "solo", mixerPath, false);
            data.channels.push_back (out);
        }

        data.selectedChannel = static_cast<int> (r.number (root, "selectedChannel", "project", 1, 1,
                                                           static_cast<double> (data.channels.size()))) -
                               1;

        const auto patterns = root["patterns"];
        if (! patterns.isVoid() && ! patterns.isArray())
            r.fail ("patterns", "must be a list");
        for (int i = 0; patterns.isArray() && i < patterns.size() && r.ok(); ++i)
        {
            const auto p = patterns[i];
            const auto path = "patterns[" + juce::String (i) + "]";
            const auto index = readPatternHeader (r, p, path, data);
            if (index < 0)
                break;

            const auto lanes = p["lanes"];
            if (! lanes.isVoid() && ! lanes.isArray())
                r.fail (path + ".lanes", "must be a list");
            std::vector<bool> seen (data.channels.size(), false);
            for (int l = 0; lanes.isArray() && l < lanes.size() && r.ok(); ++l)
            {
                const auto lane = lanes[l];
                const auto lanePath = path + ".lanes[" + juce::String (l) + "]";
                const auto channel = static_cast<int> (
                    r.number (lane, "channel", lanePath, 0, 1, static_cast<double> (data.channels.size())));
                if (! r.ok())
                    break;
                if (channel == 0)
                {
                    r.fail (lanePath + ".channel", "is missing");
                    break;
                }
                auto& owner = data.channels[static_cast<size_t> (channel - 1)];
                if (seen[static_cast<size_t> (channel - 1)])
                {
                    r.fail (lanePath + ".channel", "channel " + juce::String (channel) + " appears twice");
                    break;
                }
                seen[static_cast<size_t> (channel - 1)] = true;
                const auto defaultNote = owner.kind == InstrumentKind::sampler ? Step::samplerRootNote : Step {}.note;
                r.steps (lane["steps"], lanePath + ".steps", owner.lanes[static_cast<size_t> (index)], defaultNote);
            }
        }
    }

    juce::Result parse (const juce::String& text, ProjectData& data, const juce::File& folder)
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

        const auto version = static_cast<int> (r.number (root, "version", "project", 1, 0, 1'000'000));
        if (version > formatVersion)
            return juce::Result::fail ("this project was saved by a newer tekno-daw (format version " +
                                       juce::String (version) + ", this one reads up to " +
                                       juce::String (formatVersion) + ")");

        const auto tempo = root["tempo"];
        data.bpm = r.number (tempo, "bpm", "tempo", data.bpm, TransportState::minBpm, TransportState::maxBpm);
        data.swing = static_cast<float> (
            r.number (tempo, "swing", "tempo", data.swing, TransportState::minSwing, TransportState::maxSwing));
        data.selected =
            static_cast<int> (r.number (root, "selectedPattern", "project", 1, 1, Session::maxPatterns)) - 1;

        if (version <= 1)
            parseVersion1 (r, root, data);
        else
            parseVersion2 (r, root, data, folder);

        return r.ok() ? juce::Result::ok() : juce::Result::fail (r.error);
    }

    void apply (const ProjectData& data, Session& session)
    {
        session.clearProject();
        session.transport.bpm = data.bpm;
        session.transport.swing = data.swing;
        session.master.volumeDb = data.masterDb;

        juce::StringArray warnings;
        for (size_t c = 0; c < data.channels.size(); ++c)
        {
            juce::String warning;
            session.insertChannel (static_cast<int> (c), data.channels[c], &warning);
            if (warning.isNotEmpty())
                warnings.add ("channel " + juce::String (c + 1) + " (" + data.channels[c].name + "): " + warning);
        }

        for (int i = 0; i < Session::maxPatterns; ++i)
        {
            const auto& in = data.patterns[static_cast<size_t> (i)];
            if (! in.present)
                continue;
            session.project.patternNames[static_cast<size_t> (i)] = in.name;
            session.pattern (i).setLength (in.length);
        }

        session.selected = data.selected;
        session.playing = data.selected;
        session.selectedChannel = std::clamp (data.selectedChannel, 0, session.channelCount() - 1);
        session.project.warnings = warnings;
    }
} // namespace

juce::String toText (const Session& session, const juce::File& projectFolder)
{
    juce::StringArray lines;
    lines.add ("{");
    lines.add ("  \"format\": " + quoted (formatId) + ",");
    lines.add ("  \"version\": " + juce::String (formatVersion) + ",");
    lines.add ("  \"tempo\": { \"bpm\": " + number (session.transport.bpm.load()) +
               ", \"swing\": " + number (session.transport.swing.load()) + " },");
    lines.add ("  \"master\": { \"volume\": " + number (session.master.volumeDb.load()) + " },");
    lines.add ("  \"selectedPattern\": " + juce::String (session.selected.load() + 1) + ",");
    lines.add ("  \"selectedChannel\": " + juce::String (session.selectedChannel.load() + 1) + ",");

    const auto channels = session.channelCount();
    lines.add ("  \"channels\": [");
    for (int c = 0; c < channels; ++c)
    {
        const auto& channel = session.channel (c);
        const auto slot = static_cast<size_t> (c);
        const auto kind = channel.kind.load();
        lines.add ("    {");
        lines.add ("      \"name\": " + quoted (session.project.channelNames[slot]) + ",");
        lines.add ("      \"instrument\": " + quoted (instrumentName (kind)) + ",");
        if (kind == InstrumentKind::acid)
            lines.add ("      \"acid\": " + acidLine (channel.acid.load()) + ",");
        else
            lines.add (
                "      \"sampler\": " +
                samplerLine (channel.sampler.load(), samplePath (session.project.channelSamples[slot], projectFolder)) +
                ",");
        lines.add ("      \"mixer\": " + mixerLine (channel.mixer.load()));
        lines.add (c + 1 < channels ? "    }," : "    }");
    }
    lines.add ("  ],");

    // Only patterns with something in them, only lanes with something in them; one step per line.
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

        std::vector<int> lanes;
        for (int c = 0; c < channels; ++c)
            if (! pattern.lane (c).isBlank())
                lanes.push_back (c);

        lines.add ("    {");
        lines.add ("      \"number\": " + juce::String (i + 1) + ",");
        lines.add ("      \"name\": " + quoted (session.project.patternNames[static_cast<size_t> (i)]) + ",");
        lines.add ("      \"length\": " + juce::String (pattern.getLength()) + ",");
        lines.add ("      \"lanes\": [");
        for (size_t l = 0; l < lanes.size(); ++l)
        {
            const auto& lane = pattern.lane (lanes[l]);

            // Keep steps past the length if they hold anything, so shortening never loses work.
            auto lastToWrite = pattern.getLength();
            for (int s = Pattern::maxSteps - 1; s >= lastToWrite; --s)
                if (lane.getStep (s) != Step {})
                {
                    lastToWrite = s + 1;
                    break;
                }

            lines.add ("        {");
            lines.add ("          \"channel\": " + juce::String (lanes[l] + 1) + ",");
            lines.add ("          \"steps\": [");
            for (int s = 0; s < lastToWrite; ++s)
                lines.add ("            " + stepLine (lane.getStep (s)) + (s + 1 < lastToWrite ? "," : ""));
            lines.add ("          ]");
            lines.add (l + 1 < lanes.size() ? "        }," : "        }");
        }
        lines.add ("      ]");
        lines.add (u + 1 < used.size() ? "    }," : "    }");
    }
    lines.add ("  ]");
    lines.add ("}");
    return lines.joinIntoString ("\n") + "\n";
}

juce::Result fromText (const juce::String& text, Session& session, const juce::File& projectFolder)
{
    ProjectData data;
    if (const auto result = parse (text, data, projectFolder); result.failed())
        return result;
    apply (data, session);
    return juce::Result::ok();
}

juce::Result save (const Session& session, const juce::File& file)
{
    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create the folder " + file.getParentDirectory().getFullPathName());

    const juce::TemporaryFile temp (file);
    if (! temp.getFile().replaceWithText (toText (session, file.getParentDirectory()), false, false, "\n"))
        return juce::Result::fail ("cannot write " + file.getFullPathName() + " (disk full or no permission?)");
    if (! temp.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result load (const juce::File& file, Session& session)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("file not found: " + file.getFullPathName());
    const auto result = fromText (file.loadFileAsString(), session, file.getParentDirectory());
    return result.failed() ? juce::Result::fail (file.getFileName() + ": " + result.getErrorMessage()) : result;
}
} // namespace tekno::core::io::project
