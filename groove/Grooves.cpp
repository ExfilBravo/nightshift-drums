// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Grooves.h"
#include "OfflineDrummer.h"
#include "Request.h"

namespace drums::grooves
{

int noteFor (const juce::String& voice)
{
    static const std::map<juce::String, int> keys {
        { "kick", 36 }, { "snare", 38 }, { "snare_rim", 40 }, { "sidestick", 37 }, { "clap", 39 }, { "hat_closed", 42 },
        { "hat_pedal", 44 }, { "hat_open", 46 }, { "tom_high", 50 }, { "tom_mid", 47 }, { "tom_floor", 43 }, { "ride", 51 },
        { "ride_bell", 53 }, { "crash", 49 }, { "crash_2", 57 }, { "china", 52 }, { "splash", 55 }, { "tambourine", 54 },
        { "cowbell", 56 }, { "shaker", 70 }, { "stick_click", 31 } };

    auto it = keys.find (voice);
    return it != keys.end() ? it->second : -1;
}

Written write (const juce::String& prompt, double bpm, int meterBeats, int meterUnit, juce::int64 seed)
{
    nightshift::Song song;
    song.bpm = bpm > 0.0 ? bpm : 120.0;
    song.meterBeats = meterBeats > 0 ? meterBeats : 4;
    song.meterUnit = meterUnit > 0 ? meterUnit : 4;

    auto result = nightshift::drummer::offline::write (prompt, song, nullptr, {}, false, {}, false, seed);
    return { std::move (result.groove), result.reply };
}

juce::StringArray styleNames()
{
    juce::StringArray names;

    for (auto& s : nightshift::agents::styles())
        names.add (s.name);

    return names;
}

double tempoOf (const Groove& g, double songBpm)
{
    return g.tempo > 0 ? (double) g.tempo : (songBpm > 0.0 ? songBpm : 120.0);
}

namespace
{
    /** Roughly normal, -1.5..1.5, from a seeded generator: the same groove always plays the same. */
    float wobble (juce::Random& r)
    {
        return r.nextFloat() + r.nextFloat() + r.nextFloat() - 1.5f;
    }
}

juce::MidiMessageSequence toMidi (const Groove& g, double bpm, int sectionIndex, double& lengthQuarters)
{
    juce::MidiMessageSequence seq;
    const int beats = g.meterBeats > 0 ? g.meterBeats : 4, unit = g.meterUnit > 0 ? g.meterUnit : 4;
    const double barTicks = (double) beats * 4.0 / unit * ticksPerQuarter;
    const double ticksPerMs = tempoOf (g, bpm) / 60.0 * ticksPerQuarter / 1000.0;

    // The sections to play, in order
    std::vector<int> order;

    if (juce::isPositiveAndBelow (sectionIndex, (int) g.sections.size()))
        order.push_back (sectionIndex);
    else
    {
        for (auto& name : g.arrangement)
            for (size_t s = 0; s < g.sections.size(); ++s)
                if (g.sections[s].name == name)
                {
                    order.push_back ((int) s);
                    break;
                }

        if (order.empty())
            for (size_t s = 0; s < g.sections.size(); ++s)
                order.push_back ((int) s);
    }

    double barStart = 0.0;
    int barNumber = 0;

    for (int s : order)
    {
        const auto& section = g.sections[(size_t) s];

        for (const auto& bar : section.bars)
        {
            const double stepTicks = barTicks / std::max (1, bar.steps);

            for (const auto& lane : bar.lanes)
            {
                const int note = noteFor (lane.voice);

                if (note < 0)
                    continue;

                const float push = g.pushMs.count (lane.voice) > 0 ? g.pushMs.at (lane.voice) : 0.0f;

                for (int i = 0; i < lane.hits.length(); ++i)
                {
                    const auto hit = lane.hits[i];

                    if (hit == '.' || hit == '-' || hit == '0')
                        continue;

                    double pos = i * stepTicks;

                    // Swing: the off-beat 8ths (or 16ths) late, up to a triplet
                    const double inBeat = std::fmod (pos / ticksPerQuarter, 1.0);

                    if (! g.swingSixteenths && std::abs (inBeat - 0.5) < 1.0e-6)
                        pos += g.swing * ticksPerQuarter / 6.0;
                    else if (g.swingSixteenths && (std::abs (inBeat - 0.25) < 1.0e-6 || std::abs (inBeat - 0.75) < 1.0e-6))
                        pos += g.swing * ticksPerQuarter / 12.0;

                    // The drum's push, then a little wander: the same every time it's played
                    juce::Random random ((juce::int64) (barNumber * 10007 + i * 101) ^ lane.voice.hashCode64());
                    pos += (push + wobble (random) * g.looseMs * 0.6f) * ticksPerMs;

                    int velocity = nightshift::drummer::velocityFor (hit);
                    velocity = juce::jlimit (1, 127, velocity + juce::roundToInt (wobble (random) * 3.0f));
                    const double at = std::max (0.0, barStart + pos);
                    const double length = ticksPerQuarter / 8.0;

                    auto add = [&] (double when, int vel)
                    {
                        seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) juce::jlimit (1, 127, vel)), when);
                        seq.addEvent (juce::MidiMessage::noteOff (10, note), when + length);
                    };

                    // A flam's grace note, or a drag's two, just before
                    if (hit == 'f' || hit == 'F')
                        add (std::max (0.0, at - 22.0 * ticksPerMs), velocity * 45 / 100);

                    if (hit == 'r' || hit == 'R')
                        for (int grace : { 2, 1 })
                            add (std::max (0.0, at - grace * 45.0 * ticksPerMs), velocity * 40 / 100);

                    add (at, velocity);
                }
            }

            barStart += barTicks;
            ++barNumber;
        }
    }

    seq.sort();
    seq.updateMatchedPairs();
    lengthQuarters = barStart / ticksPerQuarter;
    return seq;
}

bool saveMidiFile (const Groove& g, double bpm, int section, const juce::File& file)
{
    double length = 0.0;
    auto notes = toMidi (g, bpm, section, length);

    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, g.title.isNotEmpty() ? g.title : juce::String ("Nightshift Drums groove")), 0.0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempoOf (g, bpm))), 0.0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (g.meterBeats > 0 ? g.meterBeats : 4, g.meterUnit > 0 ? g.meterUnit : 4), 0.0);
    track.addSequence (notes, 0.0);
    track.addEvent (juce::MidiMessage::endOfTrack(), length * ticksPerQuarter);
    track.updateMatchedPairs();

    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ticksPerQuarter);
    midi.addTrack (track);

    file.deleteFile();
    juce::FileOutputStream out (file);
    return out.openedOk() && midi.writeTo (out);
}

} // namespace drums::grooves
