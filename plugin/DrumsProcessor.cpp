// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "DrumsProcessor.h"
#include "DrumsEditor.h"
#include "Parameters.h"
#include "common/ScaledEditor.h"
#include "engine/Machines.h"

namespace drums
{

/** Loads a kit off the message thread: its index, and every hit's first moments into memory (a kit
    from disk), or every drum made (a drum machine). */
class DrumsProcessor::Loader : public juce::Thread
{
public:
    using Make = std::function<std::unique_ptr<LoadedKit> (juce::String& error, std::function<bool()> shouldStop, std::function<void (float)> progress)>;

    Loader (Make m, std::atomic<float>& p) : juce::Thread ("Nightshift Drums kit loader"), make (std::move (m)), progress (p) {}
    ~Loader() override      { stopThread (10000); }

    void run() override
    {
        juce::String why;
        std::shared_ptr<LoadedKit> kit (make (why, [this] { return threadShouldExit(); }, [this] (float p) { progress.store (p); }).release());
        result = std::move (kit);
        error = why;
        done = true;
    }

    std::atomic<bool> done { false };
    std::shared_ptr<LoadedKit> result;
    juce::String error;

private:
    Make make;
    std::atomic<float>& progress;
};

//==============================================================================
juce::AudioProcessor::BusesProperties DrumsProcessor::buses()
{
    // The main out, and Out 2..9 for mics given a channel of their own in the host (off until asked for)
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);

    for (int o = 2; o <= Mixer::maxOutputs; ++o)
        b = b.withOutput ("Out " + juce::String (o), juce::AudioChannelSet::stereo(), false);

    return b;
}

DrumsProcessor::DrumsProcessor()
    : juce::AudioProcessor (buses()),
      apvts (*this, nullptr, "drums", params::layout())
{
    masterParam = apvts.getRawParameterValue (params::master);
    bleedParam = apvts.getRawParameterValue (params::bleed);
    dynamicsParam = apvts.getRawParameterValue (params::dynamics);
    brushesParam = apvts.getRawParameterValue (params::brushes);
    hatPedalParam = apvts.getRawParameterValue (params::hatPedal);

    for (int i = 0; i < Mixer::maxStrips; ++i)
        stripParams[(size_t) i] = { apvts.getRawParameterValue (params::micLevel (i)), apvts.getRawParameterValue (params::micPan (i)),
                                    apvts.getRawParameterValue (params::micMute (i)), apvts.getRawParameterValue (params::micSolo (i)),
                                    apvts.getRawParameterValue (params::micInvert (i)), apvts.getRawParameterValue (params::micOutput (i)) };

    events.reserve (4096);
    library->addChangeListener (this);
    startTimerHz (20);
}

DrumsProcessor::~DrumsProcessor()
{
    stopTimer();
    library->removeChangeListener (this);
    loader = nullptr;
}

//==============================================================================
bool DrumsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (int b = 1; b < layouts.outputBuses.size(); ++b)
        if (! layouts.outputBuses[b].isDisabled() && layouts.outputBuses[b] != juce::AudioChannelSet::stereo())
            return false;

    for (auto& in : layouts.inputBuses)
        if (! in.isDisabled())
            return false;

    return true;
}

void DrumsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    maxBlock = std::max (16, samplesPerBlock);

    for (auto& r : resamplers)
        r.resampler.prepare (Engine::maxMics, r.kitRate, sampleRate, maxBlock);

    active = nullptr;
    hostMics.setSize (Engine::maxMics, maxBlock, false, true, false);
    scratchOut.setSize (2, maxBlock, false, true, false);
    merged.ensureSize (16384);
    mixer.prepare (maxBlock);
    events.clear();
    eventsRead = 0;
    updateLatency();
}

Resampler* DrumsProcessor::resamplerFor (double kitRate)
{
    for (auto& r : resamplers)
        if (std::abs (r.kitRate - kitRate) < 0.5)
            return &r.resampler;

    return nullptr;
}

void DrumsProcessor::updateLatency()
{
    if (kitIndex == nullptr)
        return;

    if (auto* rs = resamplerFor (kitIndex->sampleRate))
        setLatencySamples (juce::roundToInt (rs->getLatency()));
}

//==============================================================================
int DrumsProcessor::remapHiHat (int note) const
{
    if (pedal < 0 || hatPedalParam->load() < 0.5f || ! juce::isPositiveAndBelow (note, 128))
        return note;

    const auto* map = engine.getNoteMap();
    const auto* def = engine.getDefinition();

    if (map == nullptr || def == nullptr)
        return note;

    const auto& target = map->notes[(size_t) note];

    if (target.piece < 0 || def->pieces[(size_t) target.piece].id != "hihat")
        return note;

    const auto& piece = def->pieces[(size_t) target.piece];
    const auto& name = piece.articulations[(size_t) target.articulation].name;

    if (name != "Closed" && name != "Open" && name != "Half open")
        return note;

    auto noteOf = [&] (const char* wanted)
    {
        for (auto& a : piece.articulations)
            if (a.name == wanted && ! a.notes.empty() && map->notes[(size_t) a.notes.front()].instrument >= 0)
                return a.notes.front();

        return -1;
    };

    // The pedal down (127) closes it; part way, half open; up, open
    const int closed = noteOf ("Closed"), open = noteOf ("Open"), half = noteOf ("Half open");
    const int chosen = pedal >= 90 ? closed : pedal >= 40 ? (half >= 0 ? half : (pedal >= 65 ? closed : open)) : open;
    return chosen >= 0 ? chosen : note;
}

void DrumsProcessor::addGrooveNotes (juce::MidiBuffer& midi, int numSamples)
{
    const juce::SpinLock::ScopedTryLockType sl (grooveLock);

    if (! sl.isLocked() || ! groovePlaying.load() || grooveNotes.getNumEvents() == 0 || grooveLength <= 0.0)
    {
        wasHostPlaying = false;
        return;
    }

    double bpm = grooveBpm, startQ = grooveAt;

    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
            if (pos->getIsPlaying() && pos->getPpqPosition().hasValue() && pos->getBpm().hasValue())
            {
                bpm = *pos->getBpm();
                startQ = std::fmod (*pos->getPpqPosition(), grooveLength);

                if (startQ < 0.0)
                    startQ += grooveLength;
            }

    const double perSample = bpm / 60.0 / std::max (1.0, getSampleRate());
    const double endQ = startQ + numSamples * perSample;
    const auto tick = (double) grooves::ticksPerQuarter;

    auto emit = [&] (double fromQ, double toQ, double offsetQ)
    {
        for (int i = grooveNotes.getNextIndexAtTime (fromQ * tick); i < grooveNotes.getNumEvents(); ++i)
        {
            const auto* e = grooveNotes.getEventPointer (i);
            const double q = e->message.getTimeStamp() / tick;

            if (q >= toQ)
                break;

            if (e->message.isNoteOn())
                midi.addEvent (e->message, juce::jlimit (0, numSamples - 1, (int) ((q - fromQ + offsetQ) / perSample)));
        }
    };

    if (endQ <= grooveLength)
        emit (startQ, endQ, 0.0);
    else
    {
        emit (startQ, grooveLength, 0.0);
        emit (0.0, endQ - grooveLength, grooveLength - startQ);
    }

    grooveAt = std::fmod (endQ, grooveLength);
    groovePosition = (float) (grooveAt / grooveLength);
}

void DrumsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    double beat = -1.0;

    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (pos->getBpm().hasValue())
                hostBpm = *pos->getBpm();

            if (pos->getIsPlaying() && pos->getPpqPosition().hasValue())
                beat = *pos->getPpqPosition();
        }

    engine.setStreaming (! isNonRealtime());
    engine.setBleed (bleedParam->load() * 0.01f);
    engine.setDynamics (dynamicsParam->load() * 0.01f);

    // The host's notes and the groove player's, in a buffer of our own (sized in prepareToPlay, so
    // adding to it doesn't allocate here)
    merged.clear();
    merged.addEvents (midi, 0, -1, 0);
    addGrooveNotes (merged, numSamples);

    // Where the music is, for a drum machine's step lights: the host's beat, or the groove player's
    // when it's auditioning on its own
    if (beat < 0.0 && groovePlaying.load())
        beat = (double) groovePosition.load() * grooveLengthQ.load();

    runningBeat = beat;

    const bool hasKit = engine.beginBlock();
    auto* rs = hasKit ? resamplerFor (engine.getKitRate()) : nullptr;

    if (rs != active)
    {
        // Another kit: its own timeline from here
        if (rs != nullptr)
            rs->reset();

        active = rs;
        events.clear();
        eventsRead = 0;
    }

    if (rs == nullptr)
    {
        buffer.clear();
        previewFifo.read (previewFifo.getNumReady());
        return;
    }

    // The mixer's strips, and the outputs the host has turned on
    std::array<Mixer::Strip, Mixer::maxStrips> strips;
    bool enabled[Mixer::maxOutputs] {};
    float* outs[Mixer::maxOutputs * 2] {};
    float* scratch[2] { scratchOut.getWritePointer (0), scratchOut.getWritePointer (1) };
    const int micCount = std::min (engine.getMicCount(), Engine::maxMics);

    for (int b = 0; b < Mixer::maxOutputs; ++b)
    {
        auto bus = b < getBusCount (false) ? getBusBuffer (buffer, false, b) : juce::AudioBuffer<float>();
        enabled[b] = bus.getNumChannels() >= 2;
        outs[b * 2] = enabled[b] ? bus.getWritePointer (0) : nullptr;
        outs[b * 2 + 1] = enabled[b] ? bus.getWritePointer (1) : nullptr;
    }

    for (int i = 0; i < Mixer::maxStrips; ++i)
    {
        const auto& p = stripParams[(size_t) i];
        auto& s = strips[(size_t) i];
        const float db = p.level->load();
        s.level = db <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (db);
        s.pan = p.pan->load() * 0.01f;
        s.mute = p.mute->load() >= 0.5f;
        s.solo = p.solo->load() >= 0.5f;
        s.invert = p.invert->load() >= 0.5f;
        s.output = juce::roundToInt (p.output->load());

        if (! juce::isPositiveAndBelow (s.output, Mixer::maxOutputs) || ! enabled[s.output])
            s.output = 0;
    }

    const float masterDb = masterParam->load();
    const float master = masterDb <= -59.95f ? 0.0f : juce::Decibels::decibelsToGain (masterDb);

    // Notes from the window, at the start of the block: only clicks from just now (a host that stopped
    // calling us while its transport was stopped mustn't get every click since, all at once, later)
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        scope.forEach ([&] (int index)
        {
            const auto& p = previews[(size_t) index];

            if (events.size() < events.capacity() && now - p.at < 300)
                events.push_back ({ rs->inputFrameFor (0), 0, p.note, p.velocity });
        });
    }

    auto midiIt = merged.cbegin();

    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - start);

        // This chunk's MIDI, each note at the kit frame matching its time
        for (; midiIt != merged.cend() && (*midiIt).samplePosition < start + n; ++midiIt)
        {
            const auto m = (*midiIt).getMessage();
            const auto frame = rs->inputFrameFor (std::max (0, (*midiIt).samplePosition - start));

            if (events.size() >= events.capacity())
                break;

            if (m.isNoteOn())
                events.push_back ({ frame, 0, remapHiHat (m.getNoteNumber()), m.getFloatVelocity() });
            else if (m.isController() && m.getControllerNumber() == 4)
                pedal = m.getControllerValue();
            else if (m.isAftertouch() && m.getAfterTouchValue() >= 64)
                events.push_back ({ frame, 1, m.getNoteNumber(), 0.0f });
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                events.push_back ({ frame, 2, 0, 0.0f });
        }

        // Render the kit-rate frames this chunk needs, a note at a time
        const int need = rs->inputNeeded (n);
        rs->clearAhead (need);
        const auto base = rs->inputWritten();
        int done = 0;

        for (;;)
        {
            while (eventsRead < events.size() && events[eventsRead].frame <= base + done)
            {
                const auto& e = events[eventsRead++];

                if (e.kind == 0)
                    engine.noteOn (e.note, e.value);
                else if (e.kind == 1)
                    engine.choke (e.note);
                else
                    engine.stopAll();
            }

            if (done >= need)
                break;

            auto until = base + need;

            if (eventsRead < events.size())
                until = std::min (until, events[eventsRead].frame);

            const int length = (int) (until - (base + done));
            float* dest[Engine::maxMics];

            for (int m = 0; m < Engine::maxMics; ++m)
                dest[m] = rs->writePointer (m) + done;

            engine.render (dest, length);
            done += length;
        }

        rs->appended (need);

        if (eventsRead == events.size())
        {
            events.clear();
            eventsRead = 0;
        }

        rs->produce (hostMics.getArrayOfWritePointers(), n);

        // Mix into the outputs (the ones the host turned off point at scratch; nothing's sent there)
        float* chunk[Mixer::maxOutputs * 2];

        for (int c = 0; c < Mixer::maxOutputs * 2; ++c)
            chunk[c] = outs[c] != nullptr ? outs[c] + start : scratch[c & 1];

        mixer.process (hostMics.getArrayOfReadPointers(), micCount, strips, master, chunk, Mixer::maxOutputs, n);
    }
}

//==============================================================================
void DrumsProcessor::playNote (int note, float velocity)
{
    const auto scope = previewFifo.write (1);
    scope.forEach ([&] (int index) { previews[(size_t) index] = { note, velocity, juce::Time::getMillisecondCounter() }; });
}

void DrumsProcessor::setGroove (std::optional<grooves::Groove> groove, int section)
{
    juce::MidiMessageSequence notes;
    double length = 0.0, bpm = 120.0;

    if (groove.has_value())
    {
        bpm = grooves::tempoOf (*groove, hostBpm.load());
        notes = grooves::toMidi (*groove, bpm, section, length);
    }

    const juce::SpinLock::ScopedLockType sl (grooveLock);
    grooveNotes.swapWith (notes);
    grooveLength = length;
    grooveLengthQ = length;
    grooveBpm = bpm;
    grooveAt = 0.0;
}

//==============================================================================
void DrumsProcessor::chooseKit (const juce::String& id, bool freshMix)
{
    kitId = id;
    choseOnce = true;
    loader = nullptr;
    loadedKit = nullptr;
    kitError.clear();

    const auto* def = findKit (id);

    if (def == nullptr)
    {
        kitState = KitState::none;
        kitIndex = nullptr;
        engine.setKit (nullptr, nullptr, {});
        return;
    }

    if (! library->isReady (id))
    {
        kitState = KitState::notInstalled;
        kitIndex = nullptr;
        engine.setKit (nullptr, nullptr, {});

        if (freshMix)
            mixFor.clear();     // its own mix goes on once it's here

        return;
    }

    juce::String error;
    auto index = def->isMachine() ? std::optional<KitIndex> (machines::outline (*def)) : KitIndex::load (library->folderFor (id), error);

    if (! index.has_value())
    {
        kitState = KitState::failed;
        kitError = "The " + def->name + " kit can't be opened: " + error + ". Get it again on the Kits page.";
        kitIndex = nullptr;
        return;
    }

    kitIndex = std::make_shared<const KitIndex> (std::move (*index));

    if (freshMix || mixFor != id)
    {
        applyDefaultMix (*kitIndex);
        mixFor = id;
    }

    updateLatency();
    startLoading();
}

void DrumsProcessor::startLoading()
{
    kitState = KitState::loading;
    loadProgress = 0.0f;
    Loader::Make make;

    if (const auto* def = findKit (kitId); def != nullptr && def->isMachine())
    {
        // A drum machine: made now, in memory
        make = [def = *def] (juce::String& error, std::function<bool()> shouldStop, std::function<void (float)> progress)
        {
            return machines::build (def, Engine::preloadSeconds, error, std::move (shouldStop), std::move (progress));
        };
    }
    else
    {
        make = [folder = library->folderFor (kitId)] (juce::String& error, std::function<bool()> shouldStop, std::function<void (float)> progress)
        {
            return LoadedKit::open (folder, Engine::preloadSeconds, error, std::move (shouldStop), std::move (progress));
        };
    }

    loader = std::make_unique<Loader> (std::move (make), loadProgress);
    loader->startThread();
}

void DrumsProcessor::kitLoaded (std::shared_ptr<LoadedKit> kit, const juce::String& error)
{
    const auto* def = findKit (kitId);

    if (kit == nullptr || def == nullptr)
    {
        kitState = KitState::failed;
        kitError = "The " + (def != nullptr ? def->name : kitId) + " kit didn't load: " + error + ".";
        return;
    }

    brushesInMap = brushesParam->load() >= 0.5f;
    auto map = NoteMap::build (*def, kit->index(), brushesInMap);
    loadedKit = kit;
    engine.setKit (std::move (kit), def, std::move (map));
    kitState = KitState::ready;
}

void DrumsProcessor::applyDefaultMix (const KitIndex& kit)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    const auto* def = findKit (kit.id);

    for (int i = 0; i < Mixer::maxStrips; ++i)
    {
        const bool has = i < (int) kit.mics.size();
        const auto& mic = has ? kit.mics[(size_t) i] : Mic {};
        const float pan = has ? (def != nullptr ? picturedPan (*def, mic) : std::optional<float>()).value_or (mic.pan) : 0.0f;
        set (params::micLevel (i), has ? juce::jlimit (-60.0f, 12.0f, mic.level + kit.mixTrim) : 0.0f);
        set (params::micPan (i), juce::jlimit (-100.0f, 100.0f, pan * 100.0f));
        set (params::micMute (i), has && mic.muted ? 1.0f : 0.0f);
        set (params::micSolo (i), 0.0f);
        set (params::micInvert (i), has && mic.invert ? 1.0f : 0.0f);
        set (params::micOutput (i), 0.0f);
    }
}

void DrumsProcessor::service()
{
    engine.releaseUnused();

    if (restorePending)
    {
        restorePending = false;
        chooseKit (restoredKit, false);
    }

    // A new instance: the first kit that's here (Crocell first), its own mix on the faders
    if (! choseOnce)
    {
        juce::String first;

        for (auto& def : catalog())
            if (first.isEmpty() && library->isReady (def.id))
                first = def.id;

        chooseKit (first.isNotEmpty() ? first : catalog().front().id, true);
    }

    if (loader != nullptr && loader->done.load())
    {
        auto kit = loader->result;
        const auto error = loader->error;
        loader = nullptr;
        kitLoaded (std::move (kit), error);
    }

    // Sticks or brushes: the same kit, its notes on other instruments
    const bool brushes = brushesParam->load() >= 0.5f;

    if (kitState == KitState::ready && brushes != brushesInMap && loadedKit != nullptr)
        if (auto* def = findKit (kitId); def != nullptr && ! def->brushes.empty())
        {
            brushesInMap = brushes;
            engine.setKit (loadedKit, def, NoteMap::build (*def, loadedKit->index(), brushes));
        }
}

void DrumsProcessor::timerCallback()
{
    service();
}

void DrumsProcessor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The library changed: a kit we were waiting for may be here now
    if (kitState == KitState::notInstalled && library->isReady (kitId))
        chooseKit (kitId, mixFor != kitId);
}

//==============================================================================
void DrumsProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("kit", kitId, nullptr);
    state.setProperty ("mixFor", mixFor, nullptr);
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DrumsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            mixFor = tree.getProperty ("mixFor").toString();
            restoredKit = tree.getProperty ("kit").toString();
            restorePending = true;
            choseOnce = true;
        }
}

juce::AudioProcessorEditor* DrumsProcessor::createEditor()
{
    return new ScaledEditor (*this, new DrumsEditor (*this));
}

} // namespace drums

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new drums::DrumsProcessor();
}
