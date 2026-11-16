// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "DrumsProcessor.h"

namespace drums
{

/** A kit's picture, its photo (cached) or the kit drawn, filling `area` (which should have the
    picture's proportions). */
void drawKitPicture (juce::Graphics&, const KitDefinition&, juce::Rectangle<float> area);

/** A kit's photo, decoded once and kept; invalid for a drawn kit. */
juce::Image kitPhoto (const KitDefinition&);

/** The kit, as it looks: the photo of the very kit you hear (or, for a kit without a free photo yet,
    the kit drawn). Every drum and cymbal lights up when it plays, and plays when it's clicked: a
    hi-hat opens at its edge, a snare's rim is a rimshot, a ride's bell is its bell. Right-click a
    piece for all its articulations. */
class KitView : public juce::Component,
                private juce::Timer
{
public:
    explicit KitView (DrumsProcessor&);
    ~KitView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** What's under the mouse, or what just played, in words: for the line under the kit. */
    std::function<void (const juce::String&)> onTell;

    /** "Get this kit": to the Kits page. */
    std::function<void()> onGetKit;

private:
    DrumsProcessor& processor;
    juce::String shownKit;
    const KitDefinition* def = nullptr;
    juce::Image picture, lit;
    juce::Rectangle<float> area;            // where the kit's picture space is drawn
    float scale = 1.0f;
    std::vector<float> flash;
    int hovered = -1, hoveredArticulation = -1;
    juce::Point<float> mouse;
    juce::TextButton getButton { "Get this kit" };

    void timerCallback() override;
    void refreshKit();
    juce::Path pathOf (int piece) const;
    int pieceAt (juce::Point<float>, int& articulation) const;
    void play (int piece, int articulation, float velocity);
    juce::String describe (int piece, int articulation) const;
    void drawKit (juce::Graphics&);
    void drawLights (juce::Graphics&);
    void drawState (juce::Graphics&);
};

} // namespace drums
