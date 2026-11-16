// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "KitFile.h"

namespace drums
{

juce::String Mic::kindName (Kind k)
{
    switch (k)
    {
        case Kind::overhead:    return "overhead";
        case Kind::room:        return "room";
        case Kind::trigger:     return "trigger";
        case Kind::close:       break;
    }

    return "close";
}

Mic::Kind Mic::kindFromName (const juce::String& s)
{
    return s == "overhead" ? Kind::overhead : s == "room" ? Kind::room : s == "trigger" ? Kind::trigger : Kind::close;
}

//==============================================================================
int KitIndex::indexOf (const juce::String& instrumentName) const
{
    for (size_t i = 0; i < instruments.size(); ++i)
        if (instruments[i].name.equalsIgnoreCase (instrumentName))
            return (int) i;

    return -1;
}

int KitIndex::micIndex (const juce::String& micName) const
{
    for (size_t i = 0; i < mics.size(); ++i)
        if (mics[i].name.equalsIgnoreCase (micName))
            return (int) i;

    return -1;
}

juce::var KitIndex::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", format);
    o->setProperty ("id", id);
    o->setProperty ("name", name);
    o->setProperty ("source", source);
    o->setProperty ("sampleRate", sampleRate);
    o->setProperty ("dataBytes", dataBytes);
    o->setProperty ("namedFromAudience", namedFromAudience);
    o->setProperty ("mixTrim", mixTrim);

    juce::Array<juce::var> micList;

    for (auto& m : mics)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("name", m.name);
        mo->setProperty ("kind", Mic::kindName (m.kind));
        mo->setProperty ("pan", m.pan);
        mo->setProperty ("level", m.level);
        mo->setProperty ("invert", m.invert);
        mo->setProperty ("muted", m.muted);
        mo->setProperty ("owner", m.owner);
        micList.add (juce::var (mo));
    }

    o->setProperty ("mics", micList);
    juce::Array<juce::var> list;

    for (auto& in : instruments)
    {
        auto* io = new juce::DynamicObject();
        io->setProperty ("name", in.name);
        io->setProperty ("group", in.group);

        juce::String own;

        for (bool b : in.own)
            own << (b ? "1" : "0");

        io->setProperty ("own", own);

        if (in.layered)
            io->setProperty ("layered", true);

        juce::Array<juce::var> hits;

        for (auto& h : in.hits)
            hits.add (juce::Array<juce::var> { h.offset, h.frames, h.level, h.scale });

        io->setProperty ("hits", hits);
        list.add (juce::var (io));
    }

    o->setProperty ("instruments", list);
    return juce::var (o);
}

std::optional<KitIndex> KitIndex::fromVar (const juce::var& v, juce::String& error)
{
    if ((int) v.getProperty ("format", 0) != format)
    {
        error = "it was made by a different version of Nightshift Drums";
        return {};
    }

    KitIndex k;
    k.id = v.getProperty ("id", {}).toString();
    k.name = v.getProperty ("name", {}).toString();
    k.source = v.getProperty ("source", {}).toString();
    k.sampleRate = (double) v.getProperty ("sampleRate", 0.0);
    k.dataBytes = (juce::int64) v.getProperty ("dataBytes", 0);

    k.namedFromAudience = (bool) v.getProperty ("namedFromAudience", false);
    k.mixTrim = (float) v.getProperty ("mixTrim", 0.0);

    if (auto* micList = v.getProperty ("mics", {}).getArray())
        for (auto& m : *micList)
            k.mics.push_back ({ m.getProperty ("name", {}).toString(), Mic::kindFromName (m.getProperty ("kind", {}).toString()),
                                (float) m.getProperty ("pan", 0.0), (float) m.getProperty ("level", 0.0),
                                (bool) m.getProperty ("invert", false), (bool) m.getProperty ("muted", false),
                                m.getProperty ("owner", {}).toString() });

    if (auto* list = v.getProperty ("instruments", {}).getArray())
    {
        for (auto& i : *list)
        {
            Instrument in;
            in.name = i.getProperty ("name", {}).toString();
            in.group = i.getProperty ("group", {}).toString();
            in.layered = (bool) i.getProperty ("layered", false);

            const auto own = i.getProperty ("own", {}).toString();

            for (int m = 0; m < (int) k.mics.size(); ++m)
                in.own.push_back (own[m] == '1');

            if (auto* hits = i.getProperty ("hits", {}).getArray())
                for (auto& h : *hits)
                    if (auto* a = h.getArray(); a != nullptr && a->size() == 4)
                        in.hits.push_back ({ (juce::int64) (*a)[0], (int) (*a)[1], (float) (*a)[2], (float) (double) (*a)[3] });

            k.instruments.push_back (std::move (in));
        }
    }

    if (k.sampleRate < 8000.0 || k.mics.empty() || k.mics.size() > 64 || k.instruments.empty())
    {
        error = "its kit.json is incomplete";
        return {};
    }

    for (auto& in : k.instruments)
        for (auto& h : in.hits)
            if (h.offset < 0 || h.frames <= 0 || h.offset + (juce::int64) h.frames * k.frameBytes() > k.dataBytes)
            {
                error = "its kit.json points past the end of its samples";
                return {};
            }

    return k;
}

bool KitIndex::save (const juce::File& folder) const
{
    const auto file = indexFile (folder), temp = file.getSiblingFile ("kit.json.part");
    return temp.replaceWithText (juce::JSON::toString (toVar(), false)) && temp.moveFileTo (file);
}

std::optional<KitIndex> KitIndex::load (const juce::File& folder, juce::String& error)
{
    const auto file = indexFile (folder);

    if (! file.existsAsFile())
    {
        error = "there's no kit in " + folder.getFullPathName();
        return {};
    }

    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    auto kit = fromVar (parsed, error);

    if (kit.has_value() && dataFile (folder).getSize() < kit->dataBytes)
    {
        error = "its samples.bin is missing or cut short";
        return {};
    }

    return kit;
}

//==============================================================================
std::unique_ptr<LoadedKit> LoadedKit::open (const juce::File& folder, double preloadSeconds, juce::String& error,
                                            std::function<bool()> shouldStop, std::function<void (float)> progress)
{
    auto index = KitIndex::load (folder, error);

    if (! index.has_value())
        return {};

    std::unique_ptr<LoadedKit> k (new LoadedKit());
    k->kit = std::move (*index);
    k->dir = folder;
    k->map = std::make_unique<juce::MemoryMappedFile> (KitIndex::dataFile (folder), juce::MemoryMappedFile::readOnly, false);

    if (k->map->getData() == nullptr || (juce::int64) k->map->getSize() < k->kit.dataBytes)
    {
        error = "couldn't open its samples (" + KitIndex::dataFile (folder).getFullPathName() + ")";
        return {};
    }

    k->base = static_cast<const juce::uint8*> (k->map->getData());
    return k->preloadHits (preloadSeconds, error, std::move (shouldStop), std::move (progress)) ? std::move (k) : nullptr;
}

std::unique_ptr<LoadedKit> LoadedKit::fromMemory (KitIndex index, juce::MemoryBlock samples, double preloadSeconds, juce::String& error)
{
    if ((juce::int64) samples.getSize() < index.dataBytes || index.mics.empty())
    {
        error = "its samples are cut short";
        return {};
    }

    std::unique_ptr<LoadedKit> k (new LoadedKit());
    k->kit = std::move (index);
    k->samplesInMemory = std::move (samples);
    k->base = static_cast<const juce::uint8*> (k->samplesInMemory.getData());
    return k->preloadHits (preloadSeconds, error, nullptr, nullptr) ? std::move (k) : nullptr;
}

bool LoadedKit::preloadHits (double preloadSeconds, juce::String& error, std::function<bool()> shouldStop, std::function<void (float)> progress)
{
    preload = std::max (256, juce::roundToInt (preloadSeconds * kit.sampleRate));

    // Every hit's start, in one block
    const int frameBytes = kit.frameBytes();
    size_t total = 0;
    int hitCount = 0;

    for (auto& in : kit.instruments)
    {
        starts.emplace_back();

        for (auto& h : in.hits)
        {
            starts.back().push_back (total);
            total += (size_t) std::min (h.frames, preload) * (size_t) frameBytes;
            ++hitCount;
        }
    }

    try
    {
        memory.setSize (std::max<size_t> (total, 1), false);
    }
    catch (const std::bad_alloc&)
    {
        error = "there isn't enough memory to load it";
        return false;
    }

    int done = 0;

    for (size_t i = 0; i < kit.instruments.size(); ++i)
    {
        const auto& in = kit.instruments[i];

        for (size_t h = 0; h < in.hits.size(); ++h)
        {
            if (shouldStop != nullptr && shouldStop())
            {
                error = "stopped";
                return false;
            }

            const auto bytes = (size_t) std::min (in.hits[h].frames, preload) * (size_t) frameBytes;
            std::memcpy (static_cast<juce::uint8*> (memory.getData()) + starts[i][h], mapped (in.hits[h], 0), bytes);

            if (progress != nullptr && (++done % 16) == 0)
                progress ((float) done / (float) std::max (1, hitCount));
        }
    }

    if (progress != nullptr)
        progress (1.0f);

    return true;
}

} // namespace drums
