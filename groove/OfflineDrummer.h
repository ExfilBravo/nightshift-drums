// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. Copied as it is from NightShift's src/ai for Nightshift Drums.

#pragma once

#include <JuceHeader.h>
#include "Drummer.h"
#include "Request.h"

/** The offline drummer: a specialist per style that knows its genre the way a session drummer
    who plays nothing else would - metalcore's half-time breakdowns locked to the chug, china on
    the hits, double kick under the verse and tom runs into the drop; trap's half-time clap, 808
    kick motifs and hi-hat rolls in 16th triplets and 32nds; drill's triplet hats and off-beat
    snares; boom bap's swung 16ths and lazy snare; rock's 8ths and fills; pop-punk's skate beat;
    lofi's late, swung, ghosted pocket; jazz's spang-a-lang ride, feathered kick and comping;
    neo-soul's Dilla feel. For each section it writes many candidate grooves and keeps the one
    that best fits its style's rules, then plays it as a drummer would through a song: phrases of
    four bars, a variation on the fourth, a fill into the next section, a crash where it lands.

    Its answer is a drummer::Groove, so everything after (the patterns, the kit, the feel
    controls, Arrange) is the AI Drummer's as it always was. No internet. */
namespace nightshift::drummer::offline
{
    struct Result
    {
        Groove groove;
        juce::String reply;             // what it wrote, in a drummer's words
        agents::Request request;        // what it understood (kept, so a follow-up changes it)
    };

    /** Writes a part for `prompt` in the song's (or the asked-for) time signature. `previous` is
        this channel's last request (a follow-up changes it rather than starting over); with
        `mayPickKit` it chooses the kit for the style from `kitNames`, else it keeps `currentKit`.
        `songIsBlank`: nothing else in the song yet, so it may set the tempo for the style.
        A different `seed` gives another take. */
    Result write (const juce::String& prompt, const Song&, const agents::Request* previous,
                  const juce::StringArray& kitNames, bool mayPickKit, const juce::String& currentKit,
                  bool songIsBlank, juce::int64 seed);

    /** The conversation's turn for it: the prompt, the reply, the groove (as the AI Drummer reads
        it) and what it understood (in `answer`, for the next request to build on). */
    DrummerTurn turnFor (const juce::String& prompt, const Result&);

    /** What a turn's request was, when the offline drummer wrote it. */
    std::optional<agents::Request> requestOf (const DrummerTurn&);

    /** A groove as the AI Drummer's JSON. */
    juce::var toJson (const Groove&);
}
