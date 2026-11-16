#pragma once

#include <JuceHeader.h>

namespace drums
{

/** The plugin's window: the editor as it's designed (1040 x 660), shown a quarter bigger as a whole
    so its words come out at 15 px and up (the user, 2026-11-15: "make all of the text more readable"),
    its art and spacing keeping their balance. The window can be resized (its corner), keeping its
    proportions, from 0.8 to 2 times the design; it opens at 1.25. createEditor returns one of these
    around the designed editor; it's an ordinary editor to the host. */
class ScaledEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr float scale = 1.25f;

    ScaledEditor (juce::AudioProcessor& p, juce::AudioProcessorEditor* designed)
        : juce::AudioProcessorEditor (p), inner (designed)
    {
        addAndMakeVisible (*inner);
        designWidth = inner->getWidth();
        designHeight = inner->getHeight();
        setResizable (true, true);
        setResizeLimits (juce::roundToInt (designWidth * 0.8f), juce::roundToInt (designHeight * 0.8f),
                         designWidth * 2, designHeight * 2);
        getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        setSize (juce::roundToInt ((float) designWidth * scale), juce::roundToInt ((float) designHeight * scale));
    }

    void resized() override
    {
        inner->setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth));
        inner->setTopLeftPosition (0, 0);
    }

private:
    std::unique_ptr<juce::AudioProcessorEditor> inner;
    int designWidth = 1, designHeight = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScaledEditor)
};

} // namespace drums
