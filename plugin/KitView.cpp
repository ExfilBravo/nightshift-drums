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

    //==============================================================================
    // The drum machines, drawn in the spirit of the classic boxes: a name band, a section a drum (its
    // lamp, its name, its knobs) across the middle, sixteen step keys along the bottom. The sections are
    // the pieces (Catalog.cpp places them); the rest is laid out here, in the same 2000 x 1116 space.

    namespace face
    {
        constexpr float bodyX = 24.0f, bodyY = 20.0f, bodyW = 1952.0f, bodyH = 1076.0f, corner = 34.0f;
        constexpr float bandBottom = 196.0f, seam = 800.0f;
        constexpr float keysLeft = 60.0f, keysWidth = 1880.0f, keysTop = 862.0f, keyHeight = 118.0f, keyGap = 12.0f, groupGap = 30.0f;
        constexpr float lampDrop = 42.0f, lampRadius = 11.0f, knobRadius = 34.0f, firstKnob = 166.0f, knobStep = 116.0f;

        inline float keyWidth()             { return (keysWidth - 12.0f * keyGap - 3.0f * groupGap) / 16.0f; }

        inline juce::Rectangle<float> key (int step)
        {
            const float x = keysLeft + (float) step * keyWidth() + (float) (step - step / 4) * keyGap + (float) (step / 4) * groupGap;
            return { x, keysTop, keyWidth(), keyHeight };
        }

        inline juce::Point<float> stepLamp (int step)       { return { key (step).getCentreX(), keysTop - 24.0f }; }

        inline juce::Point<float> sectionLamp (const Piece& p)
        {
            const auto r = p.shape.path().getBounds();
            return { r.getCentreX(), r.getY() + lampDrop };
        }
    }

    struct MachineLook
    {
        juce::Colour bodyTop, bodyBottom, rim, band, bandBottom, name, number, subtitle;
        juce::Colour panel, panelEdge, label, knobLabel, skirt, skirtDark, knobTop, pointer;
        juce::Colour lampOff, lampOn, stepNumber, key, keyEdge;
        bool warm = false;      // the 808's keys and section caps run red to cream; the 909's are pale, marked in orange
    };

    MachineLook lookOf (const KitDefinition& def)
    {
        MachineLook l;

        if (def.machine == "909")
        {
            // Brushed silver, a graphite band, orange marks, black knobs
            l.bodyTop = juce::Colour (0xffdfddd7);  l.bodyBottom = juce::Colour (0xffb6b3ac);  l.rim = juce::Colour (0xff85827c);
            l.band = juce::Colour (0xff2f3034);     l.bandBottom = juce::Colour (0xff222326);  l.name = juce::Colours::white;
            l.number = juce::Colour (0xffff7d33);   l.subtitle = juce::Colour (0xffcbc8c2);
            l.panel = juce::Colour (0xffd4d2cc);    l.panelEdge = juce::Colour (0xff97948d);   l.label = juce::Colour (0xff1f2023);
            l.knobLabel = juce::Colour (0xff3b3c40); l.skirt = juce::Colour (0xff3a3b3f);      l.skirtDark = juce::Colour (0xff0e0f11);
            l.knobTop = juce::Colour (0xff2a2b2f);  l.pointer = juce::Colour (0xfff4f4f1);
            l.lampOff = juce::Colour (0xff5e221d);  l.lampOn = juce::Colour (0xffff3d2b);     l.stepNumber = juce::Colour (0xff2f3034);
            l.key = juce::Colour (0xffeeede9);      l.keyEdge = juce::Colour (0xff86837d);
            return l;
        }

        // Near-black, warm: cream-topped knobs, keys from red to cream
        l.bodyTop = juce::Colour (0xff2c2926);  l.bodyBottom = juce::Colour (0xff161514);  l.rim = juce::Colour (0xff4c4741);
        l.band = juce::Colour (0xff1f1d1b);     l.bandBottom = juce::Colour (0xff141312);  l.name = juce::Colour (0xfff4efe7);
        l.number = theme::accent;               l.subtitle = juce::Colour (0xffb6aea2);
        l.panel = juce::Colour (0xff25221f);    l.panelEdge = juce::Colour (0xff403b35);   l.label = juce::Colour (0xfff2e9da);
        l.knobLabel = juce::Colour (0xffb3ab9f); l.skirt = juce::Colour (0xff3b3835);      l.skirtDark = juce::Colour (0xff121110);
        l.knobTop = juce::Colour (0xffe8dec9);  l.pointer = juce::Colour (0xff221e1a);
        l.lampOff = juce::Colour (0xff521c17);  l.lampOn = juce::Colour (0xffff4433);     l.stepNumber = juce::Colour (0xffaaa296);
        l.warm = true;
        return l;
    }

    /** The 808's run of colours across its keys and sections: red, orange, amber, cream. */
    juce::Colour warmAt (float t)
    {
        const juce::Colour stops[] { juce::Colour (0xffc8372d), juce::Colour (0xffe3712b), juce::Colour (0xfff0b13d), juce::Colour (0xffede1c5) };
        t = juce::jlimit (0.0f, 1.0f, t) * 3.0f;
        const int i = std::min (2, (int) t);
        return stops[i].interpolatedWith (stops[i + 1], t - (float) i);
    }

    /** A knob's turn (0..1) that looks set by hand, the same every time. */
    float turnOf (int section, int knob)
    {
        const double v = std::sin ((section + 1) * 12.9898 + (knob + 1) * 78.233) * 43758.5453;
        return 0.3f + 0.45f * (float) (v - std::floor (v));
    }

    void drawKnob (juce::Graphics& g, juce::Point<float> c, float r, float turn, const MachineLook& look)
    {
        g.setColour (juce::Colours::black.withAlpha (look.warm ? 0.55f : 0.3f));
        g.fillEllipse (c.x - r + r * 0.06f, c.y - r + r * 0.14f, r * 2.0f, r * 2.0f);

        juce::ColourGradient skirt (look.skirt, c.x, c.y - r, look.skirtDark, c.x, c.y + r, false);
        g.setGradientFill (skirt);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        // The skirt's grip (too fine to see on a small one)
        g.setColour (juce::Colours::black.withAlpha (0.3f));

        for (int i = 0; i < (r >= 8.0f ? 16 : 0); ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / 16.0f;
            g.drawLine (c.x + std::sin (a) * r * 0.74f, c.y - std::cos (a) * r * 0.74f, c.x + std::sin (a) * r * 0.97f, c.y - std::cos (a) * r * 0.97f,
                        std::max (0.6f, r * 0.03f));
        }

        const float t = r * 0.64f;
        juce::ColourGradient top (look.knobTop.brighter (0.3f), c.x - t * 0.4f, c.y - t * 0.6f, look.knobTop.darker (0.25f), c.x + t * 0.5f, c.y + t * 0.8f, true);
        g.setGradientFill (top);
        g.fillEllipse (c.x - t, c.y - t, t * 2.0f, t * 2.0f);

        const float a = juce::degreesToRadians (-135.0f + 270.0f * turn);
        g.setColour (look.pointer);
        g.drawLine (c.x + std::sin (a) * r * 0.18f, c.y - std::cos (a) * r * 0.18f, c.x + std::sin (a) * r * 0.9f, c.y - std::cos (a) * r * 0.9f,
                    std::max (1.0f, r * 0.12f));
    }

    void drawLamp (juce::Graphics& g, juce::Point<float> c, float r, const MachineLook& look, float on)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (c.x - r * 1.3f, c.y - r * 1.3f, r * 2.6f, r * 2.6f);

        const auto colour = look.lampOff.interpolatedWith (look.lampOn, juce::jlimit (0.0f, 1.0f, on));
        juce::ColourGradient lens (colour.brighter (0.3f + 0.5f * on), c.x - r * 0.3f, c.y - r * 0.4f, colour.darker (0.35f), c.x + r * 0.6f, c.y + r * 0.7f, true);
        g.setGradientFill (lens);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

        if (on > 0.01f)
        {
            juce::ColourGradient glow (look.lampOn.withAlpha (0.5f * on), c.x, c.y, look.lampOn.withAlpha (0.0f), c.x + r * 4.5f, c.y, true);
            g.setGradientFill (glow);
            g.fillEllipse (c.x - r * 4.5f, c.y - r * 4.5f, r * 9.0f, r * 9.0f);
        }
    }

    /** Words on the machine at `px` (picture space) or smaller down to `leastPx`, on one line or two,
        whichever fits `box`. */
    void drawWords (juce::Graphics& g, const juce::String& words, juce::Rectangle<float> box, float px, float leastPx, float s,
                    bool bold, juce::Colour colour, juce::Justification just = juce::Justification::centredTop)
    {
        g.setColour (colour);
        auto fits = [&] (const juce::String& line, float size) { return juce::GlyphArrangement::getStringWidth (font (size * s, bold), line) <= box.getWidth(); };

        for (float size = px; size >= leastPx - 0.01f; size -= 1.0f)
            if (fits (words, size))
            {
                g.setFont (font (size * s, bold));
                g.drawText (words, box, just, false);
                return;
            }

        // Two lines: the words split where both halves fit best
        const auto parts = juce::StringArray::fromTokens (words, " ", "");

        for (float size = px; size >= leastPx - 0.01f; size -= 1.0f)
            for (int cut = 1; cut < parts.size(); ++cut)
            {
                juce::StringArray first, second;

                for (int i = 0; i < parts.size(); ++i)
                    (i < cut ? first : second).add (parts[i]);

                const auto a = first.joinIntoString (" "), b = second.joinIntoString (" ");

                if (fits (a, size) && fits (b, size))
                {
                    g.setFont (font (size * s, bold));
                    const float line = size * s * 1.12f;
                    g.drawText (a, box.withHeight (line), just, false);
                    g.drawText (b, box.withTrimmedTop (line).withHeight (line), just, false);
                    return;
                }
            }

        g.setFont (font (leastPx * s, bold));
        g.drawText (words, box, just, false);
    }

    void drawMachine (juce::Graphics& g, const KitDefinition& def, juce::Rectangle<float> area)
    {
        const float s = area.getWidth() / def.pictureWidth;
        const bool words = s >= 0.3f;         // a thumbnail's too small to read: shapes only
        const auto look = lookOf (def);
        const auto toView = juce::AffineTransform::scale (s).translated (area.getX(), area.getY());
        auto at = [&] (juce::Rectangle<float> r) { return r.transformedBy (toView); };
        auto point = [&] (juce::Point<float> p) { return p.transformedBy (toView); };
        const auto clip = g.getClipBounds().toFloat();

        // The box, and its edge catching the light
        const auto body = at ({ face::bodyX, face::bodyY, face::bodyW, face::bodyH });
        juce::Path shell;
        shell.addRoundedRectangle (body, face::corner * s);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (body.translated (0.0f, 10.0f * s), face::corner * s);
        juce::ColourGradient metal (look.bodyTop, body.getX(), body.getY(), look.bodyBottom, body.getX(), body.getBottom(), false);
        g.setGradientFill (metal);
        g.fillPath (shell);

        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shell);

            // The name band, and the line of colour under it
            const auto band = at ({ face::bodyX, face::bodyY, face::bodyW, face::bandBottom - face::bodyY });
            juce::ColourGradient bandFill (look.band, band.getX(), band.getY(), look.bandBottom, band.getX(), band.getBottom(), false);
            g.setGradientFill (bandFill);
            g.fillRect (band);

            const auto line = at ({ face::bodyX, face::bandBottom, face::bodyW, 7.0f });

            if (look.warm)
            {
                juce::ColourGradient run (warmAt (0.0f), line.getX(), line.getY(), warmAt (1.0f), line.getRight(), line.getY(), false);
                run.addColour (1.0 / 3.0, warmAt (1.0f / 3.0f));
                run.addColour (2.0 / 3.0, warmAt (2.0f / 3.0f));
                g.setGradientFill (run);
            }
            else
            {
                g.setColour (look.number);
            }

            g.fillRect (line);

            // A seam between the sections and the steps
            const auto seam = at ({ face::bodyX + 30.0f, face::seam, face::bodyW - 60.0f, 2.0f });
            g.setColour (juce::Colours::black.withAlpha (look.warm ? 0.5f : 0.22f));
            g.fillRect (seam);
            g.setColour (juce::Colours::white.withAlpha (look.warm ? 0.05f : 0.35f));
            g.fillRect (seam.translated (0.0f, 2.0f * s));
        }

        g.setColour (look.rim);
        g.drawRoundedRectangle (body.reduced (1.0f * s), face::corner * s, std::max (1.0f, 2.5f * s));

        // The name, and the two big knobs at the right of the band
        if (words)
        {
            const auto nameBox = at ({ 80.0f, 44.0f, 1200.0f, 84.0f });
            const auto nameFont = font (78.0f * s, true);
            const juce::String first ("NIGHTSHIFT ");
            g.setFont (nameFont);
            g.setColour (look.name);
            g.drawText (first, nameBox, juce::Justification::centredLeft, false);
            g.setColour (look.number);
            g.drawText (def.machine, nameBox.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (nameFont, first)), juce::Justification::centredLeft, false);
            drawWords (g, "Drum machine", { nameBox.getX(), at ({ 0.0f, 132.0f, 1.0f, 1.0f }).getY(), 600.0f * s, 40.0f * s }, 30.0f, 30.0f, s,
                       false, look.subtitle, juce::Justification::centredLeft);
        }

        for (auto [x, name] : { std::pair (1676.0f, "Tempo"), std::pair (1842.0f, "Volume") })
        {
            drawKnob (g, point ({ x, 96.0f }), 42.0f * s, name[0] == 'T' ? 0.5f : 0.68f, look);

            if (words)
                drawWords (g, name, at ({ x - 75.0f, 144.0f, 150.0f, 36.0f }), 26.0f, 26.0f, s, false, look.subtitle);
        }

        // A section a drum, every name the same size: the biggest that all of them fit on a line at
        const int count = (int) def.pieces.size();
        float namePx = 30.0f;

        if (words)
            for (; namePx > 26.0f; namePx -= 0.5f)
            {
                bool all = true;

                for (auto& p : def.pieces)
                    all = all && juce::GlyphArrangement::getStringWidth (font (namePx * s, true), p.name)
                                     <= (p.shape.path().getBounds().getWidth() - 8.0f) * s;

                if (all)
                    break;
            }

        for (int i = 0; i < count; ++i)
        {
            const auto& p = def.pieces[(size_t) i];
            const auto r = p.shape.path().getBounds();
            const auto v = at (r);

            if (! v.expanded (60.0f * s).intersects (clip))
                continue;

            g.setColour (look.panel);
            g.fillRoundedRectangle (v, 8.0f * s);
            g.setColour (look.panelEdge);
            g.drawRoundedRectangle (v, 8.0f * s, std::max (1.0f, 2.0f * s));

            // Its cap: the run of colours across the 808, orange on the 909
            g.setColour (look.warm ? warmAt ((float) i / (float) std::max (1, count - 1)) : look.number);
            g.fillRoundedRectangle (at (r.withHeight (12.0f)), 4.0f * s);

            drawLamp (g, point (face::sectionLamp (p)), face::lampRadius * s, look, 0.0f);

            if (words)
                drawWords (g, p.name, at ({ r.getX() + 4.0f, r.getY() + 62.0f, r.getWidth() - 8.0f, 72.0f }), namePx, namePx, s, true, look.label);

            for (int k = 0; k < p.knobs.size(); ++k)
            {
                const float y = r.getY() + face::firstKnob + (float) k * face::knobStep;
                drawKnob (g, point ({ r.getCentreX(), y }), face::knobRadius * s, turnOf (i, k), look);

                if (words)
                    drawWords (g, p.knobs[k], at ({ r.getX() + 2.0f, y + face::knobRadius + 8.0f, r.getWidth() - 4.0f, 34.0f }), 26.0f, 25.5f, s,
                               false, look.knobLabel);
            }
        }

        // Sixteen steps, in fours
        for (int step = 0; step < 16; ++step)
        {
            const auto k = face::key (step);
            const auto v = at (k);

            if (! v.expanded (60.0f * s).intersects (clip))
                continue;

            drawLamp (g, point (face::stepLamp (step)), 9.0f * s, look, 0.0f);

            const auto colour = look.warm ? warmAt ((float) step / 15.0f) : look.key;
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (v.translated (0.0f, 5.0f * s), 8.0f * s);
            juce::ColourGradient plastic (colour.brighter (0.16f), v.getX(), v.getY(), colour.darker (0.22f), v.getX(), v.getBottom(), false);
            g.setGradientFill (plastic);
            g.fillRoundedRectangle (v, 8.0f * s);
            g.setColour (look.warm ? colour.darker (0.7f) : look.keyEdge);
            g.drawRoundedRectangle (v, 8.0f * s, std::max (1.0f, 2.0f * s));
            g.setColour (juce::Colours::white.withAlpha (0.24f));
            g.fillRoundedRectangle (v.reduced (8.0f * s).withHeight (v.getHeight() * 0.16f), 4.0f * s);

            if (words)
                drawWords (g, juce::String (step + 1), at ({ k.getX(), k.getBottom() + 10.0f, k.getWidth(), 36.0f }), 26.0f, 26.0f, s, false, look.stepNumber);

            // The 909's fours, marked in orange under their numbers
            if (! look.warm && step % 4 == 0)
            {
                const auto last = face::key (step + 3);
                g.setColour (look.number);
                g.fillRect (at ({ k.getX(), k.getBottom() + 52.0f, last.getRight() - k.getX(), 6.0f }));
            }
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

    if (def.isMachine())
    {
        drawMachine (g, def, area);
        return;
    }

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

    const bool machine = def != nullptr && def->isMachine();
    dirty.assign (flash.size(), false);

    for (int i = 0; i < n; ++i)
        if (def != nullptr && juce::isPositiveAndBelow (struck[i].piece, (int) flash.size()))
        {
            const float strength = 0.35f + 0.65f * struck[i].velocity;
            flash[(size_t) struck[i].piece] = std::max (flash[(size_t) struck[i].piece], strength);
            dirty[(size_t) struck[i].piece] = true;
            const auto& with = def->pieces[(size_t) struck[i].piece].lightsWith;

            for (size_t p = 0; p < def->pieces.size(); ++p)
                if (with.isNotEmpty() && def->pieces[p].id == with)
                {
                    flash[p] = std::max (flash[p], strength * 0.8f);
                    dirty[p] = true;
                }

            changed = true;
        }

    for (size_t i = 0; i < flash.size(); ++i)
        if (flash[i] > 0.0f)
        {
            flash[i] = flash[i] < 0.02f ? 0.0f : flash[i] * 0.8f;
            dirty[i] = true;
            changed = true;
        }

    const bool waiting = processor.getKitState() == DrumsProcessor::KitState::loading;
    getButton.setVisible (processor.getKitState() == DrumsProcessor::KitState::notInstalled);

    // Loaded, or failed: all of it again, without the message over it
    if (processor.getKitState() != shownState)
    {
        shownState = processor.getKitState();
        repaint();
    }

    if (machine && ! waiting)
    {
        // A drum machine's face is a lot to draw: only the sections whose lamps changed, and the step
        // lamps as the music moves along
        for (size_t i = 0; i < dirty.size(); ++i)
            if (dirty[i])
                repaint (pathOf ((int) i).getBounds().expanded (60.0f * scale).getSmallestIntegerContainer());

        int step = -1;

        if (const double beat = processor.getRunningBeat(); beat >= 0.0)
            step = (int) std::fmod (std::floor (beat * 4.0), 16.0);

        if (step != litStep)
        {
            for (int s : { litStep, step })
                if (s >= 0)
                {
                    const auto c = juce::Point<float> (area.getX(), area.getY()) + face::stepLamp (s) * scale;
                    repaint (juce::Rectangle<float> (120.0f * scale, 120.0f * scale).withCentre (c).getSmallestIntegerContainer());
                }

            litStep = step;
        }

        return;
    }

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
    const bool machine = def != nullptr && def->isMachine();
    const auto look = machine ? lookOf (*def) : MachineLook {};
    const auto toView = juce::AffineTransform::scale (scale).translated (area.getX(), area.getY());

    for (size_t i = 0; i < flash.size(); ++i)
    {
        const float f = flash[i];
        const bool hover = (int) i == hovered;

        if (f <= 0.0f && ! hover)
            continue;

        const auto path = pathOf ((int) i);

        if (machine)
        {
            if (f > 0.0f)
            {
                // Its lamp lit, and the section warm under it
                g.setColour (look.lampOn.withAlpha (0.12f * f));
                g.fillPath (path);
                drawLamp (g, face::sectionLamp (def->pieces[i]).transformedBy (toView), face::lampRadius * scale, look, f);
            }
        }
        else if (picture.isValid() && lit.isValid())
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

        if (hover || (f > 0.05f && ! machine))
        {
            g.setColour (accent.withAlpha (hover ? 0.9f : 0.7f * f));
            g.strokePath (path, juce::PathStrokeType (hover ? 1.6f : 2.0f));
        }
    }

    // The step the music's on
    if (machine && litStep >= 0)
        drawLamp (g, face::stepLamp (litStep).transformedBy (toView), 9.0f * scale, look, 1.0f);

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
            if (def != nullptr && def->isMachine())
            {
                title = "Making the " + name;
                detail = juce::String (juce::roundToInt (processor.getLoadProgress() * 100.0f)) + " %: every drum synthesised, three layers each, nothing to download";
            }
            else
            {
                title = "Loading the " + name + " kit";
                detail = juce::String (juce::roundToInt (processor.getLoadProgress() * 100.0f)) + " %: every hit's first moments into memory, the rest plays from disk";
            }
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
