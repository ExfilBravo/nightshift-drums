// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>

/** NightShift's own look, so the drums feel part of the studio: its palette, its fonts, and
    controls drawn the way NightShift draws them. Sizes here are the window's design sizes; the
    window is shown a quarter bigger (ScaledEditor), so 12 here is 15 px on screen, the least any
    word is (capitals 11.5, 14.4 px). */
namespace drums::theme
{
    inline const juce::Colour background   { 0xff08090e };
    inline const juce::Colour panel        { 0xff0f1219 };
    inline const juce::Colour panelLight   { 0xff151922 };
    inline const juce::Colour raised       { 0xff1c212c };
    inline const juce::Colour hover        { 0xff252b38 };
    inline const juce::Colour hairline     { 0x14ffffff };
    inline const juce::Colour edge         { 0x22ffffff };
    inline const juce::Colour text         { 0xffe9ecf4 };
    inline const juce::Colour textDim      { 0xffa3aabc };
    inline const juce::Colour textFaint    { 0xff8a92a9 };
    inline const juce::Colour accent       { 0xffffb054 };     // amber
    inline const juce::Colour accentCool   { 0xff8f9bff };     // periwinkle
    inline const juce::Colour teal         { 0xff5fd0c5 };
    inline const juce::Colour ledOn        { 0xff6fdc8c };
    inline const juce::Colour danger       { 0xffff5a6a };

    juce::Font font (float height, bool bold = false);

    /** Small capitals for captions: BLEED, MASTER. */
    juce::Font caps (float height = 11.5f);

    /** The colour for a mic by its kind: close mics amber, overheads periwinkle, room teal. */
    juce::Colour micColour (int kind);
}

namespace drums
{

class DrumsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DrumsLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawProgressBar (juce::Graphics&, juce::ProgressBar&, int width, int height, double progress, const juce::String&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool down) override;
};

} // namespace drums
