// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Engine.h"

namespace drums
{

/** Keeps every playing voice's ring buffer filled ahead of it, from the kit's mapped file. */
class Engine::Streamer : public juce::Thread
{
public:
    explicit Streamer (Engine& e) : juce::Thread ("Nightshift Drums streamer"), engine (e) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            {
                const juce::ScopedLock sl (engine.streamLock);

                for (auto& v : engine.voices)
                    engine.fill (v);
            }

            wait (2);
        }
    }

private:
    Engine& engine;
};

//==============================================================================
Engine::Engine()
{
    for (auto& v : voices)
        v.ring.allocate ((size_t) ringFrames * maxMics * KitIndex::bytesPerSample, true);

    streamer = std::make_unique<Streamer> (*this);
    streamer->startThread (juce::Thread::Priority::high);
}

Engine::~Engine()
{
    streamer->stopThread (4000);
    current = nullptr;
    owned.clear();
}

//==============================================================================
void Engine::setKit (std::shared_ptr<LoadedKit> kit, const KitDefinition* definition, NoteMap map)
{
    auto loaded = std::make_unique<Loaded>();
    loaded->kit = std::move (kit);
    loaded->definition = definition;
    loaded->map = std::move (map);

    if (loaded->kit != nullptr)
        loaded->lastHits.assign (loaded->kit->index().instruments.size(), { -1, -1 });

    auto* raw = loaded.get();
    owned.push_back (std::move (loaded));

    // One the audio thread never took is nobody's: free it now
    if (auto* old = pending.exchange (raw))
        owned.erase (std::remove_if (owned.begin(), owned.end(), [old] (auto& o) { return o.get() == old; }), owned.end());
}

void Engine::releaseUnused()
{
    const int ready = retiredFifo.getNumReady();

    if (ready == 0)
        return;

    std::vector<Loaded*> done;
    const auto scope = retiredFifo.read (ready);
    scope.forEach ([&] (int index) { done.push_back (retired[(size_t) index]); });

    // Not while the streamer might be copying out of one of them
    const juce::ScopedLock sl (streamLock);

    for (auto* d : done)
        if (d != inUse.load (std::memory_order_acquire) && d != pending.load())
            owned.erase (std::remove_if (owned.begin(), owned.end(), [d] (auto& o) { return o.get() == d; }), owned.end());
}

bool Engine::beginBlock()
{
    if (auto* next = pending.exchange (nullptr))
    {
        for (auto& v : voices)
            if (v.active)
                stop (v);

        if (current != nullptr)
        {
            const auto scope = retiredFifo.write (1);
            scope.forEach ([&] (int index) { retired[(size_t) index] = current; });
        }

        current = next;
        inUse.store (current, std::memory_order_release);
    }

    return current != nullptr && current->kit != nullptr;
}

//==============================================================================
int Engine::chooseHit (const Instrument& in, int instrument, float velocity, float& gainDb)
{
    const auto& hits = in.hits;
    const int n = (int) hits.size();

    if (n == 0)
        return -1;

    // How loud this velocity wants it, on the kit's own scale (0 dB: the hardest hit recorded),
    // across the instrument's recorded range (at least 30 dB, so even one recorded hit has dynamics)
    const float range = std::max (-hits.front().level, 30.0f) * dynamics;
    const float target = -range * (1.0f - juce::jlimit (0.0f, 1.0f, velocity));

    // The three recorded hits nearest that loudness
    int lo = (int) (std::lower_bound (hits.begin(), hits.end(), target, [] (const Hit& h, float t) { return h.level < t; }) - hits.begin());
    int hi = lo;
    lo -= 1;
    int candidates[3] {}, count = 0;

    while (count < std::min (3, n))
    {
        const bool takeLow = lo >= 0 && (hi >= n || target - hits[(size_t) lo].level <= hits[(size_t) hi].level - target);
        candidates[count++] = takeLow ? lo-- : hi++;
    }

    // Not one of the last two it played, when there's another to pick
    auto& last = current->lastHits[(size_t) instrument];
    int fresh[3] {}, freshCount = 0;

    for (int i = 0; i < count; ++i)
        if (candidates[i] != last[0] && candidates[i] != last[1])
            fresh[freshCount++] = candidates[i];

    const int pick = freshCount > 0 ? fresh[random.nextInt (freshCount)] : candidates[random.nextInt (count)];
    last[1] = last[0];
    last[0] = pick;

    gainDb = juce::jlimit (-40.0f, 6.0f, target - hits[(size_t) pick].level);
    return pick;
}

Engine::Voice* Engine::freeVoice()
{
    Voice* oldest = nullptr;

    for (auto& v : voices)
    {
        if (! v.active)
            return &v;

        // Fading voices go first, then the oldest
        if (oldest == nullptr || (v.fadeStep > 0.0f && oldest->fadeStep <= 0.0f)
            || ((v.fadeStep > 0.0f) == (oldest->fadeStep > 0.0f) && v.order < oldest->order))
            oldest = &v;
    }

    stop (*oldest);
    return oldest;
}

void Engine::start (Voice& v, int instrument, int hitIndex, float gainDb, int piece)
{
    const auto& kit = *current->kit;
    const auto& in = kit.index().instruments[(size_t) instrument];
    const auto& hit = in.hits[(size_t) hitIndex];
    const int micCount = std::min (maxMics, (int) kit.index().mics.size());

    v.active = true;
    v.instrument = instrument;
    v.hitIndex = hitIndex;
    v.piece = piece;
    v.hit = &hit;
    v.position = 0;
    v.frames = hit.frames;
    v.fade = 1.0f;
    v.fadeStep = 0.0f;
    v.order = ++orderCounter;

    const float gain = juce::Decibels::decibelsToGain (gainDb, -200.0f) * hit.scale;

    for (int m = 0; m < micCount; ++m)
    {
        v.gains[m] = gain;
        v.own[m] = in.own[(size_t) m];
    }

    // Tell the streamer: what to fill, under a new sequence number (odd while it's being written)
    const auto seq = v.sequence.load (std::memory_order_relaxed);
    v.sequence.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    v.streamKit.store (&kit, std::memory_order_relaxed);
    v.streamOffset.store (hit.offset, std::memory_order_relaxed);
    v.streamFrames.store (hit.frames, std::memory_order_relaxed);
    v.consumed.store (0, std::memory_order_relaxed);
    v.progress.store ((juce::uint64) (seq + 2) << 32, std::memory_order_relaxed);
    v.sequence.store (seq + 2, std::memory_order_release);
}

void Engine::stop (Voice& v)
{
    v.active = false;

    const auto seq = v.sequence.load (std::memory_order_relaxed);
    v.sequence.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    v.streamKit.store (nullptr, std::memory_order_relaxed);
    v.streamFrames.store (0, std::memory_order_relaxed);
    v.progress.store ((juce::uint64) (seq + 2) << 32, std::memory_order_relaxed);
    v.sequence.store (seq + 2, std::memory_order_release);
}

void Engine::fadeOut (Voice& v, double seconds)
{
    const auto step = (float) (1.0 / std::max (1.0, seconds * getKitRate()));
    v.fadeStep = std::max (v.fadeStep, step);
}

//==============================================================================
void Engine::noteOn (int note, float velocity)
{
    if (current == nullptr || current->kit == nullptr || ! juce::isPositiveAndBelow (note, 128))
        return;

    const auto& target = current->map.notes[(size_t) note];

    if (target.instrument >= 0)
        play (target.instrument, velocity, target.piece);
}

void Engine::play (int instrument, float velocity, int piece)
{
    if (current == nullptr || current->kit == nullptr)
        return;

    const auto& kit = current->kit->index();

    if (! juce::isPositiveAndBelow (instrument, (int) kit.instruments.size()))
        return;

    const auto& in = kit.instruments[(size_t) instrument];
    float gainDb = 0.0f;
    const int hit = chooseHit (in, instrument, velocity, gainDb);

    if (hit < 0)
        return;

    // A hi-hat hit stops the hi-hat ringing (anything in its group); a stopped crash, its crash.
    // And no more than maxPerInstrument of one instrument at once: the oldest goes
    Voice* oldest = nullptr;
    int playing = 0;

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        const auto& other = kit.instruments[(size_t) v.instrument];

        if (in.group.isNotEmpty() && other.group == in.group)
            fadeOut (v, 0.04);

        for (int t : current->map.chokes[(size_t) instrument])
            if (v.instrument == t)
                fadeOut (v, 0.03);

        if (v.instrument == instrument && v.fadeStep <= 0.0f)
        {
            ++playing;

            if (oldest == nullptr || v.order < oldest->order)
                oldest = &v;
        }
    }

    if (playing >= maxPerInstrument && oldest != nullptr)
        fadeOut (*oldest, 0.03);

    start (*freeVoice(), instrument, hit, gainDb, piece);

    if (piece >= 0)
    {
        const auto scope = struckFifo.write (1);
        scope.forEach ([&] (int index) { struck[(size_t) index] = { piece, velocity }; });
    }
}

void Engine::stopAll (bool atOnce)
{
    for (auto& v : voices)
        if (v.active)
        {
            if (atOnce)
                stop (v);
            else
                fadeOut (v, 0.01);
        }
}

void Engine::choke (int note)
{
    if (current == nullptr || current->kit == nullptr || ! juce::isPositiveAndBelow (note, 128))
        return;

    const int instrument = current->map.notes[(size_t) note].instrument;

    for (auto& v : voices)
        if (v.active && instrument >= 0 && v.instrument == instrument)
            fadeOut (v, 0.05);
}

int Engine::popStruck (Struck* into, int most)
{
    const auto scope = struckFifo.read (std::min (most, struckFifo.getNumReady()));
    int count = 0;
    scope.forEach ([&] (int index) { into[count++] = struck[(size_t) index]; });
    return count;
}

//==============================================================================
void Engine::render (float* const* mics, int numFrames)
{
    if (current == nullptr || current->kit == nullptr || numFrames <= 0)
        return;

    const int micCount = std::min (maxMics, getMicCount());
    int active = 0;

    for (auto& v : voices)
        if (v.active)
        {
            renderVoice (v, mics, micCount, numFrames);
            active += v.active ? 1 : 0;
        }

    activeCount.store (active);
}

void Engine::renderVoice (Voice& v, float* const* mics, int micCount, int numFrames)
{
    const auto& kit = *current->kit;
    const int preload = kit.preloadFrames(), frameBytes = kit.index().frameBytes();
    const auto* start = kit.preloaded (v.instrument, v.hitIndex);

    // How far the streamer has got, once for the whole block
    int filled = 0;

    if (streaming && v.frames > preload)
    {
        const auto p = v.progress.load (std::memory_order_acquire);

        if ((juce::uint32) (p >> 32) == v.sequence.load (std::memory_order_relaxed))
            filled = (int) (juce::uint32) p;
    }

    // The gains for this block: each mic's, with the bleed applied to the ones that aren't the hit's own
    float gains[maxMics];

    for (int m = 0; m < micCount; ++m)
        gains[m] = v.gains[m] * (v.own[m] ? 1.0f : bleed);

    const int count = std::min (numFrames, v.frames - v.position);
    juce::int64 missing = 0;

    for (int i = 0; i < count; ++i)
    {
        const int f = v.position + i;
        const juce::uint8* src;

        if (f < preload)
            src = start + (size_t) f * (size_t) frameBytes;
        else if (! streaming)
            src = kit.mapped (*v.hit, f);
        else if (f - preload < filled)
            src = v.ring.get() + (size_t) ((f - preload) % ringFrames) * (size_t) frameBytes;
        else
        {
            ++missing;
            continue;
        }

        float amp = v.fade;

        if (v.fadeStep > 0.0f)
        {
            v.fade -= v.fadeStep;

            if (v.fade <= 0.0f)
            {
                v.position = v.frames;      // faded right out
                break;
            }
        }

        for (int m = 0; m < micCount; ++m)
            mics[m][i] += storedAt (src + m * KitIndex::bytesPerSample) * gains[m] * amp;
    }

    if (missing > 0)
        underruns.fetch_add (missing);

    if (v.position < v.frames)
        v.position += count;

    v.consumed.store (std::max (0, v.position - preload), std::memory_order_release);

    if (v.position >= v.frames)
        stop (v);
}

//==============================================================================
void Engine::fill (Voice& v)
{
    const auto seq = v.sequence.load (std::memory_order_acquire);

    if ((seq & 1) != 0)
        return;

    const auto* kit = v.streamKit.load (std::memory_order_relaxed);
    const auto offset = v.streamOffset.load (std::memory_order_relaxed);
    const int frames = v.streamFrames.load (std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_acquire);

    if (v.sequence.load (std::memory_order_relaxed) != seq || kit == nullptr)
        return;

    const int preload = kit->preloadFrames(), frameBytes = kit->index().frameBytes();

    if (frames <= preload)
        return;

    auto p = v.progress.load (std::memory_order_acquire);

    if ((juce::uint32) (p >> 32) != seq)
        return;

    int filled = (int) (juce::uint32) p;

    while (preload + filled < frames)
    {
        if (filled - v.consumed.load (std::memory_order_acquire) + chunkFrames > ringFrames)
            break;

        const int n = std::min (chunkFrames, frames - preload - filled);
        std::memcpy (v.ring.get() + (size_t) (filled % ringFrames) * (size_t) frameBytes,
                     kit->data() + offset + (juce::int64) (preload + filled) * frameBytes,
                     (size_t) n * (size_t) frameBytes);

        const auto next = ((juce::uint64) seq << 32) | (juce::uint32) (filled + n);

        if (! v.progress.compare_exchange_strong (p, next, std::memory_order_release, std::memory_order_relaxed))
            return;

        p = next;
        filled += n;
    }
}

} // namespace drums
