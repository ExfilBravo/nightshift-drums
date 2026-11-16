// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. Copied as it is from NightShift's src/ai for Nightshift Drums.

#pragma once

#include <JuceHeader.h>

/** The offline instrument agents (the drummer first; bass, guitar, keys and synth to follow):
    each a specialist that knows its genres' theory and habits, writes many candidate parts and
    keeps the best by its genre's rules. No internet, no model: everything here runs on the
    machine, at once. */
namespace nightshift::agents
{
    /** A typed request, understood offline: the style, the feel and the song form it asks for. */
    struct Request
    {
        juce::String style;                 // see styles(); empty until one is named
        int tempo = 0;                      // asked for, else 0
        int meterBeats = 0, meterUnit = 0;  // asked for, else 0
        bool halfTime = false, doubleTime = false;
        bool brushes = false, blasts = false, ghostNotes = false, gallops = false;
        float energy = 0.5f;                // 0 gentle .. 1 all out
        float busy = 0.5f;                  // 0 sparse .. 1 busy, all over
        float busyHats = 0.0f, busyKick = 0.0f;   // more (or less) of just these, -1..1
        float swing = -1.0f;                // asked for, 0..1; -1 the style's own
        juce::StringArray sections;         // section kinds asked for, in order: intro, verse, prechorus, chorus,
                                            // breakdown, bridge, drop, hook, outro
        bool addToForm = false;             // "metalcore with a breakdown": the style's usual song with these
                                            // added, not these alone (the agent resolves it; not saved)
        int bars = 0;                       // bars a section, asked for, else 0

        // For the bassist (and the agents after it): the key and how it's played
        int keyRoot = -1;                   // the key's pitch class asked for (C = 0), else -1
        juce::String mode;                  // major, minor, dorian, phrygian, mixolydian; empty: the style's own
        int lowest = 0;                     // a tuning's lowest note asked for ("drop C": 24, "drop A": 21), else 0
        bool slides = false, walking = false, octaves = false, slap = false, pedal = false, lockKick = false;
        float legato = 0.0f;                // -1 short and choppy .. 1 long and held
        juce::StringArray understood;       // what it picked out, in words, for the reply

        juce::var toVar() const;
        static Request fromVar (const juce::var&);

        /** Reads `words`. With `previous` (this channel's last request), words that don't name a
            style change that request ("busier", "half-time", "add a breakdown") instead of
            starting over. */
        static Request parse (const juce::String& words, const Request* previous = nullptr);
    };

    /** A style an agent knows: its name, the words that mean it, its family (metal, trap, rock,
        lofi) and the tempo it lives at. */
    struct Style
    {
        const char* id;
        const char* name;
        const char* family;
        int slowest, usual, fastest;
        juce::StringArray words;
    };

    const std::vector<Style>& styles();
    const Style* findStyle (const juce::String& id);
}
