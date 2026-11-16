// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>

/** What NightShift's offline drummer (OfflineDrummer.cpp, Request.cpp: copied as they are from
    NightShift's src/ai) needs from NightShift around it: the song's tempo and time signature,
    and the groove it writes. NightShift's own Drummer.h and Song.h hold far more; this is only
    their part the drummer reads and writes. */
namespace nightshift
{
    struct Song
    {
        int meterBeats = 4, meterUnit = 4;
        double bpm = 120.0;
    };

    struct DrummerTurn
    {
        juce::String prompt, reply;
        juce::String groove;            // the groove as written (JSON)
        juce::String answer;            // what the offline drummer understood (JSON)
    };
}

namespace nightshift::drummer
{
    struct Lane
    {
        juce::String voice;             // "kick", "snare", "hat_closed"...
        juce::String hits;              // a character per step: '.' rest, '1'-'9' how hard, 'g' ghost, 'f' flam, 'r' drag
    };

    struct Bar
    {
        int steps = 16;
        std::vector<Lane> lanes;
    };

    struct Section
    {
        juce::String name;
        std::vector<Bar> bars;
    };

    struct Groove
    {
        juce::String title, message;
        juce::String kit;
        int tempo = 0;
        int meterBeats = 0, meterUnit = 0;
        float swing = 0.0f;
        bool swingSixteenths = false;
        float looseMs = 0.0f;
        std::map<juce::String, float> pushMs;
        std::vector<Section> sections;
        juce::StringArray arrangement;
    };

    /** Velocity for a hit character, before dynamics: 1 -> 20, 9 -> 127, g -> 28 (NightShift's). */
    inline int velocityFor (juce::juce_wchar c)
    {
        if (c >= '1' && c <= '9')
            return 20 + (int) (c - '1') * 107 / 8;

        if (c == 'g' || c == 'G')
            return 28;

        return 96;
    }
}
