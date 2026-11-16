// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "KitFile.h"

/** The kits Nightshift Drums knows: where to download each, and how it's played. Each drum and
    cymbal is a piece; each piece has articulations (a snare's head, rim and side stick; a
    hi-hat's closed, open and pedal), each one of the kit's recorded instruments on one or more
    MIDI notes. The notes follow General MIDI where it has a drum for them (36 kick, 38 snare,
    42 closed hi-hat, 49 crash...), so NightShift's AI drummer and any GM drum part play the
    right drums; the extras sit on notes General MIDI leaves free. */
namespace drums
{

/** Where a piece is in its kit's picture, in the picture's own coordinates: an ellipse (a cymbal,
    or a drum's head, with its shell `depth` below it), or a polygon (a pedal). */
struct Shape
{
    float cx = 0, cy = 0, rx = 0, ry = 0, rotation = 0, depth = 0;
    std::vector<juce::Point<float>> polygon;

    juce::Path path() const;
    bool contains (juce::Point<float>) const;

    /** How far out a point is on the head: 0 the centre, 1 the edge (ellipses only; -1 outside). */
    float radiusAt (juce::Point<float>) const;
};

struct Articulation
{
    juce::String name;              // "Rimshot"
    juce::String instrument;        // the kit's own instrument: "SnareRimShot"
    std::vector<int> notes;         // the first is the one Nightshift Drums plays it with
};

struct Piece
{
    juce::String id, name;          // "snare", "Snare"
    std::vector<Articulation> articulations;     // the first is what a click in its middle plays
    Shape shape;
    int edge = -1;                  // the articulation a click near its edge plays (a hi-hat opens, a snare's rim)
    float edgeFrom = 0.0f;          // ...from this far out (0..1)
    juce::String lightsWith;        // another piece this one lights up with (a ride's bell with the ride)
};

struct KitDefinition
{
    juce::String id, name, folder;  // "crocell", "Crocell", "Crocell"
    juce::String description;       // what it is and what it suits, in words
    juce::String credit;            // "CrocellKit 1.1 by DrumGizmo.org, CC BY 4.0"
    juce::String url;               // the DrumGizmo zip
    juce::int64 zipBytes = 0;
    juce::String md5;               // the zip's, as drumgizmo.org gives it
    juce::String kitFile;           // the kit file inside the zip

    // The kit view
    juce::String picture;           // a BinaryData resource name; "" draws the kit instead
    juce::String pictureCredit;     // the photo's own credit and licence
    float pictureWidth = 1200, pictureHeight = 675;     // the space the shapes are in
    std::vector<Piece> pieces;

    /** The same pieces with brushes (DRS was recorded with both): articulations by instrument name. */
    std::map<juce::String, juce::String> brushes;

    juce::String importId() const   { return id; }
};

const std::vector<KitDefinition>& catalog();
const KitDefinition* findKit (const juce::String& id);

/** drumstool's tests: a kit of their own, played like the real ones. */
void addKitForTests (KitDefinition);

/** Where a close mic goes in the default mix: where its drum is in the kit's picture, as the
    drummer sits (kick and snare in the middle). Nothing when the picture doesn't show its drum. */
std::optional<float> picturedPan (const KitDefinition&, const Mic&);

/** The kit folder's name for a definition, inside the kits folder. */
juce::File kitFolderFor (const juce::File& kitsFolder, const KitDefinition&);

/** Where Nightshift Drums keeps its kits by default: %LOCALAPPDATA%\Programs\Nightshift Drums\Kits on
    Windows (beside its plugin folder), ~/Library/Application Support/Nightshift Drums/Kits on a Mac. */
juce::File defaultKitsFolder();

//==============================================================================
/** What a note plays in a loaded kit: an instrument of the kit and the piece it belongs to. Built
    when a kit loads, from its definition and what the import found. */
struct NoteMap
{
    struct Target
    {
        int instrument = -1;        // index into the kit's instruments, -1 for nothing
        int piece = -1;             // index into the definition's pieces
        int articulation = -1;
    };

    std::array<Target, 128> notes {};

    /** Instruments that stop another's ringing when played: a stopped crash stops its crash. */
    std::vector<std::vector<int>> chokes;

    static NoteMap build (const KitDefinition&, const KitIndex&, bool brushes);
};

} // namespace drums
