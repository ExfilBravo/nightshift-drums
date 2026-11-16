// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "DrumsEditor.h"
#include "Parameters.h"

namespace drums
{

using namespace theme;

DrumsEditor::DrumsEditor (DrumsProcessor& p) : juce::AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    kitPage = std::make_unique<KitPage> (p, [this] { showPage (3); });
    mixerPage = std::make_unique<MixerPage> (p);
    groovePage = std::make_unique<GroovePage> (p);
    kitsPage = std::make_unique<KitsPage> (p, [this] { showPage (0); });

    for (auto* c : std::initializer_list<juce::Component*> { kitPage.get(), mixerPage.get(), groovePage.get(), kitsPage.get() })
        addChildComponent (c);

    const juce::StringArray names { "Kit", "Mixer", "Grooves", "Kits" };

    for (int i = 0; i < names.size(); ++i)
    {
        auto* t = tabs.add (new juce::TextButton (names[i]));
        t->setRadioGroupId (1);
        t->setClickingTogglesState (true);
        t->setColour (juce::TextButton::buttonColourId, panel);
        t->getProperties().set ("bold", true);
        t->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (t);
    }

    kitBox.setTooltip ("The kit");
    kitBox.onChange = [this]
    {
        const int index = kitBox.getSelectedItemIndex();

        if (juce::isPositiveAndBelow (index, (int) catalog().size()) && catalog()[(size_t) index].id != processor.getKitId())
        {
            processor.chooseKit (catalog()[(size_t) index].id, true);

            if (! processor.getLibrary().isReady (processor.getKitId()))
                showPage (3);
        }
    };
    addAndMakeVisible (kitBox);
    fillKitBox();

    master.setSliderStyle (juce::Slider::LinearHorizontal);
    master.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    master.setDoubleClickReturnValue (true, 0.0);
    master.setTooltip ("Master level (the main out)");
    master.onValueChange = [this] { repaint(); };
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), params::master, master);
    addAndMakeVisible (master);
    addAndMakeVisible (left);
    addAndMakeVisible (right);

    // A kit that isn't here yet opens on the Kits page
    showPage (p.getKitState() == DrumsProcessor::KitState::notInstalled ? 3 : 0);
    setSize (designWidth, designHeight);
    startTimerHz (30);
}

DrumsEditor::~DrumsEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DrumsEditor::fillKitBox()
{
    kitBox.clear (juce::dontSendNotification);
    int id = 1;

    for (auto& def : catalog())
    {
        const bool here = processor.getLibrary().isReady (def.id);
        kitBox.addItem (def.name + (here || def.id == "testkit" ? juce::String() : "  (not here yet)"), id++);
    }

    for (size_t i = 0; i < catalog().size(); ++i)
        if (catalog()[i].id == processor.getKitId())
            kitBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);

    shownKit = processor.getKitId();
}

void DrumsEditor::showPage (int index)
{
    page = juce::jlimit (0, 3, index);
    std::array<juce::Component*, 4> pages { kitPage.get(), mixerPage.get(), groovePage.get(), kitsPage.get() };

    for (int i = 0; i < 4; ++i)
    {
        pages[(size_t) i]->setVisible (i == page);
        tabs[i]->setToggleState (i == page, juce::dontSendNotification);
    }
}

void DrumsEditor::resized()
{
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (14, 10);
    header.removeFromLeft (196);    // the name

    for (auto* t : tabs)
    {
        t->setBounds (header.removeFromLeft (84));
        header.removeFromLeft (4);
    }

    right.setBounds (header.removeFromRight (6).reduced (0, 2));
    header.removeFromRight (3);
    left.setBounds (header.removeFromRight (6).reduced (0, 2));
    header.removeFromRight (10);
    master.setBounds (header.removeFromRight (120));
    header.removeFromRight (60);    // the master's words
    kitBox.setBounds (header.removeFromRight (200));

    const auto body = getLocalBounds().withTrimmedTop (headerHeight);

    for (auto* c : std::initializer_list<juce::Component*> { kitPage.get(), mixerPage.get(), groovePage.get(), kitsPage.get() })
        c->setBounds (body);
}

void DrumsEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    auto header = getLocalBounds().removeFromTop (headerHeight);
    g.setColour (panel);
    g.fillRect (header);
    g.setColour (hairline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    auto name = header.reduced (14, 10).withWidth (190);
    g.setColour (accent);
    g.fillRoundedRectangle ((float) name.getX(), name.getY() + 4.0f, 4.0f, name.getHeight() - 8.0f, 2.0f);
    g.setColour (text);
    g.setFont (font (16.0f, true).withExtraKerningFactor (0.06f));
    g.drawText ("NIGHTSHIFT", name.withTrimmedLeft (12), juce::Justification::centredLeft, false);
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), "NIGHTSHIFT");
    g.setColour (accent);
    g.drawText ("DRUMS", name.withTrimmedLeft (12 + (int) w + 7), juce::Justification::centredLeft, false);

    g.setColour (textDim);
    g.setFont (font (12.0f));
    g.drawText (params::dbText ((float) master.getValue(), 0), master.getX() - 64, master.getY(), 60, master.getHeight(), juce::Justification::centredRight, false);
}

void DrumsEditor::timerCallback()
{
    // The page a snapshot asked for (drumstool and the standalone's --snapshot)
    if (! pageAsked)
        if (auto* parent = getParentComponent())
            if (parent->getProperties().contains ("page"))
            {
                pageAsked = true;
                const auto asked = parent->getProperties()["page"].toString();
                showPage (asked == "mixer" ? 1 : asked == "grooves" ? 2 : asked == "kits" ? 3 : 0);
            }

    if (processor.getKitId() != shownKit)
        fillKitBox();

    const float l = processor.getMixer().takeOutputPeak (0, 0), r = processor.getMixer().takeOutputPeak (0, 1);
    left.push (l);
    right.push (r);
    mixerPage->showMasterPeaks (l, r);
}

void DrumsEditor::writeGroove (const juce::String& words)
{
    groovePage->writeGroove (words, false);
}

void DrumsEditor::scrollKitsToEnd()
{
    kitsPage->scrollToEnd();
}

} // namespace drums
