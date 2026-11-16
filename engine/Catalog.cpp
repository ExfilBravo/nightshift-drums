// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Catalog.h"

namespace drums
{

//==============================================================================
juce::Path Shape::path() const
{
    juce::Path p;

    if (! polygon.empty())
    {
        p.startNewSubPath (polygon.front());

        for (size_t i = 1; i < polygon.size(); ++i)
            p.lineTo (polygon[i]);

        p.closeSubPath();
        return p;
    }

    if (depth <= 0.0f)
    {
        p.addEllipse (cx - rx, cy - ry, rx * 2.0f, ry * 2.0f);
    }
    else
    {
        // A drum: its head, and the shell down to the bottom hoop
        p.startNewSubPath (cx - rx, cy);
        p.addCentredArc (cx, cy, rx, ry, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        p.lineTo (cx + rx, cy + depth);
        p.addCentredArc (cx, cy + depth, rx, ry, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f);
        p.closeSubPath();
    }

    if (rotation != 0.0f)
        p.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (rotation), cx, cy));

    return p;
}

bool Shape::contains (juce::Point<float> point) const
{
    return path().contains (point);
}

float Shape::radiusAt (juce::Point<float> point) const
{
    if (! polygon.empty() || rx <= 0.0f || ry <= 0.0f)
        return -1.0f;

    const float a = -juce::degreesToRadians (rotation);
    const float dx = point.x - cx, dy = point.y - cy;
    const float x = dx * std::cos (a) - dy * std::sin (a), y = dx * std::sin (a) + dy * std::cos (a);
    return std::hypot (x / rx, y / ry);
}

//==============================================================================
namespace
{
    Shape ellipse (float cx, float cy, float rx, float ry, float rotation = 0.0f)
    {
        Shape s;
        s.cx = cx; s.cy = cy; s.rx = rx; s.ry = ry; s.rotation = rotation;
        return s;
    }

    Shape drum (float cx, float cy, float rx, float ry, float depth)
    {
        auto s = ellipse (cx, cy, rx, ry);
        s.depth = depth;
        return s;
    }

    Shape polygon (std::initializer_list<juce::Point<float>> points)
    {
        Shape s;
        s.polygon = points;
        return s;
    }

    Piece piece (const char* id, const char* name, Shape shape, std::vector<Articulation> articulations,
                 int edge = -1, float edgeFrom = 0.0f, const char* lightsWith = "")
    {
        Piece p;
        p.id = id;
        p.name = name;
        p.shape = std::move (shape);
        p.articulations = std::move (articulations);
        p.edge = edge;
        p.edgeFrom = edgeFrom;
        p.lightsWith = lightsWith;
        return p;
    }

    KitDefinition crocell()
    {
        KitDefinition k;
        k.id = "crocell";
        k.name = "Crocell";
        k.folder = "Crocell";
        k.description = "The kit of the Danish metal band Crocell, sampled in JBOSound's studio during the recording of their album "
                        "Relics: double pedal, two rack and two floor toms, three crashes, two chinas, two splashes. "
                        "Metal, metalcore, hard rock, punk.";
        k.credit = "CrocellKit 1.1 by DrumGizmo.org, sampled by Lars Muldjord; kit set up by Crocell, mics by JBOSound. CC BY 4.0";
        k.url = "https://drumgizmo.org/kits/CrocellKit/CrocellKit1_1.zip";
        k.zipBytes = 5646502341;
        k.md5 = "fa2be0f847bcd8ddef3830c1523690d3";
        k.kitFile = "CrocellKit/CrocellKit_full.xml";
        k.picture = "crocellkit_jpg";
        k.pictureCredit = "Picture: generated with an image AI for Nightshift Drums, from a description of the kit";
        k.pictureWidth = 2000;
        k.pictureHeight = 1116;

        k.pieces = {
            piece ("ride", "Ride", ellipse (1697, 490, 222, 110, -4), { { "Ride", "RideR", { 51 } } }),
            piece ("bell", "Ride bell", ellipse (1678, 457, 45, 24), { { "Bell", "RideRBell", { 53 } } }, -1, 0.0f, "ride"),
            piece ("chinaR", "China, right", ellipse (1586, 250, 172, 67), { { "China", "ChinaR", { 52 } } }),
            piece ("crashX", "Crash, top", ellipse (1096, 133, 138, 34), { { "Crash", "CrashRXtra", { 59 } } }),
            piece ("crashR", "Crash, right", ellipse (1304, 291, 163, 88), { { "Crash", "CrashR", { 57 } }, { "Stopped", "CrashRStopped", { 75 } } }),
            piece ("splashR", "Splash, right", ellipse (1000, 272, 78, 25), { { "Splash", "SplashR", { 58 } } }),
            piece ("splashL", "Splash, left", ellipse (826, 273, 78, 27), { { "Splash", "SplashL", { 55 } } }),
            piece ("crashL", "Crash, left", ellipse (627, 161, 160, 53), { { "Crash", "CrashL", { 49 } }, { "Stopped", "CrashLStopped", { 56 } } }),
            piece ("chinaL", "China, left", ellipse (293, 266, 198, 78), { { "China", "ChinaL", { 54 } } }),
            piece ("kick", "Kick", ellipse (990, 745, 150, 160), { { "Right foot", "KDrumR", { 36 } }, { "Left foot", "KDrumL", { 35 } } }),
            piece ("tom1", "Tom 1", drum (888, 396, 92, 54, 92), { { "Tom 1", "Tom1", { 50, 48 } } }),
            piece ("tom2", "Tom 2", drum (1113, 406, 113, 64, 113), { { "Tom 2", "Tom2", { 47, 45 } } }),
            piece ("ftom1", "Floor tom 1", drum (1273, 575, 132, 50, 233), { { "Floor tom 1", "FTom1", { 43 } } }),
            piece ("ftom2", "Floor tom 2", drum (1510, 688, 182, 80, 283), { { "Floor tom 2", "FTom2", { 41 } } }),
            piece ("hihat", "Hi-hat", ellipse (502, 447, 137, 45),
                   { { "Closed", "HihatClosed", { 42 } }, { "Open", "HihatOpen", { 46 } }, { "Pedal", "HihatPedal", { 44 } },
                     { "Half open", "HihatSemiOpen", { 80 } }, { "Closed, no pedal", "HihatClosedNoPedal", { 78 } },
                     { "Pedal hit", "HihatPedalHit", { 73 } } }, 1, 0.62f),
            piece ("hatpedal", "Hi-hat pedal", polygon ({ { 545, 930 }, { 605, 925 }, { 615, 1085 }, { 555, 1090 } }),
                   { { "Pedal", "HihatPedal", { 44 } } }, -1, 0.0f, "hihat"),
            piece ("kickpedalL", "Kick pedal, left foot", polygon ({ { 625, 910 }, { 670, 905 }, { 728, 1070 }, { 680, 1080 } }),
                   { { "Left foot", "KDrumL", { 35 } } }, -1, 0.0f, "kick"),
            piece ("kickpedal", "Kick pedal, right foot", polygon ({ { 970, 868 }, { 1028, 868 }, { 1030, 1032 }, { 968, 1032 } }),
                   { { "Right foot", "KDrumR", { 36 } } }, -1, 0.0f, "kick"),
            piece ("snare", "Snare", drum (800, 578, 138, 60, 108),
                   { { "Snare", "Snare", { 38 } }, { "Rimshot", "SnareRimShot", { 40 } }, { "Side stick", "SnareRim", { 37 } },
                     { "Rest", "SnareRest", { 39 } } }, 1, 0.83f),
        };

        return k;
    }

    KitDefinition drs()
    {
        KitDefinition k;
        k.id = "drs";
        k.name = "DRS";
        k.folder = "DRS";
        k.description = "A handmade orange kit lent by Jes Eiler of DRS Drums, with Paiste cymbals, recorded with sticks and with "
                        "brushes: one rack tom, two floor toms. Warm and open: jazz, funk, soul, pop, indie, rock.";
        k.credit = "DRSKit 2.1 by DrumGizmo.org with DRS Drums, sampled by Deva and Lars Muldjord. CC BY 4.0";
        k.url = "https://drumgizmo.org/kits/DRSKit/DRSKit2_1.zip";
        k.zipBytes = 2803397710;
        k.md5 = "8c4d4b61ad9d354b3b845edd5da9c133";
        k.kitFile = "DRSKit/DRSKit_full.xml";
        k.picture = "drskit_jpg";
        k.pictureCredit = "Picture: generated with an image AI for Nightshift Drums, from a description of the kit";
        k.pictureWidth = 2000;
        k.pictureHeight = 1116;

        k.pieces = {
            piece ("crashL", "Crash, left", ellipse (589, 176, 164, 54), { { "Crash", "Crash_left_shank", { 49 } }, { "Tip", "Crash_left_tip", { 54 } } }),
            piece ("crashR", "Crash, right", ellipse (1274, 214, 188, 86), { { "Crash", "Crash_right_shank", { 57 } }, { "Tip", "Crash_right_tip", { 55 } } }),
            piece ("ride", "Ride", ellipse (1577, 375, 219, 100),
                   { { "Ride", "Ride_tip", { 51 } }, { "Shank", "Ride_shank", { 66 } }, { "Chain", "Ride_tip_chain", { 73 } }, { "Rest", "Ride_rest", { 63 } } }),
            piece ("bell", "Ride bell", ellipse (1567, 330, 42, 24),
                   { { "Bell", "Ride_tip_bell", { 53 } }, { "Bell, shank", "Ride_shank_bell", { 68 } }, { "Bell, chain", "Ride_tip_bell_chain", { 70 } } },
                   -1, 0.0f, "ride"),
            piece ("kick", "Kick", ellipse (990, 670, 195, 215), { { "Kick", "Kdrum_with_contact", { 36 } }, { "Beater off", "Kdrum_without_contact", { 35 } } }),
            piece ("tom1", "Tom", drum (797, 345, 117, 50, 80), { { "Tom", "Tom1", { 50, 48 } } }),
            piece ("ftom1", "Floor tom 1", drum (1315, 534, 137, 46, 260), { { "Floor tom 1", "Tom2", { 47, 45 } } }),
            piece ("ftom2", "Floor tom 2", drum (1566, 634, 189, 66, 350), { { "Floor tom 2", "Tom3", { 43, 41 } } }),
            piece ("hihat", "Hi-hat", ellipse (450, 395, 150, 42),
                   { { "Closed", "Hihat_closed", { 42 } }, { "Open", "Hihat_open", { 46, 58 } }, { "Pedal", "Hihat_foot", { 44 } },
                     { "Half open", "Hihat_semi_open", { 23 } }, { "Closed, shank", "Hihat_closed_shank", { 22 } }, { "Open, tip", "Hihat_open_tip", { 61 } } },
                   1, 0.62f),
            piece ("hatpedal", "Hi-hat pedal", polygon ({ { 505, 915 }, { 550, 910 }, { 630, 1050 }, { 585, 1070 } }),
                   { { "Pedal", "Hihat_foot", { 44 } } }, -1, 0.0f, "hihat"),
            piece ("kickpedal", "Kick pedal", polygon ({ { 968, 868 }, { 1030, 868 }, { 1032, 1022 }, { 966, 1022 } }),
                   { { "Kick", "Kdrum_with_contact", { 36 } } }, -1, 0.0f, "kick"),
            piece ("snare", "Snare", drum (760, 530, 148, 50, 120),
                   { { "Snare", "Snare", { 38 } }, { "Rest", "Snare_rest", { 40 } }, { "Side stick", "Snare_rim", { 37 } },
                     { "Brush sweep", "Snare_circle_whisker", { 69 } } }),
        };

        k.brushes = { { "Snare", "Snare_whisker" }, { "Tom1", "Tom1_whisker" }, { "Tom2", "Tom2_whisker" }, { "Tom3", "Tom3_whisker" },
                      { "Hihat_closed", "Hihat_closed_whisker" }, { "Hihat_open", "Hihat_open_whisker" }, { "Ride_tip", "Ride_whisker" },
                      { "Crash_left_shank", "Crash_left_whisker" }, { "Crash_right_shank", "Crash_right_whisker" } };
        return k;
    }

    KitDefinition muldjord()
    {
        KitDefinition k;
        k.id = "muldjord";
        k.name = "Muldjord";
        k.folder = "Muldjord";
        k.description = "Lars Muldjord's Tama Superstar, recorded for a metal album: two kicks, three rack toms and a floor tom, two rides, "
                        "two crashes and a china. Big and roomy: heavy rock, metal, doom, stoner, grunge.";
        k.credit = "MuldjordKit 3 by Lars Muldjord, DrumGizmo.org. CC BY 4.0";
        k.url = "https://drumgizmo.org/kits/MuldjordKit/MuldjordKit3.zip";
        k.zipBytes = 2536576789;
        k.md5 = "8a66a3e90bbf15687b2d34fd355024f2";
        k.kitFile = "MuldjordKit3/MuldjordKit3.xml";
        k.picture = "muldjordkit_jpg";
        k.pictureCredit = "Picture: generated with an image AI for Nightshift Drums, from a description of the kit";
        k.pictureWidth = 2000;
        k.pictureHeight = 1116;

        // The hi-hat's pedal is in the picture, but the kit has no pedal hits, so it isn't a piece
        k.pieces = {
            piece ("rideL", "Ride, left", ellipse (338, 259, 176, 61, 6), { { "Ride", "RideL", { 59 } } }),
            piece ("bellL", "Left ride bell", ellipse (348, 240, 40, 18), { { "Bell", "RideLBell", { 55 } } }, -1, 0.0f, "rideL"),
            piece ("crashL", "Crash, left", ellipse (696, 176, 150, 56, 3), { { "Crash", "CrashL", { 49 } } }),
            piece ("crashR", "Crash, right", ellipse (1224, 174, 146, 55, -2), { { "Crash", "CrashR", { 57 } } }),
            piece ("china", "China", ellipse (1547, 230, 175, 78, -5), { { "China", "China", { 52 } } }),
            piece ("rideR", "Ride, right", ellipse (1702, 495, 210, 106, -7), { { "Ride", "RideR", { 51 } } }),
            piece ("bellR", "Right ride bell", ellipse (1694, 478, 46, 22), { { "Bell", "RideRBell", { 53 } } }, -1, 0.0f, "rideR"),
            piece ("kickL", "Kick, left", ellipse (800, 728, 175, 205), { { "Kick", "KdrumL", { 35 } } }),
            piece ("kickR", "Kick, right", ellipse (1217, 730, 175, 205), { { "Kick", "KdrumR", { 36 } } }),
            piece ("tom1", "Tom 1", drum (788, 374, 90, 54, 110), { { "Tom 1", "Tom1", { 50 } } }),
            piece ("tom2", "Tom 2", drum (1001, 371, 101, 63, 120), { { "Tom 2", "Tom2", { 48 } } }),
            piece ("tom3", "Tom 3", drum (1244, 380, 118, 78, 122), { { "Tom 3", "Tom3", { 47, 45 } } }),
            piece ("tom4", "Floor tom", drum (1525, 718, 173, 83, 269), { { "Floor tom", "Tom4", { 43, 41 } } }),
            piece ("hihat", "Hi-hat", ellipse (461, 474, 132, 46, -2), { { "Closed", "HihatClosed", { 42 } }, { "Open", "HihatOpen", { 46 } } }, 1, 0.62f),
            piece ("kickpedalL", "Left kick pedal", polygon ({ { 798, 896 }, { 842, 896 }, { 858, 1054 }, { 812, 1054 } }),
                   { { "Kick", "KdrumL", { 35 } } }, -1, 0.0f, "kickL"),
            piece ("kickpedalR", "Right kick pedal", polygon ({ { 1184, 898 }, { 1230, 898 }, { 1220, 1056 }, { 1172, 1056 } }),
                   { { "Kick", "KdrumR", { 36 } } }, -1, 0.0f, "kickR"),
            piece ("snare", "Snare", drum (748, 610, 142, 64, 120), { { "Snare", "Snare", { 38 } }, { "Side stick", "SnareRest", { 37 } } }),
        };

        return k;
    }
}

namespace
{
    std::vector<KitDefinition>& kits()
    {
        static std::vector<KitDefinition> all = []
        {
            std::vector<KitDefinition> k;
            k.reserve (32);         // room for test kits, so no definition ever moves
            k.push_back (crocell());
            k.push_back (drs());
            k.push_back (muldjord());
            return k;
        }();

        return all;
    }
}

const std::vector<KitDefinition>& catalog()
{
    return kits();
}

void addKitForTests (KitDefinition def)
{
    if (kits().size() < kits().capacity() && findKit (def.id) == nullptr)
        kits().push_back (std::move (def));
}

const KitDefinition* findKit (const juce::String& id)
{
    for (auto& k : catalog())
        if (k.id == id)
            return &k;

    return nullptr;
}

std::optional<float> picturedPan (const KitDefinition& def, const Mic& mic)
{
    if (mic.kind != Mic::Kind::close || mic.owner.isEmpty())
        return {};

    for (auto& piece : def.pieces)
        for (auto& a : piece.articulations)
            if (a.instrument.equalsIgnoreCase (mic.owner) && piece.shape.polygon.empty())
            {
                if (piece.id.startsWith ("kick") || piece.id == "snare")
                    return 0.0f;

                return juce::jlimit (-0.75f, 0.75f, (piece.shape.cx / def.pictureWidth - 0.5f) * 2.0f * 0.75f);
            }

    return {};
}

juce::File kitFolderFor (const juce::File& kitsFolder, const KitDefinition& def)
{
    return kitsFolder.getChildFile (def.folder);
}

juce::File defaultKitsFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Application Support").getChildFile ("Nightshift Drums").getChildFile ("Kits");
   #elif JUCE_WINDOWS
    // Under Programs, beside the per-user plugin folder: Windows doesn't move it to a private copy
    // for programs started from a packaged app, as it does the rest of AppData
    return juce::File::getSpecialLocation (juce::File::windowsLocalAppData)
               .getChildFile ("Programs").getChildFile ("Nightshift Drums").getChildFile ("Kits");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Nightshift Drums").getChildFile ("Kits");
   #endif
}

//==============================================================================
NoteMap NoteMap::build (const KitDefinition& def, const KitIndex& kit, bool brushes)
{
    NoteMap map;

    for (size_t p = 0; p < def.pieces.size(); ++p)
    {
        const auto& piece = def.pieces[p];

        for (size_t a = 0; a < piece.articulations.size(); ++a)
        {
            auto name = piece.articulations[a].instrument;

            if (brushes)
                if (auto it = def.brushes.find (name); it != def.brushes.end())
                    name = it->second;

            const int instrument = kit.indexOf (name);

            if (instrument < 0)
                continue;

            for (int note : piece.articulations[a].notes)
                if (juce::isPositiveAndBelow (note, 128) && map.notes[(size_t) note].instrument < 0)
                    map.notes[(size_t) note] = { instrument, (int) p, (int) a };
        }
    }

    // A "...Stopped" instrument stops the one it's named after
    map.chokes.assign (kit.instruments.size(), {});

    for (size_t i = 0; i < kit.instruments.size(); ++i)
    {
        const auto& name = kit.instruments[i].name;

        for (auto word : { "Stopped", "Stop", "Choke" })
            if (name.endsWithIgnoreCase (word))
                if (const int target = kit.indexOf (name.dropLastCharacters ((int) std::strlen (word))); target >= 0)
                    map.chokes[i].push_back (target);
    }

    return map;
}

} // namespace drums
