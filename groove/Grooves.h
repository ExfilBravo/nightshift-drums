// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "Drummer.h"

/** Grooves for Nightshift Drums, written on the spot by NightShift's offline AI drummer (its
    specialists for 19 styles, copied here as they are), played by the kit, and dragged into the
    host as MIDI. Nothing goes to the internet. */
namespace drums::grooves
{
    using nightshift::drummer::Groove;

    /** The note a drummer's voice plays on Nightshift Drums: General MIDI, as NightShift sends a drum plugin. */
    int noteFor (const juce::String& voice);

    struct Written
    {
        Groove groove;
        juce::String reply;         // what it played, in a drummer's words
    };

    /** A groove for `prompt` in words ("funk, busy hats", "metalcore with a breakdown", "jazz brushes at 140"),
        at the song's tempo and time signature unless the words ask for others. Another `seed`, another take. */
    Written write (const juce::String& prompt, double bpm, int meterBeats, int meterUnit, juce::int64 seed);

    /** The styles it knows, by name, and an example prompt for each. */
    juce::StringArray styleNames();

    /** Its notes from its start, in quarter notes (960 ticks each): the whole arrangement, or one section,
        with its feel (swing, each drum's push, a little looseness). `lengthQuarters` gets its length. */
    juce::MidiMessageSequence toMidi (const Groove&, double bpm, int section, double& lengthQuarters);

    static constexpr int ticksPerQuarter = 960;

    /** A standard MIDI file of it, to drag into a host. */
    bool saveMidiFile (const Groove&, double bpm, int section, const juce::File&);

    /** The tempo it's at: the one it asked for, else the song's. */
    double tempoOf (const Groove&, double songBpm);
}
