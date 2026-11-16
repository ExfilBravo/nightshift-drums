// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Importer.h"
#include "ZipReader.h"
#include <map>
#include <set>

namespace drums
{

namespace
{
    struct ChannelMap
    {
        juce::String in, out;
        bool main = false;
    };

    struct SourceInstrument
    {
        juce::String name, file, group;
        std::vector<ChannelMap> maps;
        bool hasMain = false;
    };

    struct AudioRef
    {
        juce::String channel, file;
        int fileChannel = 1;
    };

    struct SourceHit
    {
        double power = -1.0;
        std::vector<AudioRef> audio;
    };

    juce::String folderOf (const juce::String& path)
    {
        return path.containsChar ('/') ? path.upToLastOccurrenceOf ("/", false, false) : juce::String();
    }

    juce::String join (const juce::String& folder, juce::String relative)
    {
        relative = relative.replaceCharacter ('\\', '/');

        while (relative.startsWith ("./"))
            relative = relative.substring (2);

        return folder.isEmpty() ? relative : folder + "/" + relative;
    }

    std::unique_ptr<juce::XmlElement> readXml (const ZipReader& zip, const juce::String& path)
    {
        const int index = zip.indexOf (path);

        if (index < 0)
            return {};

        auto in = zip.open (index);
        return in != nullptr ? juce::parseXML (in->readEntireStreamAsString()) : nullptr;
    }

    /** A WAV in the zip, decoded: every channel. */
    bool decode (const ZipReader& zip, const juce::String& path, juce::AudioBuffer<float>& audio, double& rate)
    {
        const int index = zip.indexOf (path);

        if (index < 0)
            return false;

        auto in = zip.open (index);

        if (in == nullptr)
            return false;

        juce::MemoryBlock block;
        in->readIntoMemoryBlock (block);

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (new juce::MemoryInputStream (block, false), true));

        if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
            return false;

        rate = reader->sampleRate;
        audio.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        return reader->read (audio.getArrayOfWritePointers(), audio.getNumChannels(), 0, audio.getNumSamples());
    }

    juce::String drumOf (const juce::String& name)
    {
        const auto n = name.toLowerCase();

        if (n.contains ("kdrum") || n.contains ("kick") || n.contains ("bassdrum"))   return "kick";
        if (n.contains ("snare"))                                                       return "snare";
        if (n.contains ("tom"))                                                         return "tom";
        if (n.contains ("hihat") || n.contains ("hi-hat") || n.contains ("hat"))        return "hihat";
        if (n.contains ("ride"))                                                        return "ride";
        return {};
    }

    /** "Tom1", "FTom1", "Tom1_whisker" -> "tom1", "ftom1", "tom1": which tom a name is about. */
    juce::String tomOf (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        const int at = n.indexOf ("tom");

        if (at < 0)
            return {};

        return (at > 0 && n[at - 1] == 'f' ? "f" : "") + juce::String ("tom") + n.substring (at + 3).initialSectionContainingOnly ("0123456789");
    }

    /** Whether a close mic is an instrument's own, for kits whose file doesn't say. */
    bool ownMicGuess (const juce::String& instrument, const juce::String& channel)
    {
        const auto i = drumOf (instrument), c = drumOf (channel);

        if (i.isEmpty() || i != c)
            return false;

        return i != "tom" || tomOf (instrument) == tomOf (channel);
    }

    double energy (const float* s, int count)
    {
        double sum = 0.0;

        for (int i = 0; i < count; ++i)
            sum += (double) s[i] * s[i];

        return sum;
    }

    /** Whether `other` is the opposite polarity to `reference`: the strongest correlation within
        3 ms either way is negative. */
    bool flipped (const float* reference, const float* other, int length, double rate)
    {
        const int window = std::min (length, (int) (rate * 0.03)), maxLag = (int) (rate * 0.003);
        double strongest = 0.0;

        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;

            for (int i = std::max (0, -lag); i < window && i + lag < length; ++i)
                sum += (double) reference[i] * other[i + lag];

            if (std::abs (sum) > std::abs (strongest))
                strongest = sum;
        }

        return strongest < 0.0;
    }

    /** What the default mix needs from an instrument's hardest hit: each mic's energy over its
        first 300 ms, and the opening 50 ms of each mic, for polarity. */
    struct Hardest
    {
        std::vector<double> energy;
        juce::AudioBuffer<float> opening;
    };
}

//==============================================================================
Mic::Kind micKindFor (const juce::String& channel)
{
    const auto n = channel.toLowerCase();

    if (n.contains ("trigger") || n.contains ("trig"))
        return Mic::Kind::trigger;

    if (n.contains ("amb") || n.contains ("room"))
        return Mic::Kind::room;

    if (n.startsWith ("oh") || n.contains ("overhead"))
        return Mic::Kind::overhead;

    return Mic::Kind::close;
}

float micSideFor (const juce::String& channel)
{
    const auto n = channel.toLowerCase().removeCharacters ("_- ");

    if (n.contains ("center") || n.contains ("centre") || n.contains ("mid"))
        return 0.0f;

    if (n.contains ("left") || n.endsWithChar ('l'))
        return -1.0f;

    if (n.contains ("right") || n.endsWithChar ('r'))
        return 1.0f;

    return 0.0f;
}

//==============================================================================
juce::String importDrumGizmo (const juce::File& zipFile, const ImportSource& src, const juce::File& destination,
                              std::function<void (float)> progress, std::function<bool()> shouldStop)
{
    ZipReader zip (zipFile);

    if (! zip.isOpen())
        return "The " + src.name + " download isn't a zip Nightshift Drums can read (it may be cut short). Download it again.";

    auto kitXml = readXml (zip, src.kitFile);

    if (kitXml == nullptr || ! kitXml->hasTagName ("drumkit"))
        return "The " + src.name + " download isn't the DrumGizmo kit it should be: there's no " + src.kitFile + " in it.";

    const auto kitFolder = folderOf (src.kitFile);

    KitIndex kit;
    kit.id = src.id;
    kit.name = src.name;
    kit.source = src.source;

    if (auto* channels = kitXml->getChildByName ("channels"))
        for (auto* c : channels->getChildWithTagNameIterator ("channel"))
        {
            Mic m;
            m.name = c->getStringAttribute ("name");
            m.kind = micKindFor (m.name);
            kit.mics.push_back (m);
        }

    if (kit.mics.empty() || kit.mics.size() > 64)
        return "The " + src.name + " kit file lists no mics Nightshift Drums can use.";

    std::vector<SourceInstrument> sources;

    if (auto* list = kitXml->getChildByName ("instruments"))
        for (auto* e : list->getChildWithTagNameIterator ("instrument"))
        {
            SourceInstrument in;
            in.name = e->getStringAttribute ("name");
            in.file = join (kitFolder, e->getStringAttribute ("file"));
            in.group = e->getStringAttribute ("group");

            for (auto* map : e->getChildWithTagNameIterator ("channelmap"))
            {
                in.maps.push_back ({ map->getStringAttribute ("in"), map->getStringAttribute ("out"), map->getBoolAttribute ("main") });
                in.hasMain = in.hasMain || map->getBoolAttribute ("main");
            }

            sources.push_back (std::move (in));
        }

    if (sources.empty())
        return "The " + src.name + " kit file lists no instruments.";

    // Each instrument's hits, and how much there is to read altogether (for the progress)
    std::vector<std::vector<SourceHit>> hitLists;
    juce::int64 totalBytes = 0;

    for (auto& in : sources)
    {
        auto xml = readXml (zip, in.file);

        if (xml == nullptr)
            return "Couldn't read " + in.file + " in the " + src.name + " download.";

        std::vector<SourceHit> hits;
        std::set<juce::String> files;

        if (auto* samples = xml->getChildByName ("samples"))
            for (auto* s : samples->getChildWithTagNameIterator ("sample"))
            {
                SourceHit hit;
                hit.power = s->getDoubleAttribute ("power", -1.0);

                for (auto* a : s->getChildWithTagNameIterator ("audiofile"))
                {
                    hit.audio.push_back ({ a->getStringAttribute ("channel"), join (folderOf (in.file), a->getStringAttribute ("file")),
                                           a->getIntAttribute ("filechannel", 1) });
                    files.insert (hit.audio.back().file);
                }

                if (! hit.audio.empty())
                    hits.push_back (std::move (hit));
            }

        for (auto& f : files)
            if (const int index = zip.indexOf (f); index >= 0)
                totalBytes += zip.getSize (index);

        hitLists.push_back (std::move (hits));
    }

    // Into a folder of its own, put in place only when it's complete
    const auto staging = destination.getSiblingFile (destination.getFileName() + ".importing");
    staging.deleteRecursively();

    if (! staging.createDirectory())
        return "Couldn't make a folder for the kit in " + destination.getParentDirectory().getFullPathName() + ".";

    auto fail = [&] (const juce::String& why)
    {
        staging.deleteRecursively();
        return why;
    };

    juce::int64 doneBytes = 0;
    double rate = 0.0;
    const int micCount = (int) kit.mics.size(), frameBytes = kit.frameBytes();
    std::vector<Hardest> hardest (sources.size());

    {
        juce::FileOutputStream out (KitIndex::dataFile (staging), 1 << 22);

        if (! out.openedOk())
            return fail ("Couldn't write the kit into " + staging.getFullPathName() + ".");

        juce::HeapBlock<juce::uint8> frames;
        size_t framesSize = 0;

        for (size_t i = 0; i < sources.size(); ++i)
        {
            const auto& source = sources[i];
            Instrument in;
            in.name = source.name;
            in.group = source.group;

            // An instrument's own mics: the ones its kit file calls main, and the overheads and room,
            // which are everything's; the other close mics only bleed
            for (auto& m : kit.mics)
            {
                bool own = m.kind == Mic::Kind::overhead || m.kind == Mic::Kind::room;

                if (! own && m.kind == Mic::Kind::close)
                {
                    if (source.hasMain)
                    {
                        for (auto& map : source.maps)
                            if (map.out.equalsIgnoreCase (m.name))
                                own = map.main;
                    }
                    else
                    {
                        own = ownMicGuess (source.name, m.name);
                    }
                }

                in.own.push_back (own);
            }

            // The mic a hit's channel feeds: by the instrument's channel map, else its own name
            auto micFor = [&] (const juce::String& channel)
            {
                for (auto& map : source.maps)
                    if (map.in.equalsIgnoreCase (channel))
                        return kit.micIndex (map.out);

                return kit.micIndex (channel);
            };

            double hardestPower = -1.0;

            for (auto& hit : hitLists[i])
            {
                if (shouldStop != nullptr && shouldStop())
                    return fail ("Stopped.");

                std::map<juce::String, juce::AudioBuffer<float>> decoded;
                std::vector<std::pair<const float*, int>> micAudio ((size_t) micCount, { nullptr, 0 });
                int length = 0;

                for (auto& ref : hit.audio)
                {
                    if (decoded.count (ref.file) == 0)
                    {
                        double fileRate = 0.0;

                        if (! decode (zip, ref.file, decoded[ref.file], fileRate))
                            return fail ("Couldn't read " + ref.file + " in the " + src.name + " download.");

                        if (rate == 0.0)
                            rate = fileRate;
                        else if (std::abs (fileRate - rate) > 0.5)
                            return fail ("The " + src.name + " kit mixes sample rates (" + juce::String (rate) + " and " + juce::String (fileRate) + "), which Nightshift Drums can't play.");

                        if (const int index = zip.indexOf (ref.file); index >= 0)
                            doneBytes += zip.getSize (index);
                    }

                    const auto& audio = decoded[ref.file];
                    const int mic = micFor (ref.channel);

                    if (mic >= 0 && juce::isPositiveAndBelow (ref.fileChannel - 1, audio.getNumChannels()))
                    {
                        micAudio[(size_t) mic] = { audio.getReadPointer (ref.fileChannel - 1), audio.getNumSamples() };
                        length = std::max (length, audio.getNumSamples());
                    }
                }

                if (length == 0)
                    continue;

                float peak = 0.0f;

                for (auto& [data, count] : micAudio)
                    if (data != nullptr)
                    {
                        const auto range = juce::FloatVectorOperations::findMinAndMax (data, count);
                        peak = std::max ({ peak, range.getEnd(), -range.getStart() });
                    }

                Hit h;
                h.offset = out.getPosition();
                h.frames = length;
                h.scale = peak > 0.0f ? peak / KitIndex::fullScale : 1.0f / KitIndex::fullScale;

                // How hard it was played: the kit's own measure, else the energy of its own close
                // mics (the overheads, when it has none) at the start
                if (hit.power > 0.0)
                {
                    h.level = (float) (10.0 * std::log10 (hit.power));
                }
                else
                {
                    double e = 0.0;

                    for (int m = 0; m < micCount; ++m)
                        if (micAudio[(size_t) m].first != nullptr && (in.own[(size_t) m] || kit.mics[(size_t) m].kind == Mic::Kind::overhead))
                            e += energy (micAudio[(size_t) m].first, std::min (micAudio[(size_t) m].second, (int) (rate * 0.05)));

                    h.level = (float) (10.0 * std::log10 (std::max (e, 1.0e-12)));
                }

                const auto needed = (size_t) length * (size_t) frameBytes;

                if (needed > framesSize)
                {
                    frames.realloc (needed);
                    framesSize = needed;
                }

                const double inverse = 1.0 / (double) h.scale;

                for (int m = 0; m < micCount; ++m)
                {
                    const auto [data, count] = micAudio[(size_t) m];
                    auto* p = frames.get() + (size_t) m * KitIndex::bytesPerSample;

                    for (int f = 0; f < length; ++f, p += frameBytes)
                        storeAt (p, data != nullptr && f < count ? data[f] : 0.0f, inverse);
                }

                if (! out.write (frames.get(), needed))
                    return fail ("Couldn't write the " + src.name + " kit (is the disk full?).");

                // The hardest hit so far: what the default mix is worked out from
                const double power = hit.power > 0.0 ? hit.power : std::pow (10.0, h.level / 10.0);

                if (power > hardestPower)
                {
                    hardestPower = power;
                    auto& hd = hardest[i];
                    hd.energy.assign ((size_t) micCount, 0.0);
                    const int opening = std::min (length, (int) (rate * 0.05));
                    hd.opening.setSize (micCount, opening);
                    hd.opening.clear();

                    for (int m = 0; m < micCount; ++m)
                        if (const auto [data, count] = micAudio[(size_t) m]; data != nullptr)
                        {
                            hd.energy[(size_t) m] = energy (data, std::min (count, (int) (rate * 0.3)));
                            hd.opening.copyFrom (m, 0, data, std::min (count, opening));
                        }
                }

                in.hits.push_back (h);

                if (progress != nullptr)
                    progress ((float) ((double) doneBytes / (double) std::max<juce::int64> (1, totalBytes)) * 0.98f);
            }

            if (in.hits.empty())
                continue;

            // Softest first, the hardest at 0 dB
            std::sort (in.hits.begin(), in.hits.end(), [] (const Hit& a, const Hit& b) { return a.level < b.level; });
            const float top = in.hits.back().level;

            for (auto& h : in.hits)
                h.level -= top;

            kit.instruments.push_back (std::move (in));
        }

        out.flush();

        if (out.getStatus().failed())
            return fail ("Couldn't write the " + src.name + " kit: " + out.getStatus().getErrorMessage());

        kit.dataBytes = out.getPosition();
    }

    if (kit.instruments.empty() || rate <= 0.0)
        return fail ("The " + src.name + " download has no hits in it.");

    kit.sampleRate = rate;

    //==========================================================================
    // The default mix, as the drummer sits (the hi-hat on the left), the way the kit's picture shows it.
    auto hardestOf = [&] (const Instrument& in) -> const Hardest&
    {
        const auto source = std::find_if (sources.begin(), sources.end(), [&] (auto& s) { return s.name == in.name; });
        return hardest[(size_t) std::distance (sources.begin(), source)];
    };

    // Where the overheads hear each instrument: right over left, in dB
    auto balanceOf = [&] (const Instrument& in) -> std::optional<double>
    {
        const auto& hd = hardestOf (in);

        if (hd.energy.empty())
            return {};

        double left = 0.0, right = 0.0;

        for (int m = 0; m < micCount; ++m)
        {
            if (kit.mics[(size_t) m].kind != Mic::Kind::overhead)
                continue;

            const float side = micSideFor (kit.mics[(size_t) m].name);

            if (side < 0.0f)
                left += hd.energy[(size_t) m];
            else if (side > 0.0f)
                right += hd.energy[(size_t) m];
        }

        if (left <= 0.0 || right <= 0.0)
            return {};

        return 10.0 * std::log10 (right / left);
    };

    // Which side an instrument's own name puts it on, as the drummer sees it: "Crash_left_tip", or a
    // capital L or R on the end of its name ("CrashL", "RideRBell", "CrashRXtra"; not "HihatPedal")
    auto namedSide = [] (juce::String name)
    {
        if (drumOf (name) == "kick")
            return 0;

        if (name.containsIgnoreCase ("left"))
            return -1;

        if (name.containsIgnoreCase ("right"))
            return 1;

        for (auto suffix : { "Bell", "Stopped", "Stop", "Xtra", "Choke" })
            if (name.endsWithIgnoreCase (suffix))
                name = name.dropLastCharacters ((int) std::strlen (suffix));

        return name.endsWithChar ('L') ? -1 : name.endsWithChar ('R') ? 1 : 0;
    };

    // The overheads named as the audience sees the kit: the drummer's left-hand cymbals louder in the
    // "right" overhead than the right-hand ones are (by more than 2 dB, on average)
    {
        double leftSum = 0.0, rightSum = 0.0;
        int leftCount = 0, rightCount = 0;

        for (auto& in : kit.instruments)
            if (const auto b = balanceOf (in))
            {
                if (namedSide (in.name) < 0) { leftSum += *b; ++leftCount; }
                if (namedSide (in.name) > 0) { rightSum += *b; ++rightCount; }
            }

        if (leftCount > 0 && rightCount > 0)
            kit.namedFromAudience = leftSum / leftCount - rightSum / rightCount > 2.0;
    }

    const float orientation = kit.namedFromAudience ? -1.0f : 1.0f;

    // The middle: where the overheads hear the snare (a pair of overheads seldom hear evenly), else
    // the middle of where they hear everything
    double middle = 0.0;

    if (const int snare = kit.indexOf ("Snare"); snare >= 0 && balanceOf (kit.instruments[(size_t) snare]).has_value())
    {
        middle = *balanceOf (kit.instruments[(size_t) snare]);
    }
    else
    {
        std::vector<double> all;

        for (auto& in : kit.instruments)
            if (const auto b = balanceOf (in))
                all.push_back (*b);

        if (! all.empty())
        {
            std::sort (all.begin(), all.end());
            middle = all[all.size() / 2];
        }
    }

    // Overheads as a pair, a little in from the sides; the room wide and a little under them; a trigger off
    for (auto& m : kit.mics)
    {
        if (m.kind == Mic::Kind::overhead)
            m.pan = micSideFor (m.name) * 0.8f * orientation;
        else if (m.kind == Mic::Kind::room)
        {
            m.pan = micSideFor (m.name) * orientation;
            m.level = -3.0f;
        }
        else if (m.kind == Mic::Kind::trigger)
            m.muted = true;
    }

    // Each close mic belongs to the instrument whose hardest hit it hears loudest, of those that call it their own
    std::map<int, std::vector<int>> owned;     // instrument -> its close mics

    for (int m = 0; m < micCount; ++m)
    {
        if (kit.mics[(size_t) m].kind != Mic::Kind::close)
            continue;

        int owner = -1;
        double loudest = -1.0;

        for (size_t i = 0; i < kit.instruments.size(); ++i)
        {
            const auto source = std::find_if (sources.begin(), sources.end(), [&] (auto& s) { return s.name == kit.instruments[i].name; });
            const auto& hd = hardest[(size_t) std::distance (sources.begin(), source)];

            if (kit.instruments[i].own[(size_t) m] && ! hd.energy.empty() && hd.energy[(size_t) m] > loudest)
            {
                loudest = hd.energy[(size_t) m];
                owner = (int) i;
            }
        }

        if (owner >= 0)
            owned[owner].push_back (m);
    }

    for (auto& [owner, mics] : owned)
    {
        const auto& instrument = kit.instruments[(size_t) owner];
        const auto& hd = hardestOf (instrument);
        double overheads = 0.0, close = 0.0;

        for (int m = 0; m < micCount; ++m)
            if (kit.mics[(size_t) m].kind == Mic::Kind::overhead)
                overheads += hd.energy[(size_t) m];

        for (int m : mics)
            close += hd.energy[(size_t) m];

        // Kick and snare in the middle, as mixes have them; the rest where the overheads hear them,
        // against the snare, as the drummer sits (the plugin pans them by the kit's picture when it can)
        const auto drum = drumOf (instrument.name);
        float pan = 0.0f;

        if (drum != "kick" && drum != "snare")
            if (const auto b = balanceOf (instrument))
                pan = juce::jlimit (-0.7f, 0.7f, orientation * (float) (*b - middle) * 0.15f);

        // The close mics a few dB over the overheads on drums (their punch), level with them on
        // the hats and ride
        const float wanted = drum == "kick" ? 8.0f : (drum == "snare" || drum == "tom") ? 5.0f : 0.0f;
        float level = 0.0f;

        if (close > 0.0 && overheads > 0.0)
            level = juce::jlimit (-12.0f, 12.0f, wanted - (float) (10.0 * std::log10 (close / overheads)));

        for (int m : mics)
        {
            kit.mics[(size_t) m].pan = pan;
            kit.mics[(size_t) m].level = level;
            kit.mics[(size_t) m].owner = instrument.name;
        }

        // Two or more mics on one drum: the loudest is the reference, any the other way round flipped
        if (mics.size() > 1 && hd.opening.getNumSamples() > 0)
        {
            int reference = mics.front();

            for (int m : mics)
                if (energy (hd.opening.getReadPointer (m), hd.opening.getNumSamples()) > energy (hd.opening.getReadPointer (reference), hd.opening.getNumSamples()))
                    reference = m;

            for (int m : mics)
                if (m != reference)
                    kit.mics[(size_t) m].invert = flipped (hd.opening.getReadPointer (reference), hd.opening.getReadPointer (m), hd.opening.getNumSamples(), rate);
        }
    }

    // Every kit about as loud: its hardest kick or snare through the default mix peaks at -6 dBFS
    {
        float peak = 0.0f;

        for (auto& in : kit.instruments)
        {
            const bool kick = drumOf (in.name) == "kick", snare = in.name.equalsIgnoreCase ("snare");

            if (! kick && ! snare)
                continue;

            const auto& hd = hardestOf (in);
            const int n = hd.opening.getNumSamples();

            if (n == 0)
                continue;

            std::vector<float> left ((size_t) n, 0.0f), right ((size_t) n, 0.0f);

            for (int m = 0; m < micCount; ++m)
            {
                const auto& mic = kit.mics[(size_t) m];

                if (mic.muted)
                    continue;

                const float gain = juce::Decibels::decibelsToGain (mic.level) * (mic.invert ? -1.0f : 1.0f);
                const float angle = (juce::jlimit (-1.0f, 1.0f, mic.pan) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                const auto* x = hd.opening.getReadPointer (m);

                for (int i = 0; i < n; ++i)
                {
                    left[(size_t) i] += x[i] * gain * std::cos (angle);
                    right[(size_t) i] += x[i] * gain * std::sin (angle);
                }
            }

            for (int i = 0; i < n; ++i)
                peak = std::max ({ peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]) });
        }

        if (peak > 0.0f)
            kit.mixTrim = juce::jlimit (-18.0f, 18.0f, -6.0f - juce::Decibels::gainToDecibels (peak));
    }

    if (! kit.save (staging))
        return fail ("Couldn't write the kit's index into " + staging.getFullPathName() + ".");

    destination.deleteRecursively();

    if (! staging.moveFileTo (destination))
        return fail ("Couldn't put the " + src.name + " kit in " + destination.getParentDirectory().getFullPathName() + ".");

    if (progress != nullptr)
        progress (1.0f);

    return {};
}

} // namespace drums
