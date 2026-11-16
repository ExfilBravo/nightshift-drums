// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "engine/Catalog.h"

/** A small kit made for the tests, written as a DrumGizmo zip the way the real ones are: four mics
    (two overheads, a snare top, a kick inside), a snare with five hits, a kick with three, and a
    hi-hat closed and open (in a group), the open one long enough to stream. Every sample is a
    function of (instrument, hit, mic, frame), so a test can check what the engine plays exactly. */
namespace drums::testkit
{
    constexpr double rate = 48000.0;

    struct Instrument
    {
        const char* name;
        const char* group;
        std::vector<double> powers;     // DrumGizmo's measure of each hit, softest first
        double seconds;
        std::vector<bool> main;         // per mic
    };

    const std::vector<juce::String>& micNames();
    const std::vector<Instrument>& instruments();

    /** The recorded value of a sample. */
    float sample (int instrument, int hit, int mic, int frame);

    /** Writes the zip (TestKit/TestKit.xml inside). */
    bool writeZip (const juce::File& zip);

    /** How it's played: snare 38, kick 36, hi-hat closed 42 and open 46 (its edge). */
    KitDefinition definition();
}
