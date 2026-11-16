// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>

/** A kit as Nightshift Drums keeps it on disk: a folder holding

        kit.json      the mics, the instruments, and where each hit's audio is
        samples.bin   every hit of every instrument with all of its mics at once:
                      frames of 24-bit samples, one per mic, each hit at its own scale

    made once from a DrumGizmo kit by the importer (Importer.h) and played by the engine:
    each hit's first moments from memory, the rest streamed from disk (LoadedKit). */
namespace drums
{

struct Mic
{
    enum class Kind { close, overhead, room, trigger };

    juce::String name;              // the kit's own: "SnareTop", "OHLeft", "AmbRight"
    Kind kind = Kind::close;
    float pan = 0.0f;               // where the default mix puts it, -1 left .. 1 right, as the drummer sits
    float level = 0.0f;             // its level in the default mix, dB
    bool invert = false;            // polarity flipped in the default mix (a snare's bottom mic, a kick's outside one)
    bool muted = false;             // off in the default mix (a kick trigger's clicks)
    juce::String owner;             // a close mic's drum: the instrument whose hardest hit it hears loudest

    static juce::String kindName (Kind);
    static Kind kindFromName (const juce::String&);
};

struct Hit
{
    juce::int64 offset = 0;         // where its frames start in samples.bin, bytes
    int frames = 0;
    float level = 0.0f;             // how hard it was played, dB, 0 = the instrument's hardest
    float scale = 1.0f;             // a stored sample (-8388607..8388607) times this is the recorded value
};

struct Instrument
{
    juce::String name;              // the kit's own: "HihatOpen", "Tom1"
    juce::String group;             // instruments in a group stop each other's ringing (the hi-hat's)
    std::vector<bool> own;          // per mic: one of the instrument's own (main) mics; the others only bleed
    std::vector<Hit> hits;          // softest first

    /** A drum machine's: its hits are velocity layers, each in a few takes (hits of the same level).
        Velocity picks the layer and the takes take turns, never another layer's hit. */
    bool layered = false;
};

struct KitIndex
{
    static constexpr int format = 1, bytesPerSample = 3;
    static constexpr float fullScale = 8388607.0f;

    juce::String id, name, source;  // "crocell", "Crocell", "CrocellKit 1.1 by DrumGizmo.org, CC BY 4.0"
    double sampleRate = 48000.0;
    bool namedFromAudience = false; // its overheads and room are named left and right as the audience sees the kit (the default mix turns them round)
    float mixTrim = 0.0f;           // dB on every fader of the default mix, so its hardest kick or snare peaks at -6 dBFS (kits are recorded at different levels)
    std::vector<Mic> mics;
    std::vector<Instrument> instruments;
    juce::int64 dataBytes = 0;      // samples.bin's size

    int frameBytes() const          { return (int) mics.size() * bytesPerSample; }
    int indexOf (const juce::String& instrumentName) const;     // -1 if the kit hasn't got it
    int micIndex (const juce::String& micName) const;

    juce::var toVar() const;
    static std::optional<KitIndex> fromVar (const juce::var&, juce::String& error);

    /** kit.json in `folder` (written in place of the old one only once it's complete). */
    bool save (const juce::File& folder) const;
    static std::optional<KitIndex> load (const juce::File& folder, juce::String& error);

    static juce::File indexFile (const juce::File& folder)  { return folder.getChildFile ("kit.json"); }
    static juce::File dataFile (const juce::File& folder)   { return folder.getChildFile ("samples.bin"); }
};

/** A stored sample (3 bytes, little-endian, signed), not yet scaled. */
inline float storedAt (const juce::uint8* p) noexcept
{
    const auto bits = (juce::uint32) p[0] << 8 | (juce::uint32) p[1] << 16 | (juce::uint32) p[2] << 24;
    return (float) ((juce::int32) bits >> 8);
}

/** A recorded value as a stored sample, given 1 / the hit's scale. */
inline void storeAt (juce::uint8* p, float value, double inverseScale) noexcept
{
    const auto v = (juce::int32) juce::jlimit ((double) -KitIndex::fullScale, (double) KitIndex::fullScale, std::round ((double) value * inverseScale));
    p[0] = (juce::uint8) (v & 0xff);
    p[1] = (juce::uint8) ((v >> 8) & 0xff);
    p[2] = (juce::uint8) ((v >> 16) & 0xff);
}

//==============================================================================
/** A kit ready to play: its index, its samples mapped from disk, and the first moments of every
    hit copied into memory so a hit sounds at once while the rest streams in. Made off the audio
    thread; the engine and its streamer share it. */
class LoadedKit
{
public:
    /** Opens the kit in `folder`, keeping `preloadSeconds` of every hit in memory (all of a
        shorter hit). Null, with `error` said in words, if it can't. */
    static std::unique_ptr<LoadedKit> open (const juce::File& folder, double preloadSeconds, juce::String& error,
                                            std::function<bool()> shouldStop = nullptr, std::function<void (float)> progress = nullptr);

    /** A kit made in memory (a drum machine, synthesised): `samples` laid out as samples.bin is.
        It plays exactly as a kit from disk does; there's just no disk. */
    static std::unique_ptr<LoadedKit> fromMemory (KitIndex, juce::MemoryBlock samples, double preloadSeconds, juce::String& error);

    const KitIndex& index() const noexcept                  { return kit; }
    const juce::File& folder() const noexcept               { return dir; }
    int preloadFrames() const noexcept                      { return preload; }
    juce::int64 preloadBytes() const noexcept               { return (juce::int64) memory.getSize(); }

    /** A hit's first preloadFrames() frames (or all of a shorter one), in memory. */
    const juce::uint8* preloaded (int instrument, int hit) const noexcept
    {
        return static_cast<const juce::uint8*> (memory.getData()) + starts[(size_t) instrument][(size_t) hit];
    }

    /** One of a hit's frames in the mapped file: reading it may wait for the disk, so only the
        streamer (or an offline render) does. */
    const juce::uint8* mapped (const Hit& hit, int frame) const noexcept
    {
        return base + hit.offset + (juce::int64) frame * kit.frameBytes();
    }

    /** The mapped file from its start (the streamer's). */
    const juce::uint8* data() const noexcept                { return base; }

private:
    LoadedKit() = default;

    KitIndex kit;
    juce::File dir;
    int preload = 0;
    std::unique_ptr<juce::MemoryMappedFile> map;
    juce::MemoryBlock samplesInMemory;            // a kit made in memory: its samples.bin
    const juce::uint8* base = nullptr;            // the samples, mapped or in memory
    juce::MemoryBlock memory;
    std::vector<std::vector<size_t>> starts;      // per instrument, per hit: where its preload starts in `memory`

    bool preloadHits (double preloadSeconds, juce::String& error, std::function<bool()> shouldStop, std::function<void (float)> progress);

    JUCE_DECLARE_NON_COPYABLE (LoadedKit)
};

} // namespace drums
