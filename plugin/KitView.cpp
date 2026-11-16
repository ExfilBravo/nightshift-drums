// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "KitView.h"
#include "common/Theme.h"
#include "BinaryData.h"

namespace drums
{

using namespace theme;

namespace
{
    /** The photo, lit: every pixel brighter and a touch warmer, for a piece that's just been hit. */
    juce::Image brightened (const juce::Image& source)
    {
        auto copy = source.createCopy().convertedToFormat (juce::Image::ARGB);
        juce::Image::BitmapData data (copy, juce::Image::BitmapData::readWrite);

        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x)
            {
                const auto c = data.getPixelColour (x, y);
                auto lift = [] (float v, float gain, float add) { return (juce::uint8) juce::jlimit (0, 255, (int) (v * gain + add)); };
                data.setPixelColour (x, y, juce::Colour (lift (c.getRed(), 1.6f, 10.0f), lift (c.getGreen(), 1.5f, 8.0f), lift (c.getBlue(), 1.35f, 4.0f)));
            }

        return copy;
    }

    bool isCymbal (const juce::String& id)
    {
        return id.startsWith ("ride") || id.startsWith ("crash") || id.startsWith ("china") || id.startsWith ("splash") || id.startsWith ("bell");
    }

    //==============================================================================
    // The drawn kit: brass cymbals turned on a lathe, black shells with chrome hoops, coated heads

    void drawStand (juce::Graphics& g, juce::Point<float> top, float floorY, float scale)
    {
        g.setColour (juce::Colour (0xff8d939f).withAlpha (0.55f));
        g.drawLine (top.x, top.y, top.x, floorY - 18.0f * scale, 2.2f * scale);

        for (float dx : { -22.0f, 0.0f, 22.0f })
            g.drawLine (top.x, floorY - 18.0f * scale, top.x + dx * scale, floorY, 1.6f * scale);
    }

    void drawCymbal (juce::Graphics& g, const Shape& s, juce::AffineTransform toView, bool china)
    {
        const auto path = s.path();
        const auto centre = juce::Point<float> (s.cx, s.cy).transformedBy (toView);
        const auto edge = juce::Point<float> (s.cx + s.rx, s.cy).transformedBy (toView);
        const float r = centre.getDistanceFrom (edge);

        juce::ColourGradient brass (juce::Colour (china ? 0xffd9b064 : 0xfff0d27c), centre.translated (-r * 0.25f, -r * 0.2f),
                                    juce::Colour (china ? 0xff5e4319 : 0xff7d5b22), centre.translated (r, r * 0.3f), true);
        brass.addColour (0.55, juce::Colour (china ? 0xff9a7330 : 0xffbf913f));
        g.setGradientFill (brass);
        g.fillPath (path, toView);

        // Lathe rings
        for (int ring = 1; ring < 14; ++ring)
        {
            const float k = 0.24f + ring * 0.055f;

            if (k >= 0.98f)
                break;

            juce::Path p;
            p.addEllipse (s.cx - s.rx * k, s.cy - s.ry * k, s.rx * k * 2.0f, s.ry * k * 2.0f);
            p.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (s.rotation), s.cx, s.cy));
            g.setColour ((ring % 2 == 0 ? juce::Colours::black : juce::Colours::white).withAlpha (0.07f));
            g.strokePath (p, juce::PathStrokeType (1.0f), toView);
        }

        // A china's turned-up edge
        if (china)
        {
            juce::Path rim;
            rim.addEllipse (s.cx - s.rx * 0.86f, s.cy - s.ry * 0.86f, s.rx * 1.72f, s.ry * 1.72f);
            rim.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (s.rotation), s.cx, s.cy));
            g.setColour (juce::Colour (0x55000000));
            g.strokePath (rim, juce::PathStrokeType (3.0f), toView);
        }

        // The bell, the felt and the wing nut
        juce::Path bell;
        bell.addEllipse (s.cx - s.rx * 0.22f, s.cy - s.ry * 0.22f, s.rx * 0.44f, s.ry * 0.44f);
        bell.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (s.rotation), s.cx, s.cy));
        juce::ColourGradient shine (juce::Colour (0xfffbe6a6), centre.translated (-r * 0.08f, -r * 0.08f), juce::Colour (0xffa47a2e), centre.translated (r * 0.22f, r * 0.1f), true);
        g.setGradientFill (shine);
        g.fillPath (bell, toView);
        g.setColour (juce::Colour (0xff2a2622));
        g.fillEllipse (centre.x - r * 0.035f, centre.y - r * 0.04f, r * 0.07f, r * 0.06f);

        g.setColour (juce::Colour (0x66000000));
        g.strokePath (path, juce::PathStrokeType (1.2f), toView);
    }

    void drawDrum (juce::Graphics& g, const Shape& s, juce::AffineTransform toView, bool snare)
    {
        // The shell: black wrap with a soft sheen, chrome lugs
        auto body = s.path();
        const auto left = juce::Point<float> (s.cx - s.rx, s.cy).transformedBy (toView);
        const auto right = juce::Point<float> (s.cx + s.rx, s.cy).transformedBy (toView);
        juce::ColourGradient wrap (snare ? juce::Colour (0xff6f6a63) : juce::Colour (0xff17181c), left,
                                   snare ? juce::Colour (0xff3b3833) : juce::Colour (0xff0b0b0d), right, false);
        wrap.addColour (0.3, snare ? juce::Colour (0xffb7b0a4) : juce::Colour (0xff3a3c44));
        g.setGradientFill (wrap);
        g.fillPath (body, toView);

        // Lugs: small chrome casings round the front of the shell
        for (int lug = 0; lug < 5; ++lug)
        {
            const float a = juce::MathConstants<float>::pi * (0.18f + lug * 0.16f);
            const float x = s.cx - std::cos (a) * s.rx * 0.96f, y = s.cy + std::sin (a) * s.ry * 0.96f;
            const auto top = juce::Point<float> (x, y + s.depth * 0.28f).transformedBy (toView);
            const auto bottom = juce::Point<float> (x, y + s.depth * 0.5f).transformedBy (toView);
            g.setColour (juce::Colour (0xff20232a));
            g.drawLine (top.x, top.y, bottom.x, bottom.y, 3.6f);
            g.setColour (juce::Colour (0xffa9b0bc));
            g.drawLine (top.x, top.y, bottom.x, bottom.y, 2.0f);
        }

        // The bottom hoop
        juce::Path hoop;
        hoop.addCentredArc (s.cx, s.cy + s.depth, s.rx, s.ry, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
        g.setColour (juce::Colour (0xffaab0bc));
        g.strokePath (hoop, juce::PathStrokeType (2.0f), toView);

        // The head, coated, and its hoop
        juce::Path head;
        head.addEllipse (s.cx - s.rx, s.cy - s.ry, s.rx * 2.0f, s.ry * 2.0f);
        const auto c = juce::Point<float> (s.cx, s.cy).transformedBy (toView);
        juce::ColourGradient coat (juce::Colour (0xfff4f1ea), c.translated (-(right.x - left.x) * 0.15f, -(right.x - left.x) * 0.08f),
                                   juce::Colour (0xffc9c4b8), right, true);
        g.setGradientFill (coat);
        g.fillPath (head, toView);
        g.setColour (juce::Colour (0xffd8dce4));
        g.strokePath (head, juce::PathStrokeType (3.0f), toView);
        g.setColour (juce::Colour (0x55000000));
        g.strokePath (head, juce::PathStrokeType (1.0f), toView);
    }

    void drawKick (juce::Graphics& g, const Shape& s, juce::AffineTransform toView)
    {
        // Its batter head, facing the drummer, inside a black shell
        juce::Path shell, head;
        shell.addEllipse (s.cx - s.rx, s.cy - s.ry, s.rx * 2.0f, s.ry * 2.0f);
        head.addEllipse (s.cx - s.rx * 0.9f, s.cy - s.ry * 0.9f, s.rx * 1.8f, s.ry * 1.8f);
        g.setColour (juce::Colour (0xff121316));
        g.fillPath (shell, toView);
        g.setColour (juce::Colour (0xffc3c8d1));
        g.strokePath (shell, juce::PathStrokeType (2.5f), toView);

        const auto c = juce::Point<float> (s.cx, s.cy).transformedBy (toView);
        const auto e = juce::Point<float> (s.cx + s.rx, s.cy).transformedBy (toView);
        juce::ColourGradient coat (juce::Colour (0xffe7e3da), c.translated (-(e.x - c.x) * 0.3f, -(e.x - c.x) * 0.3f), juce::Colour (0xffa29d92), e, true);
        g.setGradientFill (coat);
        g.fillPath (head, toView);
        g.setColour (juce::Colour (0x40000000));
        g.fillEllipse (c.x - (e.x - c.x) * 0.12f, c.y + (e.x - c.x) * 0.25f, (e.x - c.x) * 0.24f, (e.x - c.x) * 0.24f);   // where the beater lands
    }

    void drawPedal (juce::Graphics& g, const Shape& s, juce::AffineTransform toView)
    {
        auto p = s.path();
        const auto b = p.getBounds().transformedBy (toView);
        juce::ColourGradient metal (juce::Colour (0xffd5dae3), b.getTopLeft(), juce::Colour (0xff6c727e), b.getBottomRight(), false);
        g.setGradientFill (metal);
        g.fillPath (p, toView);
        g.setColour (juce::Colour (0x40000000));

        for (int i = 1; i < 6; ++i)
        {
            const float y = b.getY() + b.getHeight() * i / 6.0f;
            g.drawLine (b.getX() + 4.0f, y, b.getRight() - 4.0f, y, 1.0f);
        }
    }
}

//==============================================================================
juce::Image kitPhoto (const KitDefinition& def)
{
    if (def.picture.isEmpty())
        return {};

    int size = 0;
    const auto* data = DrumsArt::getNamedResource (def.picture.toRawUTF8(), size);
    return data != nullptr ? juce::ImageCache::getFromMemory (data, size) : juce::Image();
}

void drawKitPicture (juce::Graphics& g, const KitDefinition& def, juce::Rectangle<float> area)
{
    const float scale = area.getWidth() / def.pictureWidth;

    if (const auto photo = kitPhoto (def); photo.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (photo, area, juce::RectanglePlacement::stretchToFit);
        return;
    }

    // Drawn: a dark stage, a rug, stands, then the pieces back to front
    juce::ColourGradient stage (juce::Colour (0xff141824), area.getCentre().translated (0.0f, -area.getHeight() * 0.2f),
                                background, area.getTopLeft(), true);
    g.setGradientFill (stage);
    g.fillRect (area);

    const auto toView = juce::AffineTransform::scale (scale).translated (area.getX(), area.getY());
    const auto rug = juce::Rectangle<float> (def.pictureWidth * 0.12f, def.pictureHeight * 0.55f, def.pictureWidth * 0.76f, def.pictureHeight * 0.5f);
    g.setColour (juce::Colour (0xff2a1f22));
    g.fillEllipse (rug.transformedBy (toView));
    g.setColour (juce::Colour (0xff3b2a2d));
    g.drawEllipse (rug.reduced (14.0f).transformedBy (toView), 3.0f * scale);

    for (auto& piece : def.pieces)
        if (isCymbal (piece.id) && piece.lightsWith.isEmpty())
            drawStand (g, juce::Point<float> (piece.shape.cx, piece.shape.cy).transformedBy (toView), area.getBottom() - 8.0f * scale, scale);

    for (auto& piece : def.pieces)
    {
        const auto& s = piece.shape;

        if (! s.polygon.empty())
            drawPedal (g, s, toView);
        else if (piece.id.startsWith ("kick"))
            drawKick (g, s, toView);
        else if (piece.id == "hihat")
        {
            drawStand (g, juce::Point<float> (s.cx, s.cy).transformedBy (toView), area.getBottom() - 8.0f * scale, scale);
            auto lower = s;
            lower.cy += s.ry * 0.35f;
            drawCymbal (g, lower, toView, false);
            drawCymbal (g, s, toView, false);
        }
        else if (isCymbal (piece.id))
        {
            if (piece.lightsWith.isEmpty())     // a bell is part of its cymbal
                drawCymbal (g, s, toView, piece.id.startsWith ("china"));
        }
        else
            drawDrum (g, s, toView, piece.id == "snare");
    }
}

//==============================================================================
KitView::KitView (DrumsProcessor& p) : processor (p)
{
    addChildComponent (getButton);
    getButton.setToggleState (true, juce::dontSendNotification);    // the amber, primary look
    getButton.setClickingTogglesState (false);
    getButton.onClick = [this] { if (onGetKit) onGetKit(); };
    refreshKit();
    startTimerHz (30);
}

KitView::~KitView()
{
    stopTimer();
}

void KitView::refreshKit()
{
    shownKit = processor.getKitId();
    def = processor.getKitDefinition();
    picture = {};
    lit = {};

    if (def != nullptr)
    {
        picture = kitPhoto (*def);

        if (picture.isValid())
            lit = brightened (picture);
    }

    flash.assign (def != nullptr ? def->pieces.size() : 0, 0.0f);
    hovered = -1;
    resized();
    repaint();
}

void KitView::resized()
{
    if (def == nullptr)
        return;

    // The kit's picture space, as big as fits, in the middle
    const auto bounds = getLocalBounds().toFloat();
    scale = std::min (bounds.getWidth() / def->pictureWidth, bounds.getHeight() / def->pictureHeight);
    area = juce::Rectangle<float> (def->pictureWidth * scale, def->pictureHeight * scale).withCentre (bounds.getCentre());
    getButton.setBounds (getLocalBounds().withSizeKeepingCentre (200, 34).translated (0, 30));
}

juce::Path KitView::pathOf (int piece) const
{
    auto p = def->pieces[(size_t) piece].shape.path();
    p.applyTransform (juce::AffineTransform::scale (scale).translated (area.getX(), area.getY()));
    return p;
}

int KitView::pieceAt (juce::Point<float> at, int& articulation) const
{
    articulation = -1;

    if (def == nullptr || scale <= 0.0f)
        return -1;

    const auto local = (at - area.getPosition()) / scale;

    for (int i = (int) def->pieces.size(); --i >= 0;)
    {
        const auto& piece = def->pieces[(size_t) i];

        if (piece.shape.contains (local))
        {
            articulation = 0;

            if (piece.edge >= 0 && piece.shape.radiusAt (local) >= piece.edgeFrom)
                articulation = piece.edge;

            return i;
        }
    }

    return -1;
}

juce::String KitView::describe (int piece, int articulation) const
{
    if (def == nullptr || piece < 0)
        return {};

    const auto& p = def->pieces[(size_t) piece];
    const auto& a = p.articulations[(size_t) juce::jlimit (0, (int) p.articulations.size() - 1, articulation)];
    juce::String words = p.name;

    if (a.name != p.name && p.articulations.size() > 1)
        words << ", " << a.name.toLowerCase();

    if (! a.notes.empty())
        words << "  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << "  note " << a.notes.front();

    return words;
}

void KitView::play (int piece, int articulation, float velocity)
{
    if (def == nullptr || piece < 0)
        return;

    const auto& a = def->pieces[(size_t) piece].articulations[(size_t) articulation];

    if (! a.notes.empty())
        processor.playNote (a.notes.front(), velocity);
}

//==============================================================================
void KitView::mouseMove (const juce::MouseEvent& e)
{
    int articulation = -1;
    const int piece = pieceAt (e.position, articulation);
    mouse = e.position;

    if (piece != hovered || articulation != hoveredArticulation)
    {
        hovered = piece;
        hoveredArticulation = articulation;

        if (onTell)
            onTell (piece >= 0 ? describe (piece, articulation) : juce::String());
    }

    setMouseCursor (piece >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void KitView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();

    if (onTell)
        onTell ({});
}

void KitView::mouseDown (const juce::MouseEvent& e)
{
    if (processor.getKitState() != DrumsProcessor::KitState::ready)
        return;

    int articulation = -1;
    const int piece = pieceAt (e.position, articulation);

    if (piece < 0)
        return;

    if (e.mods.isPopupMenu())
    {
        // Every articulation of the piece, to play one
        juce::PopupMenu menu;
        const auto& p = def->pieces[(size_t) piece];
        menu.addSectionHeader (p.name);

        for (size_t a = 0; a < p.articulations.size(); ++a)
            menu.addItem ((int) a + 1, p.articulations[a].name + (p.articulations[a].notes.empty() ? juce::String()
                                                                   : "   (note " + juce::String (p.articulations[a].notes.front()) + ")"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [safe = juce::Component::SafePointer<KitView> (this), piece] (int chosen)
                            {
                                if (safe != nullptr && chosen > 0)
                                    safe->play (piece, chosen - 1, 0.85f);
                            });
        return;
    }

    // Shift for a softer hit, Ctrl (Cmd) for the hardest
    const float velocity = e.mods.isShiftDown() ? 0.45f : (e.mods.isCommandDown() ? 1.0f : 0.85f);
    play (piece, articulation, velocity);
}

//==============================================================================
void KitView::timerCallback()
{
    if (processor.getKitId() != shownKit)
        refreshKit();

    // What's been hit, from the engine
    Engine::Struck struck[64];
    const int n = processor.getEngine().popStruck (struck, 64);
    bool changed = false;

    for (int i = 0; i < n; ++i)
        if (def != nullptr && juce::isPositiveAndBelow (struck[i].piece, (int) flash.size()))
        {
            const float strength = 0.35f + 0.65f * struck[i].velocity;
            flash[(size_t) struck[i].piece] = std::max (flash[(size_t) struck[i].piece], strength);
            const auto& with = def->pieces[(size_t) struck[i].piece].lightsWith;

            for (size_t p = 0; p < def->pieces.size(); ++p)
                if (with.isNotEmpty() && def->pieces[p].id == with)
                    flash[p] = std::max (flash[p], strength * 0.8f);

            changed = true;
        }

    for (auto& f : flash)
        if (f > 0.0f)
        {
            f = f < 0.02f ? 0.0f : f * 0.8f;
            changed = true;
        }

    const bool waiting = processor.getKitState() == DrumsProcessor::KitState::loading;
    getButton.setVisible (processor.getKitState() == DrumsProcessor::KitState::notInstalled);

    if (changed || waiting)
        repaint();
}

void KitView::paint (juce::Graphics& g)
{
    g.fillAll (background);

    if (def == nullptr)
    {
        drawState (g);
        return;
    }

    drawKit (g);
    drawLights (g);
    drawState (g);
}

void KitView::drawKit (juce::Graphics& g)
{
    drawKitPicture (g, *def, area);

    if (picture.isValid())
    {
        // The room around the kit falls away a little
        juce::ColourGradient vignette (juce::Colours::transparentBlack, area.getCentre(), background.withAlpha (0.6f),
                                       area.getTopLeft(), true);
        vignette.addColour (0.55, juce::Colours::transparentBlack);
        g.setGradientFill (vignette);
        g.fillRect (area);
    }
}

void KitView::drawLights (juce::Graphics& g)
{
    for (size_t i = 0; i < flash.size(); ++i)
    {
        const float f = flash[i];
        const bool hover = (int) i == hovered;

        if (f <= 0.0f && ! hover)
            continue;

        const auto path = pathOf ((int) i);

        if (picture.isValid() && lit.isValid())
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (path);
            g.setOpacity (juce::jlimit (0.0f, 1.0f, std::max (f, hover ? 0.22f : 0.0f)));
            g.drawImage (lit, area, juce::RectanglePlacement::stretchToFit);
        }
        else if (f > 0.0f)
        {
            // A drawn piece: lit up, warm
            g.setColour (juce::Colours::white.withAlpha (0.3f * f));
            g.fillPath (path);
            g.setColour (accent.withAlpha (0.4f * f));
            g.fillPath (path);
        }

        if (hover || f > 0.05f)
        {
            g.setColour (accent.withAlpha (hover ? 0.9f : 0.7f * f));
            g.strokePath (path, juce::PathStrokeType (hover ? 1.6f : 2.0f));
        }
    }

    // What's under the mouse, beside it
    if (hovered >= 0 && def != nullptr && processor.getKitState() == DrumsProcessor::KitState::ready)
    {
        const auto words = describe (hovered, hoveredArticulation);
        const auto f = font (12.5f);
        const float w = juce::GlyphArrangement::getStringWidth (f, words) + 18.0f;
        auto box = juce::Rectangle<float> (mouse.x + 14.0f, mouse.y + 14.0f, w, 22.0f);

        if (box.getRight() > (float) getWidth() - 4.0f)
            box.setX (mouse.x - w - 10.0f);

        if (box.getBottom() > (float) getHeight() - 4.0f)
            box.setY (mouse.y - 30.0f);

        g.setColour (raised.withAlpha (0.94f));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (edge);
        g.drawRoundedRectangle (box, 5.0f, 1.0f);
        g.setColour (text);
        g.setFont (f);
        g.drawText (words, box, juce::Justification::centred, false);
    }
}

void KitView::drawState (juce::Graphics& g)
{
    const auto state = processor.getKitState();

    if (state == DrumsProcessor::KitState::ready)
        return;

    juce::String title, detail;
    const auto name = def != nullptr ? def->name : juce::String();

    switch (state)
    {
        case DrumsProcessor::KitState::notInstalled:
            title = "The " + name + " kit isn't on this computer yet";
            detail = "It's a free download from DrumGizmo.org, " + juce::File::descriptionOfSizeInBytes (def != nullptr ? def->zipBytes : 0)
                     + ". Nightshift Drums gets it and sets it up for you.";
            break;

        case DrumsProcessor::KitState::loading:
            title = "Loading the " + name + " kit";
            detail = juce::String (juce::roundToInt (processor.getLoadProgress() * 100.0f)) + " %: every hit's first moments into memory, the rest plays from disk";
            break;

        case DrumsProcessor::KitState::failed:
            title = "The " + name + " kit didn't load";
            detail = processor.getKitError();
            break;

        case DrumsProcessor::KitState::none:
        case DrumsProcessor::KitState::ready:
            title = "Choose a kit";
            detail = "Pick one at the top right.";
            break;
    }

    g.setColour (background.withAlpha (0.72f));
    g.fillRect (getLocalBounds());

    auto box = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 620), 120).translated (0, -30);
    g.setColour (text);
    g.setFont (font (18.0f, true));
    g.drawText (title, box.removeFromTop (30), juce::Justification::centred, true);
    g.setColour (textDim);
    g.setFont (font (13.0f));
    g.drawFittedText (detail, box.removeFromTop (44), juce::Justification::centredTop, 3, 1.0f);

    if (state == DrumsProcessor::KitState::loading)
    {
        const auto bar = getLocalBounds().withSizeKeepingCentre (320, 8).translated (0, 36).toFloat();
        g.setColour (raised);
        g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, processor.getLoadProgress())), 4.0f);
    }
}

} // namespace drums
