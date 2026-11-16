// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Mixer.h"

namespace drums
{

void Mixer::prepare (int maxBlock)
{
    ramp.allocate ((size_t) std::max (1, maxBlock), true);
    rampSize = std::max (1, maxBlock);
    primed.fill (false);
    masterPrimed = false;

    for (auto& p : micPeaks)
        p.store (0.0f);

    for (auto& p : outPeaks)
        p.store (0.0f);
}

namespace
{
    /** dest += src * (from .. to, gliding across the block). */
    void addGliding (float* dest, const float* src, float from, float to, int n)
    {
        if (from == to)
        {
            if (to != 0.0f)
                juce::FloatVectorOperations::addWithMultiply (dest, src, to, n);

            return;
        }

        const float step = (to - from) / (float) n;
        float g = from;

        for (int i = 0; i < n; ++i)
        {
            g += step;
            dest[i] += src[i] * g;
        }
    }

    void storeMax (std::atomic<float>& peak, float value)
    {
        auto previous = peak.load();

        while (value > previous && ! peak.compare_exchange_weak (previous, value)) {}
    }
}

void Mixer::process (const float* const* mics, int micCount, const std::array<Strip, maxStrips>& strips, float master,
                     float* const* outputs, int outputCount, int numSamples)
{
    outputCount = juce::jlimit (1, maxOutputs, outputCount);
    micCount = std::min (micCount, maxStrips);

    for (int c = 0; c < outputCount * 2; ++c)
        juce::FloatVectorOperations::clear (outputs[c], numSamples);

    if (numSamples <= 0)
        return;

    bool anySolo = false;

    for (int m = 0; m < micCount; ++m)
        anySolo = anySolo || strips[(size_t) m].solo;

    for (int m = 0; m < micCount; ++m)
    {
        const auto& s = strips[(size_t) m];
        const bool audible = ! s.mute && (! anySolo || s.solo);
        const float sign = s.invert ? -1.0f : 1.0f;
        const float angle = (juce::jlimit (-1.0f, 1.0f, s.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float left = audible ? s.level * std::cos (angle) * sign : 0.0f;
        const float right = audible ? s.level * std::sin (angle) * sign : 0.0f;
        const int out = juce::isPositiveAndBelow (s.output, outputCount) ? s.output : 0;
        auto i = (size_t) m;

        if (! primed[i] || out != currentOutput[i])
        {
            currentLeft[i] = left;
            currentRight[i] = right;
            currentOutput[i] = out;
            primed[i] = true;
        }

        addGliding (outputs[out * 2], mics[m], currentLeft[i], left, numSamples);
        addGliding (outputs[out * 2 + 1], mics[m], currentRight[i], right, numSamples);
        currentLeft[i] = left;
        currentRight[i] = right;

        if (audible)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (mics[m], numSamples);
            storeMax (micPeaks[i], std::max (range.getEnd(), -range.getStart()) * s.level);
        }
    }

    // The master fader, on the main out
    if (! masterPrimed)
    {
        currentMaster = master;
        masterPrimed = true;
    }

    for (int c = 0; c < 2; ++c)
    {
        if (currentMaster == master)
        {
            if (master != 1.0f)
                juce::FloatVectorOperations::multiply (outputs[c], master, numSamples);
        }
        else
        {
            const float step = (master - currentMaster) / (float) numSamples;
            float g = currentMaster;

            for (int i = 0; i < numSamples; ++i)
            {
                g += step;
                outputs[c][i] *= g;
            }
        }
    }

    currentMaster = master;

    for (int c = 0; c < outputCount * 2; ++c)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (outputs[c], numSamples);
        storeMax (outPeaks[(size_t) c], std::max (range.getEnd(), -range.getStart()));
    }
}

} // namespace drums
