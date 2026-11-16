// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "engine/Engine.h"
#include "engine/Library.h"
#include "engine/Mixer.h"
#include "engine/Resampler.h"
#include "groove/Grooves.h"

namespace drums
{

/** Nightshift Drums: MIDI in, a multi-mic kit out. Notes go to the engine at the kit's own sample
    rate (each at the kit frame that matches its time), the mics are resampled to the host's rate
    and mixed by the kit's mixer into the main out and Out 2..9.

    The kit loads on a thread of its own; until it's ready the plugin is silent. The window plays
    notes through playNote(), and the groove player auditions what the AI drummer wrote, in time
    with the host when it's playing. */
class DrumsProcessor : public juce::AudioProcessor,
                       private juce::Timer,
                       private juce::ChangeListener
{
public:
    DrumsProcessor();
    ~DrumsProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override                    {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return "Nightshift Drums"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 8.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getState()      { return apvts; }
    Library& getLibrary()                               { return *library; }
    Engine& getEngine()                                 { return engine; }
    Mixer& getMixer()                                   { return mixer; }

    /** The kit chosen (it may still be loading, or not downloaded yet). */
    juce::String getKitId() const                       { return kitId; }
    const KitDefinition* getKitDefinition() const       { return findKit (kitId); }

    /** Chooses a kit. With `freshMix` its default mix goes on the faders (a kit picked by hand);
        without, the faders stay as they are (a session opening). */
    void chooseKit (const juce::String& id, bool freshMix);

    enum class KitState { none, notInstalled, loading, ready, failed };
    KitState getKitState() const                        { return kitState; }
    float getLoadProgress() const                       { return loadProgress.load(); }
    juce::String getKitError() const                    { return kitError; }

    /** The chosen kit's mics and instruments, as soon as it's installed (before its samples load). */
    std::shared_ptr<const KitIndex> getKitIndex() const { return kitIndex; }

    /** A note from the window: a click on the kit. */
    void playNote (int note, float velocity);

    /** The groove player: what the AI drummer wrote, auditioned through the kit (looping the
        section, or the whole groove for -1). In time with the host while it plays, else at the
        groove's own tempo. */
    void setGroove (std::optional<grooves::Groove>, int section);
    void setGroovePlaying (bool shouldPlay)             { groovePlaying = shouldPlay; }
    bool isGroovePlaying() const                        { return groovePlaying.load(); }
    float getGroovePosition() const                     { return groovePosition.load(); }   // 0..1 through the loop
    double getHostBpm() const                           { return hostBpm.load(); }

    /** Where the music is, in beats (the host's while it plays, else the groove player's), or -1 when
        nothing is playing: what a drum machine's step lights follow. */
    double getRunningBeat() const                       { return runningBeat.load(); }

    /** Every frame streamed too late since the plugin started (should stay 0). */
    juce::int64 getUnderruns() const                    { return engine.getUnderruns(); }

    /** The message thread's housekeeping (the timer calls it; tests without a message loop do too):
        picks up a loaded kit, frees old ones, follows the brushes switch and a restored session. */
    void service();

private:
    class Loader;

    static BusesProperties buses();

    juce::AudioProcessorValueTreeState apvts;
    juce::SharedResourcePointer<Library> library;
    Engine engine;
    Mixer mixer;

    // One resampler a kit rate (they're all prepared up front, so changing kits never allocates)
    struct RateResampler { double kitRate; Resampler resampler; };
    std::array<RateResampler, 4> resamplers { { { 44100.0, {} }, { 48000.0, {} }, { 88200.0, {} }, { 96000.0, {} } } };
    Resampler* active = nullptr;
    double hostRate = 48000.0;
    int maxBlock = 512;
    juce::AudioBuffer<float> hostMics, scratchOut;
    juce::MidiBuffer merged;
    std::shared_ptr<LoadedKit> loadedKit;

    // Notes waiting for their kit frame (in order)
    struct Event { juce::int64 frame; int kind; int note; float value; };    // kind: 0 note, 1 choke, 2 stop all
    std::vector<Event> events;
    size_t eventsRead = 0;

    juce::AbstractFifo previewFifo { 64 };
    struct Preview { int note = 0; float velocity = 0.0f; juce::uint32 at = 0; };      // at: when it was clicked, ms
    std::array<Preview, 64> previews {};

    // Kit
    juce::String kitId, mixFor, restoredKit;
    bool restorePending = false;
    std::atomic<KitState> kitState { KitState::none };
    std::atomic<float> loadProgress { 0.0f };
    juce::String kitError;
    std::shared_ptr<const KitIndex> kitIndex;
    std::unique_ptr<Loader> loader;
    bool brushesInMap = false, choseOnce = false;

    // Hi-hat pedal (CC4): which articulation a hi-hat note becomes
    int pedal = -1;

    // Parameters, read on the audio thread
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* bleedParam = nullptr;
    std::atomic<float>* dynamicsParam = nullptr;
    std::atomic<float>* brushesParam = nullptr;
    std::atomic<float>* hatPedalParam = nullptr;
    struct StripParams { std::atomic<float>* level; std::atomic<float>* pan; std::atomic<float>* mute; std::atomic<float>* solo;
                         std::atomic<float>* invert; std::atomic<float>* output; };
    std::array<StripParams, Mixer::maxStrips> stripParams {};

    // Grooves
    juce::SpinLock grooveLock;
    juce::MidiMessageSequence grooveNotes;
    double grooveLength = 0.0, grooveBpm = 120.0, grooveAt = 0.0;
    std::atomic<bool> groovePlaying { false };
    std::atomic<float> groovePosition { 0.0f };
    std::atomic<double> grooveLengthQ { 0.0 };     // the groove's length, beats, for the window
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<double> runningBeat { -1.0 };
    bool wasHostPlaying = false;

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void startLoading();
    void kitLoaded (std::shared_ptr<LoadedKit>, const juce::String& error);
    void applyDefaultMix (const KitIndex&);
    void updateLatency();
    Resampler* resamplerFor (double kitRate);
    int remapHiHat (int note) const;
    void addGrooveNotes (juce::MidiBuffer&, int numSamples);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsProcessor)
};

} // namespace drums
