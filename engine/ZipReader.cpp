// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter. Copied from NightShift (src/audio/ZipReader).

#include "ZipReader.h"

namespace drums
{

namespace
{
    juce::uint16 u16 (const juce::uint8* p)    { return (juce::uint16) (p[0] | (p[1] << 8)); }
    juce::uint32 u32 (const juce::uint8* p)    { return (juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24); }
    juce::uint64 u64 (const juce::uint8* p)    { return (juce::uint64) u32 (p) | ((juce::uint64) u32 (p + 4) << 32); }

    constexpr juce::uint32 endOfDirectory = 0x06054b50, endOfDirectory64 = 0x06064b50, locator64 = 0x07064b50,
                           directoryEntry = 0x02014b50, localHeader = 0x04034b50;

    bool readAt (juce::FileInputStream& in, juce::int64 position, juce::MemoryBlock& into, size_t bytes)
    {
        into.setSize (bytes);
        return in.setPosition (position) && in.read (into.getData(), (int) bytes) == (int) bytes;
    }
}

ZipReader::ZipReader (const juce::File& f) : file (f)
{
    read();
}

void ZipReader::read()
{
    juce::FileInputStream in (file);
    const auto length = file.getSize();

    if (! in.openedOk() || length < 22)
        return;

    // The end-of-directory record, somewhere in the last 64 KB (after any comment)
    const auto tailStart = std::max<juce::int64> (0, length - 65557);
    juce::MemoryBlock tail;

    if (! readAt (in, tailStart, tail, (size_t) (length - tailStart)))
        return;

    const auto* t = static_cast<const juce::uint8*> (tail.getData());
    juce::int64 end = -1;

    for (auto i = (juce::int64) tail.getSize() - 22; i >= 0; --i)
        if (u32 (t + i) == endOfDirectory)
        {
            end = i;
            break;
        }

    if (end < 0)
        return;

    juce::uint64 count = u16 (t + end + 10), directorySize = u32 (t + end + 12), directoryOffset = u32 (t + end + 16);

    // ZIP64: the real numbers are in a record the locator just before points at
    if (count == 0xffff || directorySize == 0xffffffff || directoryOffset == 0xffffffff)
    {
        if (end < 20 || u32 (t + end - 20) != locator64)
            return;

        juce::MemoryBlock record;

        if (! readAt (in, (juce::int64) u64 (t + end - 20 + 8), record, 56) || u32 (static_cast<const juce::uint8*> (record.getData())) != endOfDirectory64)
            return;

        const auto* r = static_cast<const juce::uint8*> (record.getData());
        count = u64 (r + 32);
        directorySize = u64 (r + 40);
        directoryOffset = u64 (r + 48);
    }

    juce::MemoryBlock directory;

    if (directorySize > (juce::uint64) length || ! readAt (in, (juce::int64) directoryOffset, directory, (size_t) directorySize))
        return;

    const auto* d = static_cast<const juce::uint8*> (directory.getData());
    size_t pos = 0;

    for (juce::uint64 i = 0; i < count && pos + 46 <= directory.getSize(); ++i)
    {
        const auto* e = d + pos;

        if (u32 (e) != directoryEntry)
            break;

        const int nameLength = u16 (e + 28), extraLength = u16 (e + 30), commentLength = u16 (e + 32);

        if (pos + 46 + (size_t) (nameLength + extraLength) > directory.getSize())
            break;

        Entry entry;
        entry.method = u16 (e + 10);
        entry.compressedSize = u32 (e + 20);
        entry.size = u32 (e + 24);
        entry.headerOffset = u32 (e + 42);
        entry.name = juce::String::fromUTF8 (reinterpret_cast<const char*> (e + 46), nameLength).replaceCharacter ('\\', '/');

        // ZIP64 sizes and offset, for whichever didn't fit: in that order in the 0x0001 field
        for (int x = 0; x + 4 <= extraLength;)
        {
            const auto* field = e + 46 + nameLength + x;
            const int id = u16 (field), size = u16 (field + 2);

            if (id == 1)
            {
                int at = 4;

                auto take = [&] (juce::int64& value)
                {
                    if (value == 0xffffffff && at + 8 <= 4 + size)
                    {
                        value = (juce::int64) u64 (field + at);
                        at += 8;
                    }
                };

                take (entry.size);
                take (entry.compressedSize);
                take (entry.headerOffset);
            }

            x += 4 + size;
        }

        byName[entry.name.toLowerCase()] = (int) entries.size();
        entries.push_back (std::move (entry));
        pos += 46 + (size_t) (nameLength + extraLength + commentLength);
    }
}

int ZipReader::indexOf (const juce::String& name) const
{
    auto it = byName.find (name.replaceCharacter ('\\', '/').toLowerCase());
    return it != byName.end() ? it->second : -1;
}

std::unique_ptr<juce::InputStream> ZipReader::open (int index) const
{
    if (! juce::isPositiveAndBelow (index, (int) entries.size()))
        return {};

    const auto& entry = entries[(size_t) index];
    auto in = std::make_unique<juce::FileInputStream> (file);
    juce::MemoryBlock header;

    if (! in->openedOk() || ! readAt (*in, entry.headerOffset, header, 30)
         || u32 (static_cast<const juce::uint8*> (header.getData())) != localHeader)
        return {};

    const auto* h = static_cast<const juce::uint8*> (header.getData());
    const auto dataStart = entry.headerOffset + 30 + u16 (h + 26) + u16 (h + 28);
    auto data = std::make_unique<juce::SubregionStream> (in.release(), dataStart, entry.compressedSize, true);

    if (entry.method == 0)
        return data;

    if (entry.method == 8)
        return std::make_unique<juce::GZIPDecompressorInputStream> (data.release(), true, juce::GZIPDecompressorInputStream::deflateFormat, entry.size);

    return {};
}

} // namespace drums
