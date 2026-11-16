// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Pages.h"
#include "Parameters.h"
#include "common/Theme.h"
#include "groove/Request.h"
#include "BinaryData.h"

namespace drums
{

using namespace theme;

namespace
{
    const juce::String dot = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"));

    void caption (juce::Graphics& g, const juce::String& words, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (textFaint);
        g.setFont (caps());
        g.drawText (words.toUpperCase(), r, j, true);
    }

    void setUpKnob (juce::Slider& s, juce::Colour colour = accent)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setColour (juce::Slider::rotarySliderFillColourId, colour);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    }
}

//==============================================================================
void LevelMeter::push (float peak)
{
    level = std::max (peak, level * 0.82f);

    if (peak >= hold)
    {
        hold = peak;
        holdFrames = 30;
    }
    else if (--holdFrames < 0)
        hold *= 0.92f;

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (background);
    g.fillRoundedRectangle (r, 2.0f);

    auto toY = [&] (float gain)
    {
        const float db = juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (gain, -60.0f));
        return r.getBottom() - r.getHeight() * (db + 60.0f) / 66.0f;
    };

    const float y = toY (level);
    juce::ColourGradient fill (ledOn, 0.0f, r.getBottom(), danger, 0.0f, r.getY(), false);
    fill.addColour (0.82, accent);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r.withTop (y), 2.0f);

    if (hold > 0.001f)
    {
        g.setColour (hold >= 1.0f ? danger : text.withAlpha (0.8f));
        g.fillRect (r.getX(), toY (hold) - 1.0f, r.getWidth(), 2.0f);
    }
}

juce::String friendlyMicName (const juce::String& raw, bool turnedRound)
{
    const auto n = raw.toLowerCase().removeCharacters ("_- ");
    auto side = [&] (bool flip) -> juce::String
    {
        if (n.endsWith ("center") || n.endsWith ("centre") || n.endsWith ("mid"))
            return " C";

        const bool left = n.endsWith ("left") || n.endsWith ("l"), right = n.endsWith ("right") || n.endsWith ("r");
        return left ? (flip ? " R" : " L") : right ? (flip ? " L" : " R") : "";
    };

    // Overheads and room by where they are in the mix, as the drummer sits (a kit named from the
    // audience's side has them the other way round)
    if (n.startsWith ("amb") || n.startsWith ("room"))     return juce::String ("Room") + side (turnedRound);
    if (n.startsWith ("oh"))                                return juce::String ("OH") + side (turnedRound);
    if (n.contains ("trigger"))                             return "Trigger";
    if (n.contains ("kdrum") || n.contains ("kick"))
    {
        if (n.contains ("inside"))                          return "Kick in";
        if (n.contains ("outside"))                         return "Kick out";
        if (n.contains ("back"))                            return "Kick back";
        if (n.contains ("front"))                           return "Kick front";
        return juce::String ("Kick") + side (false);
    }
    if (n.contains ("snare"))                               return n.contains ("bottom") ? "Snare bottom" : "Snare top";
    if (n.contains ("hihat"))                               return "Hi-hat";
    if (n.startsWith ("ride"))                              return juce::String ("Ride") + side (false);
    if (n.startsWith ("ftom"))                              return "Floor " + n.fromFirstOccurrenceOf ("ftom", false, false);
    if (n.startsWith ("tom"))                               return "Tom " + n.fromFirstOccurrenceOf ("tom", false, false);
    return raw;
}

//==============================================================================
KitPage::KitPage (DrumsProcessor& p, std::function<void()> showKits) : processor (p), view (p)
{
    addAndMakeVisible (view);
    view.onGetKit = std::move (showKits);
    view.onTell = [this] (const juce::String& words) { line = words; repaint (strip); };

    for (auto* s : { &bleed, &dynamics })
    {
        setUpKnob (*s);
        addAndMakeVisible (*s);
    }

    bleed.setTooltip ("Bleed: how much each drum is heard in the other drums' close mics (100 % as recorded). The overheads and room always hear everything.");
    dynamics.setTooltip ("Dynamics: how far velocity moves the loudness, across the kit's recorded hits (100 % as recorded).");
    bleedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), params::bleed, bleed);
    dynamicsAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), params::dynamics, dynamics);

    addAndMakeVisible (brushes);
    addAndMakeVisible (hatPedal);
    brushes.setTooltip ("Play the kit with brushes instead of sticks (the DRS kit was recorded with both).");
    hatPedal.setTooltip ("With an electronic kit's hi-hat pedal (MIDI CC 4), the hi-hat note plays closed, half open or open as the pedal is.");
    brushesAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), params::brushes, brushes);
    hatPedalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.getState(), params::hatPedal, hatPedal);
}

void KitPage::resized()
{
    auto r = getLocalBounds();
    strip = r.removeFromBottom (70);
    view.setBounds (r);

    auto s = strip.reduced (14, 8);
    bleed.setBounds (s.getX() + 54, s.getY() + 4, 46, 46);
    dynamics.setBounds (s.getX() + 196, s.getY() + 4, 46, 46);
    brushes.setBounds (s.getX() + 280, s.getY() + 4, 120, 24);
    hatPedal.setBounds (s.getX() + 280, s.getY() + 28, 300, 24);
}

void KitPage::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRect (strip);
    g.setColour (hairline);
    g.drawHorizontalLine (strip.getY(), 0.0f, (float) getWidth());

    auto s = strip.reduced (14, 8);
    caption (g, "Bleed", { s.getX(), s.getY() + 4, 52, 16 });
    g.setColour (text);
    g.setFont (font (13.0f));
    g.drawText (juce::String (juce::roundToInt (bleed.getValue())) + " %", s.getX(), s.getY() + 22, 52, 18, juce::Justification::centredLeft, false);
    caption (g, "Dynamics", { s.getX() + 106, s.getY() + 4, 88, 16 }, juce::Justification::centredRight);
    g.drawText (juce::String (juce::roundToInt (dynamics.getValue())) + " %", s.getX() + 106, s.getY() + 22, 86, 18, juce::Justification::centredRight, false);

    const auto* def = processor.getKitDefinition();
    brushes.setEnabled (def != nullptr && ! def->brushes.empty());

    // What's under the mouse, or how to play it
    auto words = s.withTrimmedLeft (600);
    g.setColour (line.isNotEmpty() ? text : textDim);
    g.setFont (font (13.0f));
    g.drawFittedText (line.isNotEmpty() ? line : "Click a drum to play it; Shift for softer. A hi-hat opens at its edge, a snare's rim is a rimshot. "
                                                 "Right-click for every way to play it.",
                      words, juce::Justification::centredLeft, 3, 1.0f);
}

//==============================================================================
struct MixerPage::Strip : public juce::Component, public juce::SettableTooltipClient
{
    Strip (DrumsProcessor& p, int i) : index (i)
    {
        auto& state = p.getState();
        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setDoubleClickReturnValue (true, 0.0);
        setUpKnob (pan, accentCool);
        pan.setDoubleClickReturnValue (true, 0.0);
        output.addItemList (params::outputNames(), 1);
        output.setTooltip ("Where this mic goes: the main out, or an output of its own (turn on Out 2..9 in your host)");

        for (auto* b : { &mute, &solo, &invert })
        {
            b->setClickingTogglesState (true);
            b->getProperties().set ("bold", true);
            addAndMakeVisible (*b);
        }

        mute.setColour (juce::TextButton::buttonOnColourId, danger);
        solo.setColour (juce::TextButton::buttonOnColourId, accent);
        invert.setColour (juce::TextButton::buttonOnColourId, accentCool);
        mute.setTooltip ("Mute");
        solo.setTooltip ("Solo");
        invert.setTooltip ("Flip the polarity");

        addAndMakeVisible (fader);
        addAndMakeVisible (pan);
        addAndMakeVisible (output);
        addAndMakeVisible (meter);

        faderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::micLevel (i), fader);
        panAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, params::micPan (i), pan);
        muteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::micMute (i), mute);
        soloAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::micSolo (i), solo);
        invertAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, params::micInvert (i), invert);
        outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, params::micOutput (i), output);
        fader.onValueChange = [this] { repaint(); };
        pan.onValueChange = [this] { repaint(); };
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (3, 0);
        r.removeFromTop (40);       // colour and name
        output.setBounds (r.removeFromTop (22));
        r.removeFromTop (6);
        pan.setBounds (r.removeFromTop (36).withSizeKeepingCentre (36, 36));
        r.removeFromTop (16);       // pan value
        auto buttons = r.removeFromTop (20);
        const int w = buttons.getWidth() / 3;
        mute.setBounds (buttons.removeFromLeft (w).reduced (1, 0));
        solo.setBounds (buttons.removeFromLeft (w).reduced (1, 0));
        invert.setBounds (buttons.reduced (1, 0));
        r.removeFromTop (8);
        auto bottom = r.removeFromBottom (18);
        juce::ignoreUnused (bottom);
        meter.setBounds (r.removeFromRight (6).reduced (0, 6));
        fader.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (colour);
        g.fillRect (r.getX() + 4, 0, r.getWidth() - 8, 3);

        g.setColour (text);
        g.setFont (font (12.0f, true));
        g.drawFittedText (name, r.getX() + 2, 5, r.getWidth() - 4, 32, juce::Justification::centred, 2, 1.0f);

        g.setColour (textDim);
        g.setFont (font (12.0f));
        g.drawText (params::panText ((float) pan.getValue(), 0), pan.getX() - 10, pan.getBottom(), pan.getWidth() + 20, 16, juce::Justification::centred, false);
        g.setColour (text);
        g.drawText (params::dbText ((float) fader.getValue(), 0).upToFirstOccurrenceOf (" dB", false, false),
                    r.getX(), r.getBottom() - 18, r.getWidth(), 16, juce::Justification::centred, false);
    }

    int index;
    juce::String name;
    juce::Colour colour = accent;
    juce::Slider fader, pan;
    juce::ComboBox output;
    juce::TextButton mute { "M" }, solo { "S" }, invert { juce::String (juce::CharPointer_UTF8 ("\xc3\x98")) };
    LevelMeter meter;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> faderAttachment, panAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttachment, soloAttachment, invertAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> outputAttachment;
};

MixerPage::MixerPage (DrumsProcessor& p) : processor (p)
{
    for (int i = 0; i < Mixer::maxStrips; ++i)
        addChildComponent (strips.add (new Strip (p, i)));

    master.setSliderStyle (juce::Slider::LinearVertical);
    master.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    master.setDoubleClickReturnValue (true, 0.0);
    master.onValueChange = [this] { repaint(); };
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.getState(), params::master, master);
    addAndMakeVisible (master);
    addAndMakeVisible (masterLeft);
    addAndMakeVisible (masterRight);
    refresh();
    startTimerHz (30);
}

MixerPage::~MixerPage()
{
    stopTimer();
}

void MixerPage::refresh()
{
    shownIndex = processor.getKitIndex();
    shownCount = shownIndex != nullptr ? std::min ((int) shownIndex->mics.size(), Mixer::maxStrips) : 0;

    for (int i = 0; i < strips.size(); ++i)
    {
        auto* s = strips[i];
        s->setVisible (i < shownCount);

        if (i < shownCount)
        {
            const auto& mic = shownIndex->mics[(size_t) i];
            s->name = friendlyMicName (mic.name, shownIndex->namedFromAudience);
            s->colour = micColour ((int) mic.kind);
            s->setTooltip (mic.name + "  " + dot + "  " + Mic::kindName (mic.kind) + " mic");
        }
    }

    resized();
    repaint();
}

void MixerPage::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    auto masterArea = r.removeFromRight (84);
    masterArea.removeFromTop (40);
    masterArea.removeFromBottom (20);
    masterRight.setBounds (masterArea.removeFromRight (7).reduced (0, 6));
    masterArea.removeFromRight (3);
    masterLeft.setBounds (masterArea.removeFromRight (7).reduced (0, 6));
    master.setBounds (masterArea.withTrimmedLeft (14));
    r.removeFromRight (10);

    const int count = std::max (1, shownCount);
    const int width = std::min (62, r.getWidth() / count);

    for (int i = 0; i < strips.size(); ++i)
        if (i < shownCount)
            strips[i]->setBounds (r.getX() + i * width, r.getY(), width - 4, r.getHeight());
}

void MixerPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
    auto r = getLocalBounds().reduced (12, 10);
    auto masterArea = r.removeFromRight (84);
    g.setColour (panel);
    g.fillRoundedRectangle (masterArea.toFloat(), 4.0f);
    g.setColour (text);
    g.setFont (font (12.0f, true));
    g.drawText ("Master", masterArea.withHeight (36), juce::Justification::centred, false);
    g.setFont (font (12.0f));
    g.drawText (params::dbText ((float) master.getValue(), 0).upToFirstOccurrenceOf (" dB", false, false),
                masterArea.withTop (masterArea.getBottom() - 18), juce::Justification::centred, false);

    if (shownCount == 0)
    {
        g.setColour (textDim);
        g.setFont (font (14.0f));
        g.drawText ("The mixer shows the kit's mics once a kit is here.", r, juce::Justification::centred, true);
    }
}

void MixerPage::timerCallback()
{
    if (processor.getKitIndex() != shownIndex)
        refresh();

    auto& mixer = processor.getMixer();

    for (int i = 0; i < shownCount; ++i)
        strips[i]->meter.push (mixer.takeMicPeak (i));
}

void MixerPage::showMasterPeaks (float leftPeak, float rightPeak)
{
    masterLeft.push (leftPeak);
    masterRight.push (rightPeak);
}

//==============================================================================
GroovePage::GroovePage (DrumsProcessor& p) : processor (p)
{
    for (auto& style : nightshift::agents::styles())
    {
        auto* b = styleButtons.add (new juce::TextButton (style.name));
        b->setTooltip ("A " + juce::String (style.name) + " groove, " + juce::String (style.slowest) + " to " + juce::String (style.fastest) + " bpm");
        b->onClick = [this, name = juce::String (style.name)] { prompt.setText (name, false); writeGroove (name, false); };
        addAndMakeVisible (b);
    }

    prompt.setFont (font (14.0f));
    prompt.setTextToShowWhenEmpty ("Or say it: funk, busy hats, 96 bpm", textFaint);
    prompt.setIndents (8, 7);
    prompt.onReturnKey = [this] { writeGroove (prompt.getText(), false); };
    addAndMakeVisible (prompt);

    write.onClick = [this] { writeGroove (prompt.getText(), false); };
    again.onClick = [this] { writeGroove (lastPrompt, true); };
    playButton.onClick = [this]
    {
        const bool play = ! processor.isGroovePlaying();
        processor.setGroovePlaying (play && written.has_value());
        playButton.setButtonText (processor.isGroovePlaying() ? "Stop" : "Play");
    };

    write.setToggleState (true, juce::dontSendNotification);
    write.setClickingTogglesState (false);

    for (auto* b : { &write, &again, &playButton })
        addAndMakeVisible (b);

    again.setEnabled (false);
    playButton.setEnabled (false);
    startTimerHz (20);
}

GroovePage::~GroovePage()
{
    stopTimer();
    processor.setGroovePlaying (false);
}

juce::File GroovePage::midiFile() const
{
    auto name = written.has_value() ? written->groove.title : juce::String ("Groove");
    name = juce::File::createLegalFileName (name.replace (",", "").replace (" ", "_"));
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Nightshift Drums").getChildFile (name + ".mid");
}

void GroovePage::writeGroove (const juce::String& words, bool newTake)
{
    if (words.trim().isEmpty())
        return;

    if (newTake)
        ++seed;

    lastPrompt = words;
    const double bpm = processor.getHostBpm() > 0.0 ? processor.getHostBpm() : 120.0;
    written = grooves::write (words, bpm, 4, 4, seed);

    sectionButtons.clear();

    if (written->groove.sections.size() > 1)
    {
        auto* whole = sectionButtons.add (new juce::TextButton ("Whole groove"));
        whole->onClick = [this] { chooseSection (-1); };
        addAndMakeVisible (whole);
    }

    for (size_t s = 0; s < written->groove.sections.size(); ++s)
    {
        auto* b = sectionButtons.add (new juce::TextButton (written->groove.sections[s].name));
        b->onClick = [this, s] { chooseSection ((int) s); };
        addAndMakeVisible (b);
    }

    again.setEnabled (true);
    playButton.setEnabled (true);
    chooseSection (written->groove.sections.size() > 1 ? -1 : 0);
    resized();
    repaint();
}

void GroovePage::chooseSection (int s)
{
    section = s;

    for (int i = 0; i < sectionButtons.size(); ++i)
    {
        const int index = written->groove.sections.size() > 1 ? i - 1 : i;
        sectionButtons[i]->setToggleState (index == section, juce::dontSendNotification);
    }

    processor.setGroove (written->groove, section);
    const double bpm = grooves::tempoOf (written->groove, processor.getHostBpm());
    midiFile().getParentDirectory().createDirectory();
    grooves::saveMidiFile (written->groove, bpm, section, midiFile());
    repaint();
}

void GroovePage::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    leftArea = r.removeFromLeft (330);
    auto left = leftArea;
    left.removeFromTop (24);        // caption

    // The styles, two to a row
    const int rowsNeeded = (styleButtons.size() + 1) / 2;

    for (int i = 0; i < styleButtons.size(); ++i)
        styleButtons[i]->setBounds (left.getX() + (i % 2) * 166, left.getY() + (i / 2) * 30, 160, 26);

    left.removeFromTop (rowsNeeded * 30 + 14);
    left.removeFromTop (22);        // caption
    prompt.setBounds (left.removeFromTop (34));
    left.removeFromTop (8);
    auto buttons = left.removeFromTop (30);
    write.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    again.setBounds (buttons.removeFromLeft (130));

    r.removeFromLeft (24);
    auto right = r;
    replyArea = right.removeFromTop (88);
    auto sections = right.removeFromTop (30);

    for (auto* b : sectionButtons)
    {
        const int w = std::max (70, (int) juce::GlyphArrangement::getStringWidth (font (13.0f), b->getButtonText()) + 26);
        b->setBounds (sections.removeFromLeft (w).withHeight (26));
        sections.removeFromLeft (6);
    }

    right.removeFromTop (12);
    auto transport = right.removeFromBottom (40);
    playButton.setBounds (transport.removeFromLeft (96).withHeight (32));
    transport.removeFromLeft (14);
    dragArea = transport.removeFromLeft (250).withHeight (32);
    right.removeFromBottom (10);
    gridArea = right;
}

void GroovePage::paint (juce::Graphics& g)
{
    g.fillAll (background);
    caption (g, "Styles", { leftArea.getX(), leftArea.getY(), 200, 18 });
    const int rowsNeeded = (styleButtons.size() + 1) / 2;
    caption (g, "Or say what you want", { leftArea.getX(), leftArea.getY() + 24 + rowsNeeded * 30 + 14, 300, 18 });

    if (! written.has_value())
    {
        g.setColour (textDim);
        g.setFont (font (14.0f));
        g.drawFittedText ("NightShift's AI drummer writes a groove on the spot, for the style you pick or the words you type: "
                          "sections, fills and its feel (swing, the snare a little late or early). It plays here on the kit, "
                          "in time with your song, and drags into it as MIDI. It all happens on this computer.",
                          gridArea.withTop (replyArea.getY()).reduced (8, 0).withHeight (120), juce::Justification::topLeft, 6, 1.0f);
        return;
    }

    const auto& gr = written->groove;
    g.setColour (text);
    g.setFont (font (17.0f, true));
    g.drawText (gr.title, replyArea.withHeight (26), juce::Justification::centredLeft, true);
    g.setColour (textDim);
    g.setFont (font (13.0f));
    g.drawFittedText (written->reply, replyArea.withTrimmedTop (28), juce::Justification::topLeft, 3, 1.0f);

    // The section as a step grid: a lane a drum, a cell a step, brighter for harder
    const auto* sec = juce::isPositiveAndBelow (section, (int) gr.sections.size()) ? &gr.sections[(size_t) section]
                                                                                   : (gr.sections.empty() ? nullptr : &gr.sections.front());

    if (sec == nullptr || sec->bars.empty())
        return;

    juce::StringArray voices;

    for (auto& bar : sec->bars)
        for (auto& lane : bar.lanes)
            if (lane.hits.containsAnyOf ("123456789gfrGFR"))
                voices.addIfNotAlreadyThere (lane.voice);

    const int bars = std::min (2, (int) sec->bars.size());
    int steps = 0;

    for (int b = 0; b < bars; ++b)
        steps += sec->bars[(size_t) b].steps;

    auto grid = gridArea;
    caption (g, sec->name + "  " + dot + "  its first " + juce::String (bars) + (bars == 1 ? " bar" : " bars"), grid.removeFromTop (20));
    grid.removeFromTop (4);
    const int labelWidth = 90;
    const float laneHeight = std::min (24.0f, (float) grid.getHeight() / std::max (1, voices.size()));
    const float cell = (float) (grid.getWidth() - labelWidth) / std::max (1, steps);

    for (int v = 0; v < voices.size(); ++v)
    {
        const float y = grid.getY() + v * laneHeight;
        g.setColour (textDim);
        g.setFont (font (12.0f));
        static const std::map<juce::String, juce::String> names {
            { "kick", "Kick" }, { "snare", "Snare" }, { "snare_rim", "Rimshot" }, { "sidestick", "Side stick" }, { "clap", "Clap" },
            { "hat_closed", "Hi-hat" }, { "hat_pedal", "Hat pedal" }, { "hat_open", "Open hat" }, { "tom_high", "High tom" },
            { "tom_mid", "Mid tom" }, { "tom_floor", "Floor tom" }, { "ride", "Ride" }, { "ride_bell", "Ride bell" },
            { "crash", "Crash" }, { "crash_2", "Crash 2" }, { "china", "China" }, { "splash", "Splash" } };
        const auto it = names.find (voices[v]);
        g.drawText (it != names.end() ? it->second : voices[v].replace ("_", " "), grid.getX(), (int) y, labelWidth - 6, (int) laneHeight,
                    juce::Justification::centredLeft, true);

        int stepAt = 0;

        for (int b = 0; b < bars; ++b)
        {
            const auto& bar = sec->bars[(size_t) b];
            const nightshift::drummer::Lane* lane = nullptr;

            for (auto& l : bar.lanes)
                if (l.voice == voices[v])
                    lane = &l;

            for (int st = 0; st < bar.steps; ++st, ++stepAt)
            {
                const auto x = (float) (grid.getX() + labelWidth) + stepAt * cell;
                const auto c = lane != nullptr && st < lane->hits.length() ? lane->hits[st] : '.';
                const bool beat = st % std::max (1, bar.steps / 4) == 0;
                auto box = juce::Rectangle<float> (x + 1.0f, y + 2.0f, cell - 2.0f, laneHeight - 4.0f);
                g.setColour (beat ? raised : panelLight);
                g.fillRoundedRectangle (box, 2.0f);

                if (c != '.' && c != '-' && c != '0')
                {
                    const float strength = (float) nightshift::drummer::velocityFor (c) / 127.0f;
                    g.setColour (accent.withAlpha (0.25f + 0.75f * strength));
                    g.fillRoundedRectangle (box, 2.0f);
                }
            }
        }
    }

    // Where the player is, over the shown bars
    if (processor.isGroovePlaying() && juce::isPositiveAndBelow (section, (int) gr.sections.size()))
    {
        const float sectionBars = (float) sec->bars.size();
        const float at = processor.getGroovePosition() * sectionBars;

        if (at < (float) bars)
        {
            const float x = (float) (grid.getX() + labelWidth) + at / (float) bars * (grid.getWidth() - labelWidth);
            g.setColour (text.withAlpha (0.8f));
            g.fillRect (x, (float) grid.getY(), 2.0f, voices.size() * laneHeight);
        }
    }

    // The MIDI to drag into the song
    g.setColour (dragging ? hover : panelLight);
    g.fillRoundedRectangle (dragArea.toFloat(), 5.0f);
    g.setColour (edge);
    g.drawRoundedRectangle (dragArea.toFloat().reduced (0.5f), 5.0f, 1.0f);
    g.setColour (text);
    g.setFont (font (13.0f));
    g.drawText ("Drag the MIDI into your song", dragArea.reduced (10, 0), juce::Justification::centred, false);

    g.setColour (textDim);
    g.drawText (juce::String (juce::roundToInt (grooves::tempoOf (gr, processor.getHostBpm()))) + " bpm" + (gr.tempo > 0 ? juce::String() : ", the song's"),
                dragArea.withX (dragArea.getRight() + 14).withWidth (200), juce::Justification::centredLeft, false);
}

void GroovePage::mouseDown (const juce::MouseEvent&)
{
    dragging = false;
}

void GroovePage::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! written.has_value() || ! dragArea.contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6)
        return;

    dragging = true;
    repaint();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ midiFile().getFullPathName() }, false, this,
                                                                [safe = juce::Component::SafePointer<GroovePage> (this)]
                                                                {
                                                                    if (safe != nullptr)
                                                                    {
                                                                        safe->dragging = false;
                                                                        safe->repaint();
                                                                    }
                                                                });
}

void GroovePage::timerCallback()
{
    playButton.setButtonText (processor.isGroovePlaying() ? "Stop" : "Play");

    if (processor.isGroovePlaying())
        repaint (gridArea);
}

//==============================================================================
struct KitsPage::Row : public juce::Component
{
    Row (KitsPage& p, const KitDefinition& d) : page (p), def (d)
    {
        get.setToggleState (true, juce::dontSendNotification);
        get.setClickingTogglesState (false);
        get.onClick = [this] { page.processor.getLibrary().install (def.id); };
        stop.onClick = [this] { page.processor.getLibrary().cancel (def.id); };
        remove.onClick = [this]
        {
            if (page.processor.getKitId() == def.id)
                page.processor.chooseKit ({}, false);

            page.processor.getLibrary().remove (def.id);
        };
        use.onClick = [this]
        {
            page.processor.chooseKit (def.id, true);
            page.showKit();
        };

        for (auto* b : { &get, &stop, &remove, &use })
            addChildComponent (b);

        addChildComponent (bar);
    }

    void update()
    {
        status = page.processor.getLibrary().getStatus (def.id);
        using S = Library::State;
        const bool busy = status.state == S::queued || status.state == S::downloading || status.state == S::checking || status.state == S::importing;
        get.setVisible (status.state == S::missing || status.state == S::failed);
        get.setButtonText (status.state == S::failed ? "Try again" : (status.bytes > 0 ? "Carry on" : "Get it"));
        stop.setVisible (busy);
        bar.setVisible (busy);
        remove.setVisible (status.state == S::ready);
        use.setVisible (status.state == S::ready && page.processor.getKitId() != def.id);
        progress = status.progress;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        r.removeFromLeft (230);
        auto buttons = r.removeFromRight (130);
        get.setBounds (buttons.removeFromTop (30));
        stop.setBounds (get.getBounds());
        buttons.removeFromTop (8);
        use.setBounds (get.getBounds());
        remove.setBounds (buttons.removeFromTop (30));

        if (use.isVisible())
            remove.setBounds (get.getBounds().translated (0, 38));

        bar.setBounds (r.removeFromBottom (14).withTrimmedRight (20));
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds();
        g.setColour (panel);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);

        if (page.processor.getKitId() == def.id)
        {
            g.setColour (accent.withAlpha (0.6f));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
        }

        r.reduce (12, 10);
        auto picture = r.removeFromLeft (216);
        const float h = picture.getWidth() * def.pictureHeight / def.pictureWidth;
        const auto pictureArea = picture.toFloat().withSizeKeepingCentre ((float) picture.getWidth(), std::min ((float) picture.getHeight(), h));
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (pictureArea.toNearestInt());
            drawKitPicture (g, def, pictureArea.withSizeKeepingCentre (pictureArea.getHeight() * def.pictureWidth / def.pictureHeight, pictureArea.getHeight()));
        }

        r.removeFromLeft (14);
        r.removeFromRight (140);
        g.setColour (text);
        g.setFont (font (17.0f, true));
        g.drawText (def.name, r.removeFromTop (24), juce::Justification::centredLeft, true);
        g.setColour (textDim);
        g.setFont (font (13.0f));
        g.drawFittedText (def.description, r.removeFromTop (54), juce::Justification::topLeft, 3, 1.0f);
        g.setColour (textFaint);
        g.setFont (font (12.0f));
        g.drawFittedText (def.credit, r.removeFromTop (18), juce::Justification::centredLeft, 1, 1.0f);

        // Where it's got to
        using S = Library::State;
        juce::String words;

        switch (status.state)
        {
            case S::missing:      words = "Not here yet: a " + juce::File::descriptionOfSizeInBytes (def.zipBytes) + " download from DrumGizmo.org"
                                          + (status.bytes > 0 ? " (" + juce::File::descriptionOfSizeInBytes (status.bytes) + " of it already here)" : juce::String()); break;
            case S::queued:       words = "Waiting for the kit before it"; break;
            case S::downloading:  words = "Downloading: " + juce::File::descriptionOfSizeInBytes (status.bytes) + " of " + juce::File::descriptionOfSizeInBytes (status.total)
                                          + (status.bytesPerSecond > 0 ? ", " + juce::File::descriptionOfSizeInBytes ((juce::int64) status.bytesPerSecond) + " a second" : juce::String()); break;
            case S::checking:     words = "Checking the download"; break;
            case S::importing:    words = "Setting it up: every mic of every hit, " + juce::String (juce::roundToInt (status.progress * 100.0f)) + " %"; break;
            case S::ready:        words = page.processor.getKitId() == def.id ? "Here, and playing" : "Here"; break;
            case S::failed:       words = status.message; break;
        }

        g.setColour (status.state == S::failed ? danger : (status.state == S::ready ? ledOn : text));
        g.setFont (font (13.0f));
        g.drawFittedText (words, r.removeFromTop (34), juce::Justification::topLeft, 2, 1.0f);
    }

    KitsPage& page;
    const KitDefinition& def;
    Library::Status status;
    double progress = 0.0;
    juce::ProgressBar bar { progress };
    juce::TextButton get { "Get it" }, stop { "Stop" }, remove { "Remove" }, use { "Play this kit" };
};

KitsPage::KitsPage (DrumsProcessor& p, std::function<void()> show) : processor (p), showKit (std::move (show))
{
    for (auto& def : catalog())
        if (def.id != "testkit")
            addAndMakeVisible (rows.add (new Row (*this, def)));

    changeFolder.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Where Nightshift Drums keeps its kits", processor.getLibrary().getKitsFolder());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      processor.getLibrary().setKitsFolder (fc.getResult());
                              });
    };

    addAndMakeVisible (changeFolder);
    licences.onClick = [this] { showLicences(); };
    addAndMakeVisible (licences);
    processor.getLibrary().addChangeListener (this);

    for (auto* r : rows)
        r->update();

    startTimerHz (4);
}

KitsPage::~KitsPage()
{
    stopTimer();
    processor.getLibrary().removeChangeListener (this);
}

void KitsPage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto* r : rows)
        r->update();

    resized();
    repaint();
}

void KitsPage::timerCallback()
{
    for (auto* r : rows)
        r->update();
}

void KitsPage::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    footer = r.removeFromBottom (76);

    for (auto* row : rows)
    {
        row->setBounds (r.removeFromTop (150));
        r.removeFromTop (10);
    }

    changeFolder.setBounds (footer.getRight() - 150, footer.getY() + 8, 150, 28);
    licences.setBounds (changeFolder.getBounds().translated (-160, 0).withWidth (150));

    if (licenceView != nullptr)
        licenceView->setBounds (getLocalBounds().reduced (16, 12));
}

void KitsPage::showLicences()
{
    // The kits' credits and licences, and where the pictures come from
    struct View : public juce::Component
    {
        explicit View (std::function<void()> close)
        {
            juce::String words;
            words << "Nightshift Drums plays the free DrumGizmo kits. Drum samples provided by DrumGizmo.org.\n\n"
                  << "CrocellKit 1.1: by DrumGizmo.org, sampled by Lars Muldjord; the kit set up by Andreas of Crocell, the mics by Jacob Olsen "
                     "of JBOSound. Creative Commons Attribution 4.0 International, https://creativecommons.org/licenses/by/4.0/.\n\n"
                  << "DRSKit 2.1: by the DrumGizmo team with Jes Eiler of DRS Drums, sampled by Deva and Lars Muldjord. CC BY 4.0.\n\n"
                  << "MuldjordKit 3: by Lars Muldjord, DrumGizmo.org. CC BY 4.0.\n\n"
                  << "The pictures of the three kits were generated with an image AI for Nightshift Drums, from descriptions of "
                     "the real kits.\n\n"
                  << "The grooves come from NightShift's offline AI drummer. Nightshift Drums is built on JUCE.\n";

            text.setMultiLine (true);
            text.setReadOnly (true);
            text.setScrollbarsShown (true);
            text.setFont (font (13.0f));
            text.setText (words, false);
            addAndMakeVisible (text);
            closeButton.onClick = std::move (close);
            addAndMakeVisible (closeButton);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            g.setColour (edge);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
            g.setColour (text.findColour (juce::TextEditor::textColourId));
            g.setFont (font (16.0f, true));
            g.drawText ("Licences", 16, 10, 300, 26, juce::Justification::centredLeft, false);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 10);
            closeButton.setBounds (r.removeFromTop (28).removeFromRight (110));
            r.removeFromTop (10);
            text.setBounds (r);
        }

        juce::TextEditor text;
        juce::TextButton closeButton { "Close" };
    };

    licenceView = std::make_unique<View> ([this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<KitsPage> (this)]
                                                                                    { if (safe != nullptr) safe->licenceView = nullptr; }); });
    addAndMakeVisible (*licenceView);
    resized();
}

void KitsPage::paint (juce::Graphics& g)
{
    g.fillAll (background);
    auto f = footer;
    g.setColour (textDim);
    g.setFont (font (13.0f));
    g.drawText ("Kits folder: " + processor.getLibrary().getKitsFolder().getFullPathName(), f.removeFromTop (44).withTrimmedRight (330),
                juce::Justification::centredLeft, true);
    g.setColour (textFaint);
    g.setFont (font (12.0f));
    g.drawFittedText ("The kits are DrumGizmo.org's, free under Creative Commons Attribution 4.0. Their pictures were "
                      "generated with an image AI from descriptions of the real kits.",
                      f, juce::Justification::topLeft, 2, 1.0f);
}

} // namespace drums
