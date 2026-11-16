// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include <JuceHeader.h>
#include "TestKit.h"
#include "engine/Importer.h"
#include "engine/Engine.h"
#include "engine/Mixer.h"
#include "engine/Resampler.h"
#include "groove/Grooves.h"
#include "groove/Request.h"
#include "plugin/DrumsProcessor.h"
#include "plugin/DrumsEditor.h"
#include "plugin/Parameters.h"
#include "common/TextAudit.h"

/** drumstool: Nightshift Drums' tests and tools, on the plugin's own sources.

        drumstool --test [name]                        every test (or those whose name has `name` in it)
        drumstool --import <zip> <kitId> <folder>      makes a kit from a DrumGizmo zip
        drumstool --info <kits folder> <kitId>         a kit's mics, instruments and default mix
        drumstool --render <kits folder> <kitId> <out.wav> [--groove "<words>"] [--rate 48000] [--block 512] [--seconds 16]
        drumstool --stress <kits folder> <kitId> [--seconds 20] [--block 128]
                                                       plays a dense groove in real time: the CPU it takes, frames late */
namespace
{
using namespace drums;

juce::String after (const juce::StringArray& args, const juce::String& flag, int offset = 1)
{
    const int i = args.indexOf (flag);
    return i >= 0 && i + offset < args.size() ? args[i + offset].unquoted() : juce::String();
}

//==============================================================================
struct Suite
{
    juce::String filter;
    int passed = 0, failed = 0;
    juce::String current;

    bool wants (const juce::String& name)
    {
        current = name;
        const bool yes = filter.isEmpty() || name.containsIgnoreCase (filter);

        if (yes)
            std::cout << "\n== " << name << std::endl;

        return yes;
    }

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
        (ok ? passed : failed) += 1;
    }
};

juce::File scratch()
{
    static const juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("nightshift-drums-tests");
    dir.createDirectory();
    return dir;
}

/** The test kit, imported once into scratch()/kits/TestKit. */
juce::File testKitFolder()
{
    static juce::File folder;

    if (folder == juce::File())
    {
        const auto zip = scratch().getChildFile ("testkit.zip");
        testkit::writeZip (zip);
        folder = scratch().getChildFile ("kits").getChildFile ("TestKit");
        folder.getParentDirectory().createDirectory();
        const auto error = importDrumGizmo (zip, { "testkit", "Test kit", "made by drumstool", "TestKit/TestKit.xml" }, folder);

        if (error.isNotEmpty())
            std::cout << "import failed: " << error << std::endl;

        addKitForTests (testkit::definition());
    }

    return folder;
}

std::shared_ptr<LoadedKit> openTestKit()
{
    juce::String error;
    std::shared_ptr<LoadedKit> kit (LoadedKit::open (testKitFolder(), Engine::preloadSeconds, error).release());

    if (kit == nullptr)
        std::cout << "couldn't open the test kit: " << error << std::endl;

    return kit;
}

/** An engine playing the test kit (offline: straight from the file, unless streamed). */
struct TestEngine
{
    Engine engine;
    std::shared_ptr<LoadedKit> kit = openTestKit();
    KitDefinition def = testkit::definition();

    TestEngine (bool streamed = false)
    {
        engine.setKit (kit, findKit ("testkit"), NoteMap::build (*findKit ("testkit"), kit->index(), false));
        engine.beginBlock();
        engine.setStreaming (streamed);
        engine.setSeed (7);
    }

    int instrument (const char* name) const     { return kit->index().indexOf (name); }

    juce::AudioBuffer<float> render (int frames, int block = 512)
    {
        juce::AudioBuffer<float> out (Engine::maxMics, frames);
        out.clear();

        for (int at = 0; at < frames; at += block)
        {
            const int n = std::min (block, frames - at);
            float* dest[Engine::maxMics];

            for (int m = 0; m < Engine::maxMics; ++m)
                dest[m] = out.getWritePointer (m, at);

            engine.render (dest, n);
        }

        return out;
    }
};

/** The kit's instrument index for the test kit's own instrument number (the import may order them differently). */
int kitIndexOf (const KitIndex& k, int testInstrument)    { return k.indexOf (testkit::instruments()[(size_t) testInstrument].name); }

//==============================================================================
void testImport (Suite& s)
{
    if (! s.wants ("import: a DrumGizmo zip becomes a Nightshift Drums kit"))
        return;

    const auto folder = testKitFolder();
    juce::String error;
    auto index = KitIndex::load (folder, error);
    s.check (index.has_value(), "the kit's index loads" + (error.isNotEmpty() ? " (" + error + ")" : juce::String()));

    if (! index.has_value())
        return;

    s.check (index->mics.size() == 4 && index->instruments.size() == 4, "4 mics and 4 instruments, as the kit file lists");
    s.check (index->mics[0].kind == Mic::Kind::overhead && index->mics[1].kind == Mic::Kind::overhead && index->mics[2].kind == Mic::Kind::close,
             "the overheads are known as overheads, the snare top as a close mic");
    s.check (std::abs (index->mics[0].pan + 0.8f) < 0.01f && std::abs (index->mics[1].pan - 0.8f) < 0.01f,
             "the overheads sit left and right in the default mix (" + juce::String (index->mics[0].pan, 2) + ", " + juce::String (index->mics[1].pan, 2) + ")");

    const int snare = index->indexOf ("Snare");
    bool sorted = true;

    for (auto& in : index->instruments)
        for (size_t h = 1; h < in.hits.size(); ++h)
            sorted = sorted && in.hits[h - 1].level <= in.hits[h].level;

    s.check (sorted, "every instrument's hits run softest to hardest");
    s.check (snare >= 0 && std::abs (index->instruments[(size_t) snare].hits.front().level + 20.0f) < 0.01f
                 && std::abs (index->instruments[(size_t) snare].hits.back().level) < 0.001f,
             "the snare's softest hit is -20 dB from its hardest, as the kit's powers say");
    s.check (snare >= 0 && index->instruments[(size_t) snare].own == std::vector<bool> { true, true, true, false },
             "the snare's own mics are the overheads and its top mic; the kick mic only bleeds");
    s.check (index->micIndex ("SnareTop") >= 0 && std::abs (index->mics[(size_t) index->micIndex ("SnareTop")].pan) < 0.001f,
             "the snare's mic is in the middle of the default mix");

    // Every sample, stored at 24 bits per hit's own scale
    auto kit = openTestKit();
    double worst = 0.0;
    float worstScale = 0.0f;

    for (int t = 0; t < (int) testkit::instruments().size(); ++t)
    {
        const int i = kitIndexOf (kit->index(), t);
        const auto& in = kit->index().instruments[(size_t) i];

        for (int h = 0; h < (int) in.hits.size(); ++h)
            for (int f = 0; f < in.hits[(size_t) h].frames; ++f)
            {
                const auto* frame = kit->mapped (in.hits[(size_t) h], f);

                for (int m = 0; m < 4; ++m)
                {
                    const double got = storedAt (frame + m * 3) * in.hits[(size_t) h].scale;
                    worst = std::max (worst, std::abs (got - testkit::sample (t, h, m, f)));
                    worstScale = std::max (worstScale, in.hits[(size_t) h].scale);
                }
            }
    }

    s.check (worst <= worstScale * 0.51 + 6.0e-8, "every sample comes back within half a step of 24 bits, and a float's rounding (worst " + juce::String (worst, 12) + ")");

    const auto* pre = kit->preloaded (snare, 0);
    s.check (std::memcmp (pre, kit->mapped (kit->index().instruments[(size_t) snare].hits[0], 0), (size_t) kit->preloadFrames() * 12) == 0,
             "each hit's start is in memory, as on disk (" + juce::String (kit->preloadFrames()) + " frames)");
}

void testEngine (Suite& s)
{
    if (s.wants ("engine: a note plays one of its hits exactly"))
    {
        TestEngine t;
        const int snare = t.instrument ("Snare");
        t.engine.noteOn (38, 1.0f);
        const int hit = t.engine.lastHitOf (snare);
        const auto out = t.render (16000);
        const auto& h = t.kit->index().instruments[(size_t) snare].hits[(size_t) hit];
        const double gain = juce::Decibels::decibelsToGain (juce::jlimit (-40.0, 6.0, 0.0 - (double) h.level));
        const int testHit = [&] { for (int k = 0; k < 5; ++k) if (std::abs (10.0 * std::log10 (testkit::instruments()[0].powers[(size_t) k]) - (double) h.level) < 0.01) return k; return -1; }();
        double worst = 0.0;

        for (int m = 0; m < 4; ++m)
            for (int f = 0; f < 14400; ++f)
                worst = std::max (worst, std::abs (out.getSample (m, f) - gain * testkit::sample (0, testHit, m, f)));

        s.check (hit >= 2 && worst < 1.0e-5, "velocity 127 plays one of the three loudest hits (" + juce::String (hit + 1) + " of 5), every mic sample for sample (worst "
                                                  + juce::String (worst, 9) + ")");
        s.check (out.getMagnitude (3, 0, 14400) > 0.0f && out.getMagnitude (3, 14400, 1600) == 0.0f,
                 "it bleeds into the kick mic, and stops where the hit ends");
    }

    if (s.wants ("engine: velocity chooses how hard a hit was played"))
    {
        TestEngine t;
        const int snare = t.instrument ("Snare");
        double soft = 0.0, hard = 0.0;

        for (int i = 0; i < 20; ++i)
        {
            t.engine.noteOn (38, 0.1f);
            soft += t.kit->index().instruments[(size_t) snare].hits[(size_t) t.engine.lastHitOf (snare)].level;
            t.engine.noteOn (38, 1.0f);
            hard += t.kit->index().instruments[(size_t) snare].hits[(size_t) t.engine.lastHitOf (snare)].level;
        }

        s.check (soft / 20 < -9.0 && hard / 20 > -5.0, "soft notes pick soft hits (average " + juce::String (soft / 20, 1) + " dB), hard ones hard hits ("
                                                          + juce::String (hard / 20, 1) + " dB)");
    }

    if (s.wants ("engine: never the same hit twice running"))
    {
        TestEngine t;
        const int snare = t.instrument ("Snare");
        int repeats = 0, previous = -1;
        std::set<int> used;

        for (int i = 0; i < 40; ++i)
        {
            t.engine.noteOn (38, 0.9f);
            const int hit = t.engine.lastHitOf (snare);
            repeats += hit == previous ? 1 : 0;
            previous = hit;
            used.insert (hit);
            t.render (64);
        }

        s.check (repeats == 0 && used.size() >= 3, "40 notes at one velocity: " + juce::String ((int) used.size()) + " different hits, " + juce::String (repeats) + " repeats");
    }

    if (s.wants ("engine: bleed"))
    {
        TestEngine a, b;
        a.engine.setBleed (0.0f);
        b.engine.setBleed (1.0f);
        a.engine.noteOn (38, 1.0f);
        b.engine.noteOn (38, 1.0f);
        const auto outA = a.render (8000), outB = b.render (8000);
        s.check (outA.getMagnitude (3, 0, 8000) == 0.0f && outB.getMagnitude (3, 0, 8000) > 0.01f,
                 "bleed at 0 takes the snare out of the kick mic; at 100 % it's there");
        s.check (outA.getMagnitude (0, 0, 8000) > 0.1f && std::abs (outA.getMagnitude (0, 0, 8000) - outB.getMagnitude (0, 0, 8000)) < 1.0e-6f,
                 "the overheads are the same either way: bleed never touches them");
    }

    if (s.wants ("engine: the hi-hat closing stops it ringing"))
    {
        // Open, then closed 100 ms later; against the same closed hit alone: the difference is what's
        // left of the open one
        TestEngine both, alone;
        both.engine.noteOn (46, 1.0f);
        const auto first = both.render (4800);
        alone.render (4800);
        both.engine.setSeed (99);
        alone.engine.setSeed (99);
        both.engine.noteOn (42, 1.0f);
        alone.engine.noteOn (42, 1.0f);
        const auto a = both.render (12000), b = alone.render (12000);
        double leftover = 0.0, early = 0.0;

        for (int m = 0; m < 2; ++m)
            for (int f = 0; f < 12000; ++f)
            {
                const double d = std::abs (a.getSample (m, f) - b.getSample (m, f));

                if (f < 960)
                    early = std::max (early, d);
                else if (f >= 1921)
                    leftover = std::max (leftover, d);
            }

        s.check (first.getMagnitude (0, 4000, 800) > 0.05f && early > 0.01, "the open hi-hat rings, and is still fading 20 ms after the closed hit");
        s.check (leftover == 0.0, "40 ms after the closed hit, nothing of the open one is left (" + juce::String (leftover, 9) + ")");
    }

    if (s.wants ("engine: streaming from disk plays the same as reading the file"))
    {
        TestEngine direct (false), streamed (true);
        direct.engine.noteOn (46, 1.0f);
        streamed.engine.noteOn (46, 1.0f);
        const int frames = (int) (1.5 * testkit::rate);
        const auto want = direct.render (frames);
        juce::AudioBuffer<float> got (Engine::maxMics, frames);
        got.clear();

        for (int at = 0; at < frames; at += 256)
        {
            const int n = std::min (256, frames - at);
            float* dest[Engine::maxMics];

            for (int m = 0; m < Engine::maxMics; ++m)
                dest[m] = got.getWritePointer (m, at);

            streamed.engine.render (dest, n);
            juce::Thread::sleep (1);      // about real time: 256 frames are 5 ms
        }

        double diff = 0.0;

        for (int m = 0; m < 4; ++m)
            for (int f = 0; f < frames; ++f)
                diff = std::max (diff, (double) std::abs (got.getSample (m, f) - want.getSample (m, f)));

        s.check (diff == 0.0 && streamed.engine.getUnderruns() == 0,
                 "a 1.5 s hit streamed in 256-frame blocks: identical, no frame late (" + juce::String (streamed.engine.getUnderruns()) + " late)");
    }

    if (s.wants ("engine: no more than 8 of one instrument at once"))
    {
        TestEngine t;

        for (int i = 0; i < 20; ++i)
        {
            t.engine.noteOn (46, 1.0f);
            t.render (48);
        }

        t.render (2000);
        s.check (t.engine.getActiveVoices() <= 2, "20 open hi-hats in a row (each stops the last): " + juce::String (t.engine.getActiveVoices()) + " still sounding");

        TestEngine k;

        for (int i = 0; i < 20; ++i)
        {
            k.engine.noteOn (36, 1.0f);
            k.render (24);
        }

        k.render (2000);
        s.check (k.engine.getActiveVoices() == Engine::maxPerInstrument,
                 "20 kicks inside a tenth of a second: once the oldest have faded, " + juce::String (k.engine.getActiveVoices()) + " still sounding");
    }
}

void testResampler (Suite& s)
{
    if (! s.wants ("resampler: kit rate to host rate"))
        return;

    for (auto [in, out] : { std::pair (48000.0, 44100.0), std::pair (44100.0, 48000.0), std::pair (48000.0, 96000.0) })
    {
        Resampler r;
        r.prepare (1, in, out, 512);

        // A 1 kHz sine through it: its level and frequency survive
        const int blocks = 200;
        juce::AudioBuffer<float> result (1, 512 * blocks);
        juce::int64 n = 0;

        for (int b = 0; b < blocks; ++b)
        {
            const int need = r.inputNeeded (512);
            auto* w = r.writePointer (0);

            for (int i = 0; i < need; ++i, ++n)
                w[i] = (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * (double) n / in);

            r.appended (need);
            float* o[1] { result.getWritePointer (0, b * 512) };
            r.produce (o, 512);
        }

        const int from = 512 * 20, count = 512 * 150;
        const float rms = result.getRMSLevel (0, from, count);
        int crossings = 0;

        for (int i = from + 1; i < from + count; ++i)
            crossings += (result.getSample (0, i - 1) < 0.0f) != (result.getSample (0, i) < 0.0f) ? 1 : 0;

        const double freq = crossings / 2.0 / (count / out);
        s.check (std::abs (juce::Decibels::gainToDecibels (rms / 0.70710678f)) < 0.05f && std::abs (freq - 1000.0) < 1.0,
                 juce::String (in / 1000.0, 1) + " kHz to " + juce::String (out / 1000.0, 1) + " kHz: a 1 kHz sine comes out at "
                     + juce::String (freq, 1) + " Hz, " + juce::String (juce::Decibels::gainToDecibels (rms / 0.70710678f), 3) + " dB");
    }
}

//==============================================================================
/** A processor playing the test kit, its kit loaded. */
std::unique_ptr<DrumsProcessor> testProcessor (double rate, int block)
{
    testKitFolder();
    auto p = std::make_unique<DrumsProcessor>();
    p->getLibrary().setKitsFolderForTests (scratch().getChildFile ("kits"));
    p->setPlayConfigDetails (0, 2, rate, block);
    p->prepareToPlay (rate, block);
    p->chooseKit ("testkit", true);

    for (int i = 0; i < 200 && p->getKitState() == DrumsProcessor::KitState::loading; ++i)
    {
        juce::Thread::sleep (10);
        p->service();
    }

    p->getEngine().setSeed (11);
    p->setNonRealtime (true);
    return p;
}

/** The processor's main out for a list of (sample, note, velocity), rendered in `block`-sample blocks. */
juce::AudioBuffer<float> play (DrumsProcessor& p, const std::vector<std::tuple<int, int, int>>& notes, int length, int block)
{
    juce::AudioBuffer<float> out (2, length);
    out.clear();
    juce::AudioBuffer<float> buffer (2, block);

    for (int at = 0; at < length; at += block)
    {
        const int n = std::min (block, length - at);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;

        for (auto [when, note, velocity] : notes)
            if (when >= at && when < at + n)
                midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) velocity), when - at);

        p.processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, at, buffer, c, 0, n);
    }

    return out;
}

int firstSound (const juce::AudioBuffer<float>& b, float above = 1.0e-6f)
{
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > above || std::abs (b.getSample (1, i)) > above)
            return i;

    return -1;
}

void testProcessor (Suite& s)
{
    if (s.wants ("plugin: notes land on their sample"))
    {
        auto p = testProcessor (48000.0, 512);
        s.check (p->getKitState() == DrumsProcessor::KitState::ready, "the test kit loads");
        const auto out = play (*p, { { 1000, 38, 120 } }, 6000, 512);
        s.check (firstSound (out) == 1000 && p->getLatencySamples() == 0,
                 "at the kit's own rate a note at sample 1000 sounds from sample " + juce::String (firstSound (out)) + ", no latency reported");
    }

    if (s.wants ("plugin: the same at any block size"))
    {
        const std::vector<std::tuple<int, int, int>> notes { { 100, 36, 127 }, { 777, 38, 90 }, { 2048, 42, 70 }, { 2049, 46, 100 }, { 5000, 38, 30 }, { 9999, 42, 127 } };

        for (double rate : { 48000.0, 44100.0, 96000.0 })
        {
            std::vector<juce::AudioBuffer<float>> outs;

            for (int block : { 512, 64, 333, 1 })
            {
                auto p = testProcessor (rate, 512);
                outs.push_back (play (*p, notes, 16000, block));
            }

            double worst = 0.0;

            for (size_t i = 1; i < outs.size(); ++i)
                for (int c = 0; c < 2; ++c)
                    for (int f = 0; f < 16000; ++f)
                        worst = std::max (worst, (double) std::abs (outs[i].getSample (c, f) - outs[0].getSample (c, f)));

            s.check (worst < 1.0e-6, "host at " + juce::String (rate / 1000.0, 1) + " kHz: blocks of 512, 64, 333 and 1 sample give the same output (worst "
                                         + juce::String (worst, 9) + ")");
        }
    }

    if (s.wants ("plugin: another host rate"))
    {
        auto same = testProcessor (48000.0, 512);
        auto other = testProcessor (44100.0, 512);
        const auto a = play (*same, { { 0, 38, 127 } }, 24000, 512);
        const auto b = play (*other, { { 0, 38, 127 } }, 22050, 512);
        const float la = a.getRMSLevel (0, 0, 14000), lb = b.getRMSLevel (0, 0, 12862);
        s.check (std::abs (juce::Decibels::gainToDecibels (lb / la)) < 0.2f,
                 "a 48 kHz kit at 44.1 kHz keeps its level (" + juce::String (juce::Decibels::gainToDecibels (lb / la), 3) + " dB)");

        // Where the snare's energy peaks, against where it peaks at the kit's own rate, in time
        auto peakAt = [] (const juce::AudioBuffer<float>& x)
        {
            int best = 0;
            for (int i = 0; i < x.getNumSamples(); ++i) if (std::abs (x.getSample (0, i)) > std::abs (x.getSample (0, best))) best = i;
            return best;
        };

        const double ta = peakAt (a) / 48000.0 * 1000.0, tb = (peakAt (b) - other->getLatencySamples()) / 44100.0 * 1000.0;
        s.check (other->getLatencySamples() > 0 && other->getLatencySamples() < 64 && std::abs (ta - tb) < 0.1,
                 "it's late by the " + juce::String (other->getLatencySamples()) + " samples it reports (peaks at " + juce::String (ta, 3) + " and "
                     + juce::String (tb, 3) + " ms once that's taken off)");
    }

    if (s.wants ("plugin: the mixer"))
    {
        // Each mix on a fresh plugin, so nothing rings on from the one before; the snare mic soloed
        auto mixWith = [] (std::function<void (std::function<void (const juce::String&, float)>, int)> settings)
        {
            auto p = testProcessor (48000.0, 512);
            auto set = [&] (const juce::String& id, float v) { auto* q = p->getState().getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
            const auto snareTop = p->getKitIndex()->micIndex ("SnareTop");

            for (int m = 0; m < 4; ++m)
                set (params::micSolo (m), m == snareTop ? 1.0f : 0.0f);

            settings (set, snareTop);
            return play (*p, { { 0, 38, 127 } }, 8000, 512);
        };

        const auto left = mixWith ([] (auto set, int mic) { set (params::micPan (mic), -100.0f); });
        s.check (left.getMagnitude (0, 0, 8000) > 0.01f && left.getMagnitude (1, 0, 8000) == 0.0f, "the snare mic soloed and panned hard left: all of it on the left");

        const auto muted = mixWith ([] (auto set, int mic) { set (params::micMute (mic), 1.0f); });
        s.check (muted.getMagnitude (0, 0, 8000) == 0.0f, "muted (and still the only solo): silence");

        const auto upright = mixWith ([] (auto, int) {});
        const auto flipped = mixWith ([] (auto set, int mic) { set (params::micInvert (mic), 1.0f); });
        double sum = 0.0;

        for (int f = 0; f < 8000; ++f)
            sum = std::max (sum, (double) std::abs (upright.getSample (0, f) + flipped.getSample (0, f)));

        s.check (upright.getMagnitude (0, 0, 8000) > 0.01f && sum < 1.0e-6, "polarity flips the mic: the two add to nothing (" + juce::String (sum, 9) + ")");

        const auto index = testProcessor (48000.0, 512)->getKitIndex();
        const float defaultLevel = index->mics[2].level + index->mixTrim;      // the snare top, in the kit's own mix
        const auto quieter = mixWith ([defaultLevel] (auto set, int mic) { set (params::micLevel (mic), defaultLevel - 6.0f); });
        const auto ratio = juce::Decibels::gainToDecibels (quieter.getMagnitude (0, 0, 8000) / upright.getMagnitude (0, 0, 8000));
        s.check (std::abs (ratio + 6.0f) < 0.05f, "its fader 6 dB down: it comes out " + juce::String (ratio, 2) + " dB");
    }

    if (s.wants ("plugin: a session keeps its kit and its faders"))
    {
        auto p = testProcessor (48000.0, 512);
        auto* level = p->getState().getParameter (params::micLevel (2));
        level->setValueNotifyingHost (level->convertTo0to1 (-7.5f));
        juce::MemoryBlock state;
        p->getStateInformation (state);

        auto q = std::make_unique<DrumsProcessor>();
        q->setStateInformation (state.getData(), (int) state.getSize());
        q->service();

        for (int i = 0; i < 200 && q->getKitState() == DrumsProcessor::KitState::loading; ++i)
        {
            juce::Thread::sleep (10);
            q->service();
        }

        const float restored = q->getState().getParameter (params::micLevel (2))->convertFrom0to1 (q->getState().getParameter (params::micLevel (2))->getValue());
        s.check (q->getKitId() == "testkit" && q->getKitState() == DrumsProcessor::KitState::ready && std::abs (restored + 7.5f) < 0.05f,
                 "opened again: the same kit, loaded, and its mic 3 fader where it was (" + juce::String (restored, 1) + " dB), not reset to the kit's default");
    }
}

void testLibrary (Suite& s)
{
    if (! s.wants ("library: getting a kit"))
        return;

    testKitFolder();
    const auto folder = scratch().getChildFile ("library");
    folder.deleteRecursively();
    juce::SharedResourcePointer<Library> library;
    library->setKitsFolderForTests (folder);
    library->useLocalZips ({ { "testkit", scratch().getChildFile ("testkit.zip") } });
    s.check (! library->isReady ("testkit"), "not there to start with");
    library->install ("testkit");
    const bool idle = library->waitUntilIdle (20000);
    s.check (idle && library->isReady ("testkit") && KitIndex::indexFile (folder.getChildFile ("TestKit")).existsAsFile(),
             "got from its zip, made into a kit, ready");
    s.check (library->remove ("testkit") && ! folder.getChildFile ("TestKit").exists() && ! library->isReady ("testkit"), "and removed again");
    library->useLocalZips ({});
    library->setKitsFolderForTests (scratch().getChildFile ("kits"));
}

void testCatalog (Suite& s)
{
    if (! s.wants ("catalog: the three kits"))
        return;

    for (auto& def : catalog())
    {
        if (def.id == "testkit")
            continue;

        std::set<juce::String> ids;
        std::map<int, juce::String> noteOwner;
        bool unique = true, notesAgree = true, shaped = true;

        for (auto& p : def.pieces)
        {
            unique = unique && ids.insert (p.id).second;
            shaped = shaped && (p.shape.rx > 0.0f || ! p.shape.polygon.empty());

            for (auto& a : p.articulations)
                for (int n : a.notes)
                {
                    if (auto it = noteOwner.find (n); it != noteOwner.end() && it->second != a.instrument)
                        notesAgree = false;

                    noteOwner[n] = a.instrument;
                }
        }

        s.check (unique && shaped && notesAgree, def.name + ": " + juce::String ((int) def.pieces.size()) + " pieces, every one with a place in the picture and no note on two instruments");

        bool gm = true;

        for (auto [voice, note] : { std::pair ("kick", 36), std::pair ("snare", 38), std::pair ("hat_closed", 42), std::pair ("hat_open", 46),
                                    std::pair ("ride", 51), std::pair ("crash", 49), std::pair ("tom_high", 50), std::pair ("tom_mid", 47), std::pair ("tom_floor", 43) })
            gm = gm && noteOwner.count (grooves::noteFor (voice)) > 0 && grooves::noteFor (voice) == note;

        s.check (gm, def.name + ": the General MIDI drums NightShift's drummer sends are all there");
    }
}

void testGrooves (Suite& s)
{
    if (! s.wants ("grooves: the AI drummer writes for every style"))
        return;

    int written = 0, total = 0, unmapped = 0;
    juce::StringArray empty;
    std::set<int> crocellNotes;

    for (auto& p : findKit ("crocell")->pieces)
        for (auto& a : p.articulations)
            for (int n : a.notes)
                crocellNotes.insert (n);

    for (auto& style : nightshift::agents::styles())
    {
        ++total;
        const auto w = grooves::write (style.name, 120.0, 4, 4, 3);
        double length = 0.0;
        const auto seq = grooves::toMidi (w.groove, 120.0, -1, length);
        int notes = 0;

        for (int i = 0; i < seq.getNumEvents(); ++i)
            if (seq.getEventPointer (i)->message.isNoteOn())
            {
                ++notes;
                unmapped += crocellNotes.count (seq.getEventPointer (i)->message.getNoteNumber()) > 0 ? 0 : 1;
            }

        if (notes > 0 && length > 0.0 && ! w.groove.sections.empty())
            ++written;
        else
            empty.add (style.name);
    }

    s.check (written == total, juce::String (written) + " of " + juce::String (total) + " styles write a groove" + (empty.isEmpty() ? juce::String() : " (not: " + empty.joinIntoString (", ") + ")"));
    s.check (unmapped == 0, "every note they play is a drum on the Crocell kit (" + juce::String (unmapped) + " aren't)");

    const auto file = scratch().getChildFile ("groove.mid");
    const auto w = grooves::write ("funk, busy hats", 100.0, 4, 4, 1);
    const bool saved = grooves::saveMidiFile (w.groove, 100.0, -1, file);
    juce::MidiFile readBack;
    juce::FileInputStream in (file);
    const bool reads = in.openedOk() && readBack.readFrom (in) && readBack.getNumTracks() == 1;
    s.check (saved && reads && file.getSize() > 100, "a groove saves as a MIDI file to drag into a host, and reads back (" + juce::String (file.getSize()) + " bytes)");
}

/** The kits themselves, where they've been downloaded: every articulation of every piece plays. */
void testRealKits (Suite& s)
{
    if (! s.wants ("the downloaded kits: every piece plays"))
        return;

    for (auto& def : catalog())
    {
        if (def.id == "testkit")
            continue;

        const auto folder = kitFolderFor (defaultKitsFolder(), def);
        juce::String error;

        if (! KitIndex::load (folder, error).has_value())
        {
            std::cout << "SKIP  " << def.name << " isn't downloaded here" << std::endl;
            continue;
        }

        std::shared_ptr<LoadedKit> kit (LoadedKit::open (folder, Engine::preloadSeconds, error).release());

        if (kit == nullptr)
        {
            s.check (false, def.name + " opens (" + error + ")");
            continue;
        }

        for (bool brushes : { false, true })
        {
            if (brushes && def.brushes.empty())
                continue;

            Engine e;
            e.setKit (kit, &def, NoteMap::build (def, kit->index(), brushes));
            e.beginBlock();
            e.setStreaming (false);
            juce::StringArray silent, missing;
            int played = 0;
            juce::AudioBuffer<float> out (Engine::maxMics, 12000);

            for (auto& piece : def.pieces)
                for (auto& a : piece.articulations)
                {
                    auto name = a.instrument;

                    if (brushes && def.brushes.count (name) > 0)
                        name = def.brushes.at (name);

                    if (kit->index().indexOf (name) < 0)
                    {
                        missing.add (name);
                        continue;
                    }

                    e.stopAll (true);
                    out.clear();
                    e.noteOn (a.notes.front(), 0.9f);
                    float* dest[Engine::maxMics];

                    for (int m = 0; m < Engine::maxMics; ++m)
                        dest[m] = out.getWritePointer (m);

                    e.render (dest, 12000);
                    ++played;

                    if (out.getMagnitude (0, 12000) < 1.0e-4f)
                        silent.add (piece.name + " " + a.name);
                }

            s.check (missing.isEmpty() && silent.isEmpty() && played > 0,
                     def.name + (brushes ? " with brushes" : "") + ": all " + juce::String (played) + " articulations play"
                         + (missing.isEmpty() ? juce::String() : "; missing: " + missing.joinIntoString (", "))
                         + (silent.isEmpty() ? juce::String() : "; silent: " + silent.joinIntoString (", ")));
        }

        s.check (kit->index().mics.size() <= (size_t) Mixer::maxStrips,
                 def.name + ": " + juce::String ((int) kit->index().mics.size()) + " mics, each with a mixer strip; "
                     + juce::File::descriptionOfSizeInBytes (kit->preloadBytes()) + " of hit starts in memory");
    }
}

//==============================================================================
int runTests (const juce::String& filter)
{
    Suite s;
    s.filter = filter;
    testImport (s);
    testEngine (s);
    testResampler (s);
    testProcessor (s);
    testLibrary (s);
    testCatalog (s);
    testGrooves (s);
    testRealKits (s);

    std::cout << "\n" << s.passed << " passed, " << s.failed << " failed" << std::endl;
    std::cout << (s.failed == 0 ? "ALL PASSED" : "SOME FAILED") << std::endl;
    return s.failed == 0 ? 0 : 1;
}

int importKit (const juce::StringArray& args)
{
    const juce::File zip (after (args, "--import", 1)), folder (after (args, "--import", 3));
    const auto* def = findKit (after (args, "--import", 2));

    if (def == nullptr || ! zip.existsAsFile())
    {
        std::cout << "usage: drumstool --import <zip> <crocell|drs|muldjord> <folder>" << std::endl;
        return 1;
    }

    const auto started = juce::Time::getMillisecondCounterHiRes();
    int lastShown = -1;
    const auto error = importDrumGizmo (zip, { def->id, def->name, def->credit, def->kitFile }, folder, [&] (float p)
    {
        if (const int pct = (int) (p * 100.0f); pct / 5 != lastShown / 5)
        {
            lastShown = pct;
            std::cout << pct << "% " << std::flush;
        }
    });

    std::cout << std::endl << (error.isEmpty() ? "imported " + def->name + " into " + folder.getFullPathName() : "failed: " + error)
              << " in " << juce::String ((juce::Time::getMillisecondCounterHiRes() - started) / 1000.0, 1) << " s" << std::endl;
    return error.isEmpty() ? 0 : 1;
}

int info (const juce::StringArray& args)
{
    const juce::File kits (after (args, "--info", 1));
    const auto* def = findKit (after (args, "--info", 2));

    if (def == nullptr)
        return 1;

    juce::String error;
    auto k = KitIndex::load (kitFolderFor (kits, *def), error);

    if (! k.has_value())
    {
        std::cout << error << std::endl;
        return 1;
    }

    std::cout << k->name << ": " << k->sampleRate << " Hz, " << (int) k->mics.size() << " mics, " << (int) k->instruments.size() << " instruments, "
              << juce::File::descriptionOfSizeInBytes (k->dataBytes) << std::endl << "Default mix:" << std::endl;

    std::cout << "  (overheads and room named as " << (k->namedFromAudience ? "the audience sees the kit: turned round" : "the drummer sits")
              << "; every fader " << juce::String (k->mixTrim, 1) << " dB more, so the hardest kick or snare peaks at -6 dBFS)" << std::endl;

    for (auto& m : k->mics)
        std::cout << "  " << m.name.paddedRight (' ', 14) << Mic::kindName (m.kind).paddedRight (' ', 9) << " level " << juce::String (m.level, 1).paddedLeft (' ', 6)
                  << " dB  pan " << juce::String (picturedPan (*def, m).value_or (m.pan), 2).paddedLeft (' ', 5)
                  << (picturedPan (*def, m).has_value() ? " (picture)" : "          ") << (m.invert ? "  flipped" : "") << (m.muted ? "  off" : "")
                  << (m.owner.isNotEmpty() ? "  " + m.owner : juce::String()) << std::endl;

    int hits = 0;
    juce::int64 frames = 0;

    for (auto& in : k->instruments)
    {
        hits += (int) in.hits.size();

        for (auto& h : in.hits)
            frames += h.frames;
    }

    std::cout << hits << " hits, " << juce::String (frames / k->sampleRate / 60.0, 1) << " minutes of recording" << std::endl;

    // Where the overheads and room hear each instrument: right minus left, in dB, on its hardest hit's first 300 ms
    if (args.contains ("--balance"))
    {
        juce::String error2;
        auto loaded = LoadedKit::open (kitFolderFor (kits, *def), 0.01, error2);

        if (loaded != nullptr)
            for (auto& in : k->instruments)
            {
                const auto& hit = in.hits.back();
                const int frames = std::min (hit.frames, (int) (k->sampleRate * 0.3));
                std::vector<double> e (k->mics.size(), 0.0);

                for (int f = 0; f < frames; ++f)
                    for (size_t m = 0; m < k->mics.size(); ++m)
                    {
                        const double v = storedAt (loaded->mapped (hit, f) + m * 3) * hit.scale;
                        e[m] += v * v;
                    }

                double ohL = 0, ohR = 0, amL = 0, amR = 0;

                for (size_t m = 0; m < k->mics.size(); ++m)
                {
                    const float side = micSideFor (k->mics[m].name);
                    const bool overhead = k->mics[m].kind == Mic::Kind::overhead, room = k->mics[m].kind == Mic::Kind::room;

                    if (side < 0.0f)
                        (overhead ? ohL : amL) += (overhead || room) ? e[m] : 0.0;
                    else if (side > 0.0f)
                        (overhead ? ohR : amR) += (overhead || room) ? e[m] : 0.0;
                }

                std::cout << "  " << in.name.paddedRight (' ', 22) << " overheads R-L " << juce::String (10.0 * std::log10 ((ohR + 1e-12) / (ohL + 1e-12)), 1).paddedLeft (' ', 6)
                          << " dB   room R-L " << juce::String (10.0 * std::log10 ((amR + 1e-12) / (amL + 1e-12)), 1).paddedLeft (' ', 6) << " dB" << std::endl;
            }
    }

    auto map = NoteMap::build (*def, *k, false);
    juce::StringArray missing;

    for (auto& p : def->pieces)
        for (auto& a : p.articulations)
            if (k->indexOf (a.instrument) < 0)
                missing.add (a.instrument);

    std::cout << (missing.isEmpty() ? "every articulation found" : "missing: " + missing.joinIntoString (", ")) << std::endl;
    return 0;
}

/** Plays a groove through the plugin into a WAV, the way a host would render it. */
int render (const juce::StringArray& args)
{
    const juce::File kits (after (args, "--render", 1)), out (after (args, "--render", 3));
    const auto kitId = after (args, "--render", 2);
    const double rate = args.contains ("--rate") ? after (args, "--rate").getDoubleValue() : 48000.0;
    const int block = args.contains ("--block") ? after (args, "--block").getIntValue() : 512;
    const double seconds = args.contains ("--seconds") ? after (args, "--seconds").getDoubleValue() : 16.0;
    const auto words = args.contains ("--groove") ? after (args, "--groove") : juce::String ("rock");
    const double bpm = args.contains ("--bpm") ? after (args, "--bpm").getDoubleValue() : 110.0;

    DrumsProcessor p;
    p.getLibrary().setKitsFolderForTests (kits);
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);
    p.chooseKit (kitId, true);

    for (int i = 0; i < 3000 && p.getKitState() == DrumsProcessor::KitState::loading; ++i)
    {
        juce::Thread::sleep (10);
        p.service();
    }

    if (p.getKitState() != DrumsProcessor::KitState::ready)
    {
        std::cout << "the kit didn't load: " << p.getKitError() << std::endl;
        return 1;
    }

    const auto w = grooves::write (words, bpm, 4, 4, 1);
    double length = 0.0;
    const auto seq = grooves::toMidi (w.groove, bpm, -1, length);
    const double tempo = grooves::tempoOf (w.groove, bpm);
    std::cout << w.reply << std::endl;

    p.setNonRealtime (true);
    const int total = (int) (seconds * rate);
    juce::AudioBuffer<float> audio (2, total), buffer (2, block);
    const double samplesPerTick = rate * 60.0 / tempo / grooves::ticksPerQuarter;
    int index = 0;

    for (int at = 0; at < total; at += block)
    {
        const int n = std::min (block, total - at);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;

        for (; index < seq.getNumEvents(); ++index)
        {
            const auto& m = seq.getEventPointer (index)->message;
            const auto when = (int) std::llround (m.getTimeStamp() * samplesPerTick);

            if (when >= at + n)
                break;

            if (m.isNoteOn())
                midi.addEvent (m, std::max (0, when - at));
        }

        p.processBlock (buffer, midi);

        for (int c = 0; c < 2; ++c)
            audio.copyFrom (c, at, buffer, c, 0, n);
    }

    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    const bool ok = writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, total);
    writer = nullptr;
    std::cout << (ok ? "rendered " : "couldn't write ") << out.getFullPathName() << ": peak " << juce::String (juce::Decibels::gainToDecibels (audio.getMagnitude (0, total)), 1)
              << " dBFS, RMS " << juce::String (juce::Decibels::gainToDecibels (std::max (audio.getRMSLevel (0, 0, total), audio.getRMSLevel (1, 0, total))), 1) << " dBFS" << std::endl;
    return ok ? 0 : 1;
}

/** A dense groove in real time, streamed: how much of a core it takes, and whether the disk kept up. */
int stress (const juce::StringArray& args)
{
    const juce::File kits (after (args, "--stress", 1));
    const auto kitId = after (args, "--stress", 2);
    const double seconds = args.contains ("--seconds") ? after (args, "--seconds").getDoubleValue() : 20.0;
    const int block = args.contains ("--block") ? after (args, "--block").getIntValue() : 128;
    const double rate = 48000.0;

    DrumsProcessor p;
    p.getLibrary().setKitsFolderForTests (kits);
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);
    p.chooseKit (kitId, true);

    for (int i = 0; i < 3000 && p.getKitState() == DrumsProcessor::KitState::loading; ++i)
    {
        juce::Thread::sleep (10);
        p.service();
    }

    if (p.getKitState() != DrumsProcessor::KitState::ready)
    {
        std::cout << "the kit didn't load: " << p.getKitError() << std::endl;
        return 1;
    }

    // Blast beats with every cymbal: the most a drummer could ask of it
    const auto w = grooves::write ("blast beats, crashes and china on every bar, double kick", 220.0, 4, 4, 5);
    double length = 0.0;
    const auto seq = grooves::toMidi (w.groove, 220.0, -1, length);
    const double samplesPerTick = rate * 60.0 / 220.0 / grooves::ticksPerQuarter;
    const juce::int64 loopSamples = (juce::int64) (length * grooves::ticksPerQuarter * samplesPerTick);
    juce::AudioBuffer<float> buffer (2, block);
    const int blocks = (int) (seconds * rate / block);
    double busiest = 0.0, totalMs = 0.0;
    int maxVoices = 0, notesPlayed = 0;
    const double blockMs = block / rate * 1000.0;

    // On a real-time thread of its own, as a host's audio thread is (above the streamer)
    struct AudioThread : juce::Thread
    {
        explicit AudioThread (std::function<void()> f) : juce::Thread ("drumstool audio"), body (std::move (f)) {}
        void run() override { body(); }
        std::function<void()> body;
    };

    AudioThread audio ([&]
    {
        juce::MidiBuffer midi;
        midi.ensureSize (8192);
        auto next = juce::Time::getMillisecondCounterHiRes();

        for (int b = 0; b < blocks; ++b)
        {
            midi.clear();
            const juce::int64 from = (juce::int64) b * block % std::max<juce::int64> (1, loopSamples);

            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;
                const auto when = (juce::int64) std::llround (m.getTimeStamp() * samplesPerTick);

                if (m.isNoteOn() && when >= from && when < from + block)
                {
                    midi.addEvent (m, (int) (when - from));
                    ++notesPlayed;
                }
            }

            buffer.clear();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            p.processBlock (buffer, midi);
            const auto took = juce::Time::getMillisecondCounterHiRes() - t0;
            busiest = std::max (busiest, took);
            totalMs += took;
            maxVoices = std::max (maxVoices, p.getEngine().getActiveVoices());

            // Real time: the next block when its time comes (spinning out the last part of the wait,
            // since a sleep can't be shorter than a millisecond)
            next += blockMs;

            while (juce::Time::getMillisecondCounterHiRes() < next - 1.5)
                juce::Thread::sleep (1);

            while (juce::Time::getMillisecondCounterHiRes() < next) {}
        }
    });

    audio.startRealtimeThread (juce::Thread::RealtimeOptions{}.withPriority (10).withPeriodMs (blockMs));

    while (audio.isThreadRunning())
        juce::Thread::sleep (50);

    std::cout << notesPlayed << " notes in " << seconds << " s at " << block << "-sample blocks: " << juce::String (100.0 * totalMs / (blocks * blockMs), 1)
              << " % of a core on average, the busiest block " << juce::String (busiest, 2) << " ms of its " << juce::String (blockMs, 2) << " ms, "
              << maxVoices << " voices at most, " << (juce::int64) p.getUnderruns() << " frames late from the disk" << std::endl;
    return p.getUnderruns() == 0 ? 0 : 1;
}

/** Loads the built VST3 as a host would, lets its kit load, plays a note through it and opens its window. */
int hostVst3 (const juce::StringArray& args)
{
    const juce::File plugin (after (args, "--vst3"));
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, plugin.getFullPathName());

    if (found.isEmpty())
    {
        std::cout << "FAIL  no plugin in " << plugin.getFullPathName() << std::endl;
        return 1;
    }

    juce::String error;
    auto instance = format.createInstanceFromDescription (*found[0], 48000.0, 512, error);

    if (instance == nullptr)
    {
        std::cout << "FAIL  couldn't load it: " << error << std::endl;
        return 1;
    }

    std::cout << "loaded " << found[0]->name << " (" << found[0]->category << ", instrument: " << (found[0]->isInstrument ? "yes" : "no") << ", "
              << instance->getParameters().size() << " parameters, " << instance->getBusCount (false) << " output buses)" << std::endl;

    instance->setPlayConfigDetails (0, 2, 48000.0, 512);
    instance->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> block (2, 512);
    float peak = 0.0f;

    // Its kit loads on its own thread, picked up on the message thread: play a snare every half second until it sounds
    for (int i = 0; i < 400 && peak < 1.0e-3f; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::MidiBuffer midi;

        if (i % 25 == 0)
            midi.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 120), 0);

        for (int b = 0; b < 2; ++b)
        {
            block.clear();
            instance->processBlock (block, midi);
            midi.clear();
            peak = std::max (peak, block.getMagnitude (0, 512));
        }
    }

    std::cout << (peak > 1.0e-3f ? "PASS  " : "FAIL  ") << "a snare through the VST3: peak " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS" << std::endl;

    std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditorIfNeeded());
    std::cout << (editor != nullptr ? "PASS  " : "FAIL  ") << "its window opens"
              << (editor != nullptr ? " (" + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) + ")" : juce::String()) << std::endl;
    editor = nullptr;
    instance->releaseResources();
    return peak > 1.0e-3f ? 0 : 1;
}

/** The window, offscreen, with a kit loaded: a page, a groove written, notes just played. */
int snapshot (const juce::StringArray& args)
{
    const juce::File out (after (args, "--snapshot"));
    const auto kitsArg = after (args, "--kits");
    const auto kitId = args.contains ("--kit") ? after (args, "--kit") : juce::String ("crocell");

    DrumsProcessor p;

    if (kitsArg == "test")
    {
        testKitFolder();
        p.getLibrary().setKitsFolderForTests (scratch().getChildFile ("kits"));
    }
    else if (kitsArg.isNotEmpty())
        p.getLibrary().setKitsFolderForTests (juce::File (kitsArg));

    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    p.chooseKit (kitId, true);

    for (int i = 0; i < 3000 && p.getKitState() == DrumsProcessor::KitState::loading; ++i)
    {
        juce::Thread::sleep (10);
        p.service();
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* inner = dynamic_cast<DrumsEditor*> (editor->getChildComponent (0));
    const auto page = after (args, "--page");

    if (inner != nullptr)
    {
        inner->showPage (page == "mixer" ? 1 : page == "grooves" ? 2 : page == "kits" ? 3 : 0);

        if (args.contains ("--groove"))
            inner->writeGroove (after (args, "--groove"));
    }

    editor->setVisible (true);

    // Play what's asked (notes "36,38,46"), a block at a time, with the window's timers running
    juce::StringArray notes;
    notes.addTokens (after (args, "--hit"), ",", "");
    notes.removeEmptyStrings();
    juce::AudioBuffer<float> block (2, 512);

    for (int frame = 0; frame < 24; ++frame)
    {
        juce::MidiBuffer midi;

        if (frame == 21)        // just before the picture, so the pieces are still lit
            for (auto& n : notes)
                midi.addEvent (juce::MidiMessage::noteOn (10, n.getIntValue(), (juce::uint8) 120), 0);

        for (int b = 0; b < 3; ++b)
        {
            block.clear();
            p.processBlock (block, midi);
            midi.clear();
        }

        juce::MessageManager::getInstance()->runDispatchLoopUntil (33);
    }

    juce::Image image;

    if (args.contains ("--textaudit"))
        std::cout << nightshift::textaudit::report (nightshift::textaudit::collect (*editor, &image), args.contains ("--alltext"));
    else
        image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    out.deleteFile();
    juce::FileOutputStream stream (out);
    const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    std::cout << (ok ? "snapshot " : "snapshot failed ") << out.getFullPathName() << "  (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
    editor = nullptr;
    return ok ? 0 : 1;
}

} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    juce::StringArray args;

    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    if (args.contains ("--test"))
        return runTests (after (args, "--test"));

    if (args.contains ("--import"))
        return importKit (args);

    if (args.contains ("--info"))
        return info (args);

    if (args.contains ("--render"))
        return render (args);

    if (args.contains ("--stress"))
        return stress (args);

    if (args.contains ("--snapshot"))
        return snapshot (args);

    if (args.contains ("--vst3"))
        return hostVst3 (args);

    std::cout << "drumstool --test [name] | --import <zip> <kitId> <folder> | --info <kits> <kitId> | --render <kits> <kitId> <out.wav> | --stress <kits> <kitId>" << std::endl;
    return 1;
}
