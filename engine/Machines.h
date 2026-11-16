// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "Catalog.h"

/** Nightshift Drums' own drum machines, the Nightshift 808 and 909: NightShift's synthesised voices
    (Machines.cpp has them as NightShift has them), made into a kit in memory when one is chosen. No
    samples, nothing to download. Each drum is in three layers that hit harder in tone as well as level,
    two takes a layer, as NightShift plays them; they all come out of one mic, "Direct". */
namespace drums::machines
{
    constexpr int layers = 3, takes = 2;

    /** Each layer's level, dB under the hardest. Velocity crosses from one layer to the next where
        NightShift's does (at 55 and 101), on the engine's velocity curve. */
    extern const float layerLevels[layers];

    /** The machine's mic and drums, with no sound made yet: for the mixer, straight away. */
    KitIndex outline (const KitDefinition&);

    /** Every drum made, in memory, ready to play (a quarter of a second or so). Null, with `error`
        said in words, if the definition isn't a drum machine or it was stopped. */
    std::unique_ptr<LoadedKit> build (const KitDefinition&, double preloadSeconds, juce::String& error,
                                      std::function<bool()> shouldStop = nullptr, std::function<void (float)> progress = nullptr);
}
