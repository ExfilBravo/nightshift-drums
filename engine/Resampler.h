// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>

namespace drums
{

/** Takes the mics from the kit's sample rate to the host's: a 64-tap windowed-sinc (Kaiser)
    resampler, one filter shared by every channel, its kernel looked up from 512 phases.

    The engine renders kit-rate frames into it just in time for each host block, and a note is
    placed at the kit-rate frame that matches its host time, a fixed half filter later: so the
    output is the same whatever the host's block size, delayed by getLatency() host samples
    (well under a millisecond). At equal rates it passes frames straight through, with no delay.

    prepare() allocates; everything else is real-time safe. */
class Resampler
{
public:
    static constexpr int taps = 64, half = taps / 2, phases = 512;

    /** For `channels` channels, kit rate `inRate` to host rate `outRate`, at most `maxOut` host
        samples a block. */
    void prepare (int channels, double inRate, double outRate, int maxOut)
    {
        numChannels = channels;
        ratio = inRate / outRate;
        bypass = std::abs (ratio - 1.0) < 1.0e-9;
        const int capacity = (int) std::ceil (maxOut * std::max (1.0, ratio)) + taps * 3 + 16;
        buffer.setSize (channels, capacity, false, true, true);

        if (! bypass)
            makeKernel();

        reset();
    }

    void reset() noexcept
    {
        buffer.clear();
        written = 0;

        if (bypass)
        {
            writePos = 0;
            readPos = 0.0;
            firstFrame = 0;
        }
        else
        {
            // `taps` frames of silence behind the first, the first output centred half a filter in
            writePos = taps;
            firstFrame = -taps;
            readPos = (double) half;
        }
    }

    bool isBypassed() const noexcept        { return bypass; }

    /** Host samples from a note to its sound. */
    double getLatency() const noexcept      { return bypass ? 0.0 : (double) (half + 1) / ratio; }

    /** Kit-rate frames to append before the next produce (numOut). */
    int inputNeeded (int numOut) const noexcept
    {
        if (bypass)
            return numOut;

        const auto last = (int) std::floor (readPos + (numOut - 1) * ratio) + half;
        return std::max (0, last + 1 - writePos);
    }

    /** The kit-rate frame (counting every frame ever appended) a note at host sample `outOffset` of the
        next produce() should start on. Never one already appended. */
    juce::int64 inputFrameFor (int outOffset) const noexcept
    {
        if (bypass)
            return written + outOffset;

        return firstFrame + (juce::int64) std::floor (readPos + outOffset * ratio) + half + 1;
    }

    juce::int64 inputWritten() const noexcept   { return written; }

    /** Where to render the next frames (inputNeeded of them, at most). Cleared. */
    float* writePointer (int channel) noexcept  { return buffer.getWritePointer (channel, writePos); }

    void clearAhead (int numFrames) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::clear (buffer.getWritePointer (ch, writePos), numFrames);
    }

    void appended (int numFrames) noexcept
    {
        writePos += numFrames;
        written += numFrames;
    }

    /** numOut host-rate samples of each channel into `out`. */
    void produce (float* const* out, int numOut) noexcept
    {
        if (bypass)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                juce::FloatVectorOperations::copy (out[ch], buffer.getReadPointer (ch), numOut);

            discard (numOut);
            return;
        }

        float kernel[taps];

        for (int j = 0; j < numOut; ++j)
        {
            const double centre = readPos + j * ratio;
            const int index = (int) std::floor (centre);
            const double phase = (centre - index) * phases;
            const int p = std::min ((int) phase, phases - 1);
            const auto t = (float) (phase - p);
            const float* k0 = table.data() + (size_t) p * taps;
            const float* k1 = k0 + taps;

            for (int k = 0; k < taps; ++k)
                kernel[k] = k0[k] + t * (k1[k] - k0[k]);

            const int base = index - half + 1;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* in = buffer.getReadPointer (ch, base);
                float sum = 0.0f;

                for (int k = 0; k < taps; ++k)
                    sum += in[k] * kernel[k];

                out[ch][j] = sum;
            }
        }

        readPos += numOut * ratio;
        const int consumed = (int) std::floor (readPos) - half + 1;

        if (consumed > 0)
        {
            discard (consumed);
            readPos -= consumed;
            firstFrame += consumed;
        }
    }

private:
    int numChannels = 0;
    double ratio = 1.0;
    bool bypass = true;
    juce::AudioBuffer<float> buffer;
    int writePos = 0;
    double readPos = 0.0;
    juce::int64 firstFrame = 0, written = 0;
    std::vector<float> table;

    void discard (int frames) noexcept
    {
        const int keep = writePos - frames;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            std::memmove (d, d + frames, sizeof (float) * (size_t) std::max (0, keep));
        }

        writePos = std::max (0, keep);
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }

        return sum;
    }

    void makeKernel()
    {
        // Cut off a little under the lower rate's Nyquist; the Kaiser window takes the stopband ~80 dB down
        const double cutoff = 0.5 * std::min (1.0, 1.0 / ratio) * 0.91;     // cycles per input sample
        const double beta = 8.0, norm = besselI0 (beta);
        table.assign ((size_t) (phases + 1) * taps, 0.0f);

        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0.0;
            auto* row = table.data() + (size_t) p * taps;

            for (int k = 0; k < taps; ++k)
            {
                const double x = (k - half + 1) - frac;
                const double arg = 2.0 * cutoff * x;
                const double sinc = std::abs (arg) < 1.0e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * arg) / (juce::MathConstants<double>::pi * arg);
                const double w = std::abs (x) >= half ? 0.0 : besselI0 (beta * std::sqrt (1.0 - (x / half) * (x / half))) / norm;
                row[k] = (float) (2.0 * cutoff * sinc * w);
                sum += row[k];
            }

            for (int k = 0; k < taps; ++k)
                row[k] = (float) (row[k] / sum);    // unity at DC on every phase
        }
    }
};

} // namespace drums
