// SPDX-License-Identifier: GPL-3.0-or-later
// NightShift - Copyright (C) 2026 NightWriter. See LICENSE.

#pragma once

#include <JuceHeader.h>

/** What Nightshift's synthesised drums are made of: band-limited squares, filters, seeded
    noise, soft saturation, and a few helpers for rendering a hit into a buffer. Shared by the
    built-in kit (DrumKit.cpp) and the 808 and 909 machines (Machines.cpp). */
namespace nightshift::drumsynth
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    inline float saturate (float x, float drive)
    {
        return std::tanh (drive * x) / std::tanh (drive);
    }

    inline double decay (double t, double tau)
    {
        return std::exp (-t / tau);
    }

    /** A band-limited square, so the hats stay clean instead of aliasing into mush. */
    struct BlepSquare
    {
        BlepSquare (double hz, double sampleRate) : increment (hz / sampleRate) {}

        float next()
        {
            float v = phase < 0.5 ? 1.0f : -1.0f;
            v += blep (phase);
            v -= blep (std::fmod (phase + 0.5, 1.0));

            phase += increment;

            if (phase >= 1.0)
                phase -= 1.0;

            return v;
        }

    private:
        float blep (double t) const
        {
            const double dt = increment;

            if (t < dt)
            {
                t /= dt;
                return (float) (t + t - t * t - 1.0);
            }

            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return (float) (t * t + t + t + 1.0);
            }

            return 0.0f;
        }

        double phase = 0.0, increment;
    };

    struct Filter
    {
        static Filter highPass (double sr, double hz, double q = 0.707)  { return Filter (juce::IIRCoefficients::makeHighPass (sr, hz, q)); }
        static Filter lowPass  (double sr, double hz, double q = 0.707)  { return Filter (juce::IIRCoefficients::makeLowPass (sr, hz, q)); }
        static Filter bandPass (double sr, double hz, double q)          { return Filter (juce::IIRCoefficients::makeBandPass (sr, hz, q)); }

        float operator() (float x)    { return filter.processSingleSampleRaw (x); }

    private:
        explicit Filter (const juce::IIRCoefficients& c)    { filter.setCoefficients (c); }
        juce::IIRFilter filter;
    };

    struct Noise
    {
        explicit Noise (juce::int64 seed) : random (seed) {}
        float operator()()            { return random.nextFloat() * 2.0f - 1.0f; }
        juce::Random random;
    };

    inline void normalise (juce::AudioBuffer<float>& b, float peakDb)
    {
        const float peak = b.getMagnitude (0, b.getNumSamples());

        if (peak > 0.0f)
            b.applyGain (juce::Decibels::decibelsToGain (peakDb) / peak);
    }

    inline void fadeOut (juce::AudioBuffer<float>& b, double sr, double ms)
    {
        const int n = std::min (b.getNumSamples(), (int) (sr * ms / 1000.0));
        b.applyGainRamp (b.getNumSamples() - n, n, 1.0f, 0.0f);
    }

    /** A mono hit of `seconds`, each sample from fn (time in seconds). */
    template <typename Fn>
    juce::AudioBuffer<float> generate (double sr, double seconds, Fn&& fn)
    {
        juce::AudioBuffer<float> b (1, (int) (sr * seconds));
        auto* d = b.getWritePointer (0);

        for (int i = 0; i < b.getNumSamples(); ++i)
            d[i] = fn ((double) i / sr);

        return b;
    }

    /** A 24-bit WAV, replacing what's there. */
    inline bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, double sr)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                     .withSampleRate (sr)
                                                     .withNumChannels (buffer.getNumChannels())
                                                     .withBitsPerSample (24));

        return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }
}
