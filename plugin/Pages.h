// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "DrumsProcessor.h"
#include "KitView.h"

namespace drums
{

/** A level meter: a thin bar with a falling peak, fed peaks (as gain) 30 times a second. */
class LevelMeter : public juce::Component
{
public:
    void push (float peak);
    void paint (juce::Graphics&) override;

private:
    float level = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};

/** A mic's name as an engineer would write it on the console tape: "KDrumOutside" -> "Kick out".
    `turnedRound`: the kit names its overheads and room from the audience's side, so their left and right swap. */
juce::String friendlyMicName (const juce::String&, bool turnedRound);

//==============================================================================
/** The kit, to play and to watch, with its feel controls under it. */
class KitPage : public juce::Component
{
public:
    KitPage (DrumsProcessor&, std::function<void()> showKits);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    DrumsProcessor& processor;
    KitView view;
    juce::Slider bleed, dynamics;
    juce::ToggleButton brushes { "Brushes" }, hatPedal { "Hi-hat pedal (CC4) opens the hi-hat" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bleedAttachment, dynamicsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> brushesAttachment, hatPedalAttachment;
    juce::String line;
    juce::Rectangle<int> strip;
};

//==============================================================================
/** The kit's mixer: a strip a mic, and the master. */
class MixerPage : public juce::Component,
                  private juce::Timer
{
public:
    explicit MixerPage (DrumsProcessor&);
    ~MixerPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

    /** The main out's peaks (the window takes them from the mixer once a frame and shares them). */
    void showMasterPeaks (float left, float right);

private:
    struct Strip;
    DrumsProcessor& processor;
    juce::OwnedArray<Strip> strips;
    juce::Slider master;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    LevelMeter masterLeft, masterRight;
    std::shared_ptr<const KitIndex> shownIndex;
    int shownCount = -1;

    void timerCallback() override;
    void refresh();
};

//==============================================================================
/** Grooves from the AI drummer: pick a style or say what you want, audition it on the kit,
    drag it into the song. */
class GroovePage : public juce::Component,
                   private juce::Timer
{
public:
    explicit GroovePage (DrumsProcessor&);
    ~GroovePage() override;

    /** Writes a groove for these words, as if they'd been typed (drumstool's snapshots too). */
    void writeGroove (const juce::String& words, bool newTake);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    DrumsProcessor& processor;
    juce::OwnedArray<juce::TextButton> styleButtons, sectionButtons;
    juce::TextEditor prompt;
    juce::TextButton write { "Write" }, again { "Another take" }, playButton { "Play" };
    std::optional<grooves::Written> written;
    juce::String lastPrompt;
    int seed = 1, section = -1;
    juce::Rectangle<int> gridArea, dragArea, replyArea, leftArea;
    bool dragging = false;

    void timerCallback() override;
    void chooseSection (int);
    juce::File midiFile() const;
};

//==============================================================================
/** The kits: what's here, what isn't, and getting them. */
class KitsPage : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    KitsPage (DrumsProcessor&, std::function<void()> showKit);
    ~KitsPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Row;
    DrumsProcessor& processor;
    std::function<void()> showKit;
    juce::OwnedArray<Row> rows;
    juce::TextButton changeFolder { "Change folder" }, licences { "Licences" };
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::Component> licenceView;
    juce::Rectangle<int> footer;

    void showLicences();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace drums
