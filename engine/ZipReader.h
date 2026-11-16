// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter. Copied from NightShift (src/audio/ZipReader).

#pragma once

#include <JuceHeader.h>

namespace drums
{

/** Reads entries out of a zip, big ones included: ZIP64, which juce::ZipFile can't read (the
    largest DrumGizmo kit is 5.6 GB). Stored and deflated entries. */
class ZipReader
{
public:
    explicit ZipReader (const juce::File&);

    bool isOpen() const                         { return ! entries.empty(); }
    int getNumEntries() const                   { return (int) entries.size(); }
    juce::String getName (int index) const      { return entries[(size_t) index].name; }
    juce::int64 getSize (int index) const       { return entries[(size_t) index].size; }

    /** An entry by its path in the zip (any case), or -1. */
    int indexOf (const juce::String& name) const;

    /** An entry's contents, or null if it can't be read. */
    std::unique_ptr<juce::InputStream> open (int index) const;

private:
    struct Entry
    {
        juce::String name;
        juce::int64 headerOffset = 0, compressedSize = 0, size = 0;
        int method = 0;
    };

    juce::File file;
    std::vector<Entry> entries;
    std::map<juce::String, int> byName;

    void read();
};

} // namespace drums
