// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "engine/Mixer.h"

/** Every parameter a host sees: the master level, how much bleed and how much dynamics, sticks or
    brushes, the hi-hat pedal, and a mixer strip for each of up to 16 mics (level, pan, mute, solo,
    polarity, output). A strip's mic is whichever the kit has in that place: its name is in the
    plugin's window; to a host it's "Mic 1".."Mic 16". */
namespace drums::params
{
    inline const juce::String master = "master", bleed = "bleed", dynamics = "dynamics", brushes = "brushes", hatPedal = "hatPedal";

    inline juce::String micLevel (int i)    { return "mic" + juce::String (i + 1) + "Level"; }
    inline juce::String micPan (int i)      { return "mic" + juce::String (i + 1) + "Pan"; }
    inline juce::String micMute (int i)     { return "mic" + juce::String (i + 1) + "Mute"; }
    inline juce::String micSolo (int i)     { return "mic" + juce::String (i + 1) + "Solo"; }
    inline juce::String micInvert (int i)   { return "mic" + juce::String (i + 1) + "Invert"; }
    inline juce::String micOutput (int i)   { return "mic" + juce::String (i + 1) + "Out"; }

    inline juce::StringArray outputNames()
    {
        juce::StringArray names { "Main" };

        for (int o = 2; o <= Mixer::maxOutputs; ++o)
            names.add ("Out " + juce::String (o));

        return names;
    }

    inline juce::String dbText (float v, int)     { return v <= -59.95f ? juce::String ("-inf dB") : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB"; }
    inline juce::String panText (float v, int)    { return std::abs (v) < 0.5f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v))) + (v < 0 ? " L" : " R"); }
    inline juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }

    inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        auto decibels = [] (const juce::String& id, const juce::String& name, float min, float max, float def)
        {
            juce::NormalisableRange<float> range (min, max, 0.1f);
            range.setSkewForCentre (-12.0f);
            return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, range, def,
                                                                juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText));
        };

        l.add (decibels (master, "Master", -60.0f, 12.0f, 0.0f));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (bleed, 1), "Bleed", juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (dynamics, 1), "Dynamics", juce::NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
                                                            juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (percentText)));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (brushes, 1), "Brushes", false));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (hatPedal, 1), "Hi-hat pedal opens the hi-hat", true));

        for (int i = 0; i < Mixer::maxStrips; ++i)
        {
            const auto n = "Mic " + juce::String (i + 1);
            l.add (decibels (micLevel (i), n + " level", -60.0f, 12.0f, 0.0f));
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (micPan (i), 1), n + " pan", juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                                                juce::AudioParameterFloatAttributes().withStringFromValueFunction (panText)));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (micMute (i), 1), n + " mute", false));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (micSolo (i), 1), n + " solo", false));
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID (micInvert (i), 1), n + " polarity", false));
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (micOutput (i), 1), n + " output", outputNames(), 0));
        }

        return l;
    }
}
