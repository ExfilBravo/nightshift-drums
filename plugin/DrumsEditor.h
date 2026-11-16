// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "DrumsProcessor.h"
#include "Pages.h"
#include "common/Theme.h"

namespace drums
{

/** Nightshift Drums' window, as designed (1040 x 660; ScaledEditor shows it a quarter bigger):
    the name, four pages (Kit, Mixer, Grooves, Kits), the kit, the master level and its meter. */
class DrumsEditor : public juce::AudioProcessorEditor,
                    public juce::DragAndDropContainer,
                    private juce::Timer
{
public:
    explicit DrumsEditor (DrumsProcessor&);
    ~DrumsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth = 1040, designHeight = 660, headerHeight = 52;

    void showPage (int);

    /** A groove on the Grooves page, as if typed there (for drumstool's snapshots). */
    void writeGroove (const juce::String& words);

private:
    DrumsProcessor& processor;
    DrumsLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::OwnedArray<juce::TextButton> tabs;
    juce::ComboBox kitBox;
    juce::Slider master;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    LevelMeter left, right;
    std::unique_ptr<KitPage> kitPage;
    std::unique_ptr<MixerPage> mixerPage;
    std::unique_ptr<GroovePage> groovePage;
    std::unique_ptr<KitsPage> kitsPage;
    int page = 0;
    bool pageAsked = false;
    juce::String shownKit;

    void timerCallback() override;
    void fillKitBox();
};

} // namespace drums
