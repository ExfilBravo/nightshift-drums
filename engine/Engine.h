// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "KitFile.h"
#include "Catalog.h"

namespace drums
{

/** Plays a kit: each note becomes one of its instrument's recorded hits, chosen by how hard it's
    played and never the same twice running, sounding on every mic at once. Runs at the kit's own
    sample rate; the processor resamples and mixes what it renders.

    Hits start from memory (each one's first moments are loaded with the kit) and carry on from
    disk: a streaming thread keeps a ring buffer a few chunks ahead of each voice. The audio thread
    never waits for the disk; if the disk ever falls behind, a voice plays silence for the frames
    that didn't arrive and getUnderruns() counts them. An offline render (setStreaming false) reads
    the kit's file straight instead, at whatever speed it goes.

    Threads: setKit and releaseUnused on the message thread; everything else on the audio thread. */
class Engine
{
public:
    static constexpr int maxMics = 16, maxVoices = 96, maxPerInstrument = 8;
    static constexpr int chunkFrames = 2048, ringChunks = 4, ringFrames = chunkFrames * ringChunks;
    static constexpr double preloadSeconds = 0.1;

    Engine();
    ~Engine();

    //==============================================================================
    /** Message thread: plays `kit` (null for none) from the audio thread's next block. */
    void setKit (std::shared_ptr<LoadedKit> kit, const KitDefinition* definition, NoteMap map);

    /** Message thread: frees kits the audio thread has let go of. Call it from a timer. */
    void releaseUnused();

    //==============================================================================
    /** Audio thread, at the start of each block: picks up a new kit. False while there's none. */
    bool beginBlock();

    const LoadedKit* getKit() const noexcept                { return current != nullptr ? current->kit.get() : nullptr; }
    const KitDefinition* getDefinition() const noexcept     { return current != nullptr ? current->definition : nullptr; }
    const NoteMap* getNoteMap() const noexcept              { return current != nullptr ? &current->map : nullptr; }
    int getMicCount() const noexcept                        { return current != nullptr ? (int) current->kit->index().mics.size() : 0; }
    double getKitRate() const noexcept                      { return current != nullptr ? current->kit->index().sampleRate : 0.0; }

    /** True (the default) when the host plays in real time: streamed through the ring buffers. */
    void setStreaming (bool streamed) noexcept              { streaming = streamed; }

    /** How much of the other drums' close mics a hit is heard in, 0..2 (1 as recorded). */
    void setBleed (float amount) noexcept                   { bleed = juce::jlimit (0.0f, 2.0f, amount); }

    /** How far velocity moves a hit's loudness, 0..2 (1: the kit's whole recorded range). */
    void setDynamics (float amount) noexcept                { dynamics = juce::jlimit (0.0f, 2.0f, amount); }

    /** Restarts the generator that picks between equally loud hits, for repeatable renders. */
    void setSeed (juce::int64 seed) noexcept                { random.setSeed (seed); }

    /** A note at the current render position. Velocity 0..1. */
    void noteOn (int note, float velocity);

    /** A hit of one of the kit's instruments directly. */
    void play (int instrument, float velocity, int piece = -1);

    /** Every sound stops: over a few milliseconds, or at once. */
    void stopAll (bool atOnce = false);

    /** A cymbal grabbed: whatever the note's instrument is ringing stops (an e-kit's aftertouch). */
    void choke (int note);

    /** Adds the next `numFrames` frames of every playing voice into mics[0 .. getMicCount()). */
    void render (float* const* mics, int numFrames);

    /** The hit an instrument played last (-1 if none yet): for tests, on the audio thread. */
    int lastHitOf (int instrument) const noexcept
    {
        return current != nullptr && juce::isPositiveAndBelow (instrument, (int) current->lastHits.size())
                   ? current->lastHits[(size_t) instrument][0] : -1;
    }

    int getActiveVoices() const noexcept                    { return activeCount.load(); }
    juce::int64 getUnderruns() const noexcept               { return underruns.load(); }

    /** What has been hit since the last call, for the kit view to light up: piece and velocity. */
    struct Struck { int piece; float velocity; };
    int popStruck (Struck* into, int most);

private:
    struct Loaded
    {
        std::shared_ptr<LoadedKit> kit;
        const KitDefinition* definition = nullptr;
        NoteMap map;
        std::vector<std::array<int, 2>> lastHits;               // per instrument: the last two hits it played
    };

    struct Voice
    {
        // The audio thread's own
        bool active = false;
        int instrument = -1, hitIndex = -1, piece = -1;
        const Hit* hit = nullptr;
        int position = 0, frames = 0;
        float gains[maxMics] {};                // velocity and the hit's scale, per mic
        bool own[maxMics] {};
        float fade = 1.0f, fadeStep = 0.0f;
        juce::uint32 order = 0;                 // when it started, for stealing the oldest

        // Shared with the streamer: what to stream, guarded by `sequence` (odd while it changes)
        std::atomic<juce::uint32> sequence { 0 };
        std::atomic<const LoadedKit*> streamKit { nullptr };
        std::atomic<juce::int64> streamOffset { 0 };
        std::atomic<int> streamFrames { 0 };
        std::atomic<juce::uint64> progress { 0 };   // sequence << 32 | frames past the preload in the ring
        std::atomic<int> consumed { 0 };            // frames past the preload already played
        juce::HeapBlock<juce::uint8> ring;
    };

    class Streamer;

    Loaded* current = nullptr;
    std::atomic<Loaded*> pending { nullptr };
    std::atomic<Loaded*> inUse { nullptr };
    std::vector<std::unique_ptr<Loaded>> owned;                 // message thread: every Loaded not yet freed
    juce::AbstractFifo retiredFifo { 16 };
    std::array<Loaded*, 16> retired {};
    juce::CriticalSection streamLock;                           // held by the streamer while it reads a kit

    std::array<Voice, maxVoices> voices;
    std::unique_ptr<Streamer> streamer;

    bool streaming = true;
    float bleed = 1.0f, dynamics = 1.0f;
    juce::Random random { 0x5eed };
    juce::uint32 orderCounter = 0;
    std::atomic<int> activeCount { 0 };
    std::atomic<juce::int64> underruns { 0 };

    juce::AbstractFifo struckFifo { 256 };
    std::array<Struck, 256> struck {};

    int chooseHit (const Instrument&, int instrument, float velocity, float& gainDb);
    Voice* freeVoice();
    void start (Voice&, int instrument, int hitIndex, float gainDb, int piece);
    void stop (Voice&);
    void fadeOut (Voice&, double seconds);
    void renderVoice (Voice&, float* const* mics, int micCount, int numFrames);
    void fill (Voice&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};

} // namespace drums
