// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "KitFile.h"

namespace drums
{

struct ImportSource
{
    juce::String id, name, source;  // the kit's id, its name, and its credit line ("CrocellKit 1.1 by DrumGizmo.org, CC BY 4.0")
    juce::String kitFile;           // the DrumGizmo kit file inside the zip: "CrocellKit/CrocellKit_full.xml"
};

/** Makes a Nightshift Drums kit from a DrumGizmo kit's zip, reading straight from the zip (ZIP64
    too): every instrument the kit file lists, every hit, every mic, as recorded. Samples are kept
    at 24 bits, each hit scaled to its own peak, so a ghost note keeps as much detail as a rimshot.

    It also works out the kit's default mix, the way an engineer would set up the faders: where
    each close mic's drum sits between the overheads, how loud each close mic is against them,
    and which mics are the other way round (a snare's bottom mic, a kick's outside one).

    Writes into a folder beside `destination` and puts it in place only when it's complete.
    Returns "" or what went wrong, in words. */
juce::String importDrumGizmo (const juce::File& zip, const ImportSource&, const juce::File& destination,
                              std::function<void (float)> progress = nullptr, std::function<bool()> shouldStop = nullptr);

/** Which kind of mic a DrumGizmo channel is, by its name. */
Mic::Kind micKindFor (const juce::String& channelName);

/** Where an overhead or room mic sits by its name: -1 left, 1 right, 0 the middle. */
float micSideFor (const juce::String& channelName);

} // namespace drums
