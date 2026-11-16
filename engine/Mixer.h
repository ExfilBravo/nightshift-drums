// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>

namespace drums
{

/** The kit's own mixer: a strip per mic (level, pan, mute, solo, polarity) into the main stereo
    out or one of the extra stereo outs, so each mic can have a channel of its own in the host's
    mixer. Pan is constant-power. Gains glide over a block when they change, so moving a fader
    never clicks. Meters each mic and each output. */
class Mixer
{
public:
    static constexpr int maxStrips = 16, maxOutputs = 9;      // the main out and Out 2..9

    struct Strip
    {
        float level = 1.0f;         // gain
        float pan = 0.0f;           // -1 .. 1
        bool mute = false, solo = false, invert = false;
        int output = 0;             // 0 the main out, 1..8 Out 2..9
    };

    void prepare (int maxBlock);

    /** Mixes numSamples of mics[0..micCount) into `outputs` (2 channels an output, those the host
        has turned on: `outputCount` of them; a mic sent to one that's off goes to the main out).
        `master` is the main out's level. Adds nothing for micCount 0; always writes every output. */
    void process (const float* const* mics, int micCount, const std::array<Strip, maxStrips>& strips, float master,
                  float* const* outputs, int outputCount, int numSamples);

    /** Peaks since the last call, as gain: a mic's (after its fader), or an output's (left/right). */
    float takeMicPeak (int strip)               { return micPeaks[(size_t) strip].exchange (0.0f); }
    float takeOutputPeak (int output, int side) { return outPeaks[(size_t) (output * 2 + side)].exchange (0.0f); }

private:
    std::array<float, maxStrips> currentLeft {}, currentRight {};
    std::array<int, maxStrips> currentOutput {};
    std::array<bool, maxStrips> primed {};
    float currentMaster = 1.0f;
    bool masterPrimed = false;
    std::array<std::atomic<float>, maxStrips> micPeaks {};
    std::array<std::atomic<float>, maxOutputs * 2> outPeaks {};
    juce::HeapBlock<float> ramp;
    int rampSize = 0;
};

} // namespace drums
