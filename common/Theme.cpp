// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Theme.h"

namespace drums::theme
{

juce::Font font (float height, bool bold)
{
   #if JUCE_MAC
    return juce::Font (juce::FontOptions ("Helvetica Neue", height, bold ? juce::Font::bold : juce::Font::plain));
   #else
    return juce::Font (juce::FontOptions ("Segoe UI", height, bold ? juce::Font::bold : juce::Font::plain));
   #endif
}

juce::Font caps (float height)
{
    return font (height, true).withExtraKerningFactor (0.08f);
}

juce::Colour micColour (int kind)
{
    switch (kind)
    {
        case 1:  return accentCool;     // overhead
        case 2:  return teal;           // room
        case 3:  return textFaint;      // trigger
        default: return accent;         // close
    }
}

} // namespace drums::theme

namespace drums
{

using namespace theme;

DrumsLookAndFeel::DrumsLookAndFeel()
{
   #if JUCE_MAC
    setDefaultSansSerifTypefaceName ("Helvetica Neue");
   #else
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #endif

    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonColourId, panelLight);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, background);
    setColour (juce::ComboBox::backgroundColourId, panelLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, edge);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, hover);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, edge);
    setColour (juce::TextEditor::backgroundColourId, panelLight);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, edge);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::CaretComponent::caretColourId, accent);
    setColour (juce::ScrollBar::thumbColourId, edge);
}

void DrumsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto colour = slider.findColour (juce::Slider::rotarySliderFillColourId);

    // The track, and the value along it (from the middle for a bipolar control: pan)
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (raised);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? (start + end) * 0.5f : start;

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (colour.withAlpha (slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // The knob, and where it points
    const float knob = radius - 6.0f;
    g.setColour (panelLight);
    g.fillEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f);
    g.setColour (edge);
    g.drawEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.25f, -knob + 2.0f, 2.5f, knob * 0.55f, 1.25f);
    g.setColour (text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void DrumsLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float, float,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto colour = slider.findColour (juce::Slider::trackColourId);

    if (style == juce::Slider::LinearVertical)
    {
        // A fader: a slot, a filled part below the cap, and the cap
        const float cx = x + width * 0.5f;
        const auto slot = juce::Rectangle<float> (cx - 2.0f, (float) y + 6.0f, 4.0f, (float) height - 12.0f);
        g.setColour (background);
        g.fillRoundedRectangle (slot, 2.0f);
        g.setColour (colour.withAlpha (0.55f));
        g.fillRoundedRectangle (slot.withTop (pos), 2.0f);

        const auto cap = juce::Rectangle<float> (cx - 11.0f, pos - 7.0f, 22.0f, 14.0f);
        g.setColour (juce::Colour (0xffc9ceda));
        g.fillRoundedRectangle (cap, 3.0f);
        g.setColour (background.withAlpha (0.7f));
        g.fillRect (cap.withSizeKeepingCentre (14.0f, 1.5f));
        return;
    }

    // Horizontal: a groove, the value, a round thumb
    const float cy = y + height * 0.5f;
    const auto groove = juce::Rectangle<float> ((float) x + 4.0f, cy - 2.0f, (float) width - 8.0f, 4.0f);
    g.setColour (raised);
    g.fillRoundedRectangle (groove, 2.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (groove.withRight (pos), 2.0f);
    g.setColour (text);
    g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

void DrumsLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : b.findColour (juce::TextButton::buttonColourId);

    if (! on && (highlighted || down))
        fill = hover;
    else if (on && (highlighted || down))
        fill = fill.brighter (0.12f);

    g.setColour (fill.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? fill.brighter (0.2f) : edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

juce::Font DrumsLookAndFeel::getTextButtonFont (juce::TextButton& b, int height)
{
    return font (juce::jlimit (12.0f, 14.0f, height * 0.55f), b.getProperties()["bold"]);
}

void DrumsLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.55f));
    const int pad = b.getWidth() < 34 ? 0 : 6;     // a one-letter button (M, S) uses all of its width
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (pad, 1), juce::Justification::centred, 1, 1.0f);
}

void DrumsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    // A switch, then its words
    const auto r = b.getLocalBounds().toFloat();
    const auto track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - 8.0f, 30.0f, 16.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (highlighted ? hover : raised));
    g.fillRoundedRectangle (track, 8.0f);
    g.setColour (on ? background : textDim);
    const float knobX = on ? track.getRight() - 14.0f : track.getX() + 2.0f;
    g.fillEllipse (knobX, track.getY() + 2.0f, 12.0f, 12.0f);

    g.setColour (b.isEnabled() ? text : textFaint);
    g.setFont (font (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (40.0f), juce::Justification::centredLeft, true);
}

void DrumsLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (box.isMouseOver (true) ? hover : panelLight);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 12.0f, ay = height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (textDim);
    g.fillPath (arrow);
}

juce::Font DrumsLookAndFeel::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jlimit (12.0f, 14.0f, box.getHeight() * 0.55f));
}

void DrumsLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

juce::Font DrumsLookAndFeel::getPopupMenuFont()                    { return font (14.0f); }
juce::Font DrumsLookAndFeel::getLabelFont (juce::Label& l)         { return l.getFont(); }

void DrumsLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (raised);
    g.setColour (edge);
    g.drawRect (0, 0, width, height);
}

void DrumsLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& t, int width, int height)
{
    g.fillAll (raised);
    g.setColour (edge);
    g.drawRect (0, 0, width, height);
    g.setColour (text);
    g.setFont (font (13.0f));
    g.drawFittedText (t, 8, 4, width - 16, height - 8, juce::Justification::centredLeft, 4, 1.0f);
}

juce::Rectangle<int> DrumsLookAndFeel::getTooltipBounds (const juce::String& t, juce::Point<int> at, juce::Rectangle<int> area)
{
    juce::AttributedString s;
    s.append (t, font (13.0f));
    juce::TextLayout layout;
    layout.createLayout (s, 320.0f);
    const int w = (int) std::ceil (layout.getWidth()) + 18, h = (int) std::ceil (layout.getHeight()) + 10;
    return juce::Rectangle<int> (at.x + 12, at.y + 18, w, h).constrainedWithin (area);
}

void DrumsLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (panelLight);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 5.0f);
}

void DrumsLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? accent : edge);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 5.0f, 1.0f);
}

void DrumsLookAndFeel::drawProgressBar (juce::Graphics& g, juce::ProgressBar&, int width, int height, double progress, const juce::String& words)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (raised);
    g.fillRoundedRectangle (r, 4.0f);

    if (progress >= 0.0)
    {
        g.setColour (accent.withAlpha (0.85f));
        g.fillRoundedRectangle (r.withWidth ((float) (width * juce::jlimit (0.0, 1.0, progress))), 4.0f);
    }

    if (words.isNotEmpty())
    {
        g.setColour (text);
        g.setFont (font (12.0f));
        g.drawText (words, r, juce::Justification::centred, true);
    }
}

void DrumsLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                      int thumbStart, int thumbSize, bool over, bool)
{
    const auto thumb = vertical ? juce::Rectangle<int> (x + width / 2 - 3, thumbStart, 6, thumbSize)
                                : juce::Rectangle<int> (thumbStart, y + height / 2 - 3, thumbSize, 6);
    g.setColour (over ? textFaint : edge);
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

} // namespace drums
