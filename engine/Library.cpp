// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "Library.h"

namespace drums
{

Library::Library() : juce::Thread ("Nightshift Drums kits")
{
    // Settings beside the kits, where every program that loads Nightshift Drums sees the same file
    juce::PropertiesFile::Options options;
    options.applicationName = "Nightshift Drums";
    options.filenameSuffix = "settings";
    settings = std::make_unique<juce::PropertiesFile> (defaultKitsFolder().getParentDirectory().getChildFile ("Nightshift Drums.settings"), options);

    const auto saved = settings->getValue ("kitsFolder");
    kitsFolder = saved.isNotEmpty() && juce::File::isAbsolutePath (saved) ? juce::File (saved) : defaultKitsFolder();

    scan();
    startThread (juce::Thread::Priority::normal);
}

Library::~Library()
{
    signalThreadShouldExit();
    notify();
    stopThread (10000);
}

//==============================================================================
Library::Status Library::getStatus (const juce::String& kitId) const
{
    // A drum machine is made as it's chosen: always here
    if (const auto* def = findKit (kitId); def != nullptr && def->isMachine())
        return Status { State::ready, 1.0f };

    const juce::ScopedLock sl (lock);
    auto it = statuses.find (kitId);
    return it != statuses.end() ? it->second : Status {};
}

juce::File Library::folderFor (const juce::String& kitId) const
{
    const auto* def = findKit (kitId);
    return def != nullptr ? kitFolderFor (getKitsFolder(), *def) : juce::File();
}

juce::File Library::getKitsFolder() const
{
    const juce::ScopedLock sl (lock);
    return kitsFolder;
}

void Library::setKitsFolder (const juce::File& folder)
{
    {
        const juce::ScopedLock sl (lock);
        kitsFolder = folder;
        settings->setValue ("kitsFolder", folder.getFullPathName());
        settings->saveIfNeeded();
    }

    refresh();
}

void Library::refresh()
{
    scan();
    triggerAsyncUpdate();
}

void Library::scan()
{
    const auto folder = getKitsFolder();

    for (auto& def : catalog())
    {
        if (def.isMachine())
            continue;

        juce::String error;
        const bool ready = KitIndex::load (kitFolderFor (folder, def), error).has_value();
        const juce::ScopedLock sl (lock);
        auto& s = statuses[def.id];

        if (def.id == working || queue.contains (def.id))
            continue;

        if (ready)
            s = Status { State::ready, 1.0f };
        else if (s.state == State::ready)
            s = Status {};

        // Part of its zip already downloaded: Get carries on from there
        if (s.state == State::missing)
        {
            const auto partial = folder.getChildFile (".downloads").getChildFile (juce::URL (def.url).getFileName());
            s.bytes = partial.existsAsFile() ? partial.getSize() : 0;
            s.total = def.zipBytes;
        }
    }
}

void Library::set (const juce::String& kitId, std::function<void (Status&)> change)
{
    {
        const juce::ScopedLock sl (lock);
        change (statuses[kitId]);
    }

    triggerAsyncUpdate();
}

//==============================================================================
void Library::install (const juce::String& kitId)
{
    if (const auto* def = findKit (kitId); def == nullptr || def->isMachine())
        return;

    {
        const juce::ScopedLock sl (lock);

        if (kitId == working || queue.contains (kitId))
            return;

        if (cancelled == kitId)
            cancelled.clear();

        queue.add (kitId);
        statuses[kitId] = Status { State::queued };
    }

    triggerAsyncUpdate();
    notify();
}

void Library::cancel (const juce::String& kitId)
{
    {
        const juce::ScopedLock sl (lock);
        queue.removeString (kitId);

        if (kitId == working)
            cancelled = kitId;
        else
            statuses[kitId] = Status {};
    }

    triggerAsyncUpdate();
}

bool Library::remove (const juce::String& kitId)
{
    if (const auto* def = findKit (kitId); def == nullptr || def->isMachine())
        return false;

    {
        const juce::ScopedLock sl (lock);

        if (kitId == working)
            return false;
    }

    const auto folder = folderFor (kitId);
    const bool ok = ! folder.exists() || folder.deleteRecursively();
    set (kitId, [] (Status& s) { s = Status {}; });
    return ok;
}

bool Library::waitUntilIdle (int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (juce::Time::getMillisecondCounter() < until)
    {
        {
            const juce::ScopedLock sl (lock);

            if (queue.isEmpty() && working.isEmpty())
                return true;
        }

        juce::Thread::sleep (50);
    }

    return false;
}

//==============================================================================
void Library::run()
{
    while (! threadShouldExit())
    {
        juce::String next;

        {
            const juce::ScopedLock sl (lock);

            if (! queue.isEmpty())
            {
                next = queue[0];
                queue.remove (0);
                working = next;
            }
        }

        if (next.isEmpty())
        {
            wait (-1);
            continue;
        }

        const auto error = getOne (*findKit (next));

        {
            const juce::ScopedLock sl (lock);
            auto& s = statuses[next];

            if (cancelled == next)
            {
                s = Status {};
                cancelled.clear();
            }
            else if (error.isEmpty())
            {
                s = Status { State::ready, 1.0f };
            }
            else
            {
                s = Status { State::failed };
                s.message = error;
            }

            working.clear();
        }

        triggerAsyncUpdate();
    }
}

juce::String Library::getOne (const KitDefinition& def)
{
    const auto folder = getKitsFolder();
    juce::File zip;
    bool local = false;

    {
        const juce::ScopedLock sl (lock);

        if (auto it = localZips.find (def.id); it != localZips.end())
        {
            zip = it->second;
            local = true;
        }
    }

    if (! folder.createDirectory())
        return "Couldn't make the kits folder, " + folder.getFullPathName() + ".";

    if (! local)
    {
        const auto downloads = folder.getChildFile (".downloads");
        downloads.createDirectory();
        zip = downloads.getChildFile (juce::URL (def.url).getFileName());

        if (auto error = fetch (def, zip); error.isNotEmpty())
            return error;

        set (def.id, [] (Status& s) { s.state = State::checking; s.progress = 0.0f; });

        if (def.md5.isNotEmpty() && ! juce::MD5 (zip).toHexString().equalsIgnoreCase (def.md5))
        {
            zip.deleteFile();
            return "The " + def.name + " download came out damaged (it doesn't match drumgizmo.org's checksum). Get it again.";
        }
    }

    set (def.id, [] (Status& s) { s.state = State::importing; s.progress = 0.0f; });

    ImportSource src { def.id, def.name, def.credit, def.kitFile };
    juce::uint32 lastTell = 0;

    const auto error = importDrumGizmo (zip, src, kitFolderFor (folder, def),
        [&] (float p)
        {
            if (const auto now = juce::Time::getMillisecondCounter(); now - lastTell > 150 || p >= 1.0f)
            {
                lastTell = now;
                set (def.id, [p] (Status& s) { s.progress = p; });
            }
        },
        [&]
        {
            const juce::ScopedLock sl (lock);
            return threadShouldExit() || cancelled == def.id;
        });

    if (error.isNotEmpty())
        return error;

    if (! local)
        zip.deleteFile();

    return {};
}

juce::String Library::fetch (const KitDefinition& def, const juce::File& zip)
{
    juce::int64 have = zip.existsAsFile() ? zip.getSize() : 0;

    if (have == def.zipBytes)
        return {};

    if (have > def.zipBytes)
    {
        zip.deleteFile();
        have = 0;
    }

    set (def.id, [&] (Status& s) { s.state = State::downloading; s.bytes = have; s.total = def.zipBytes; s.progress = (float) have / (float) def.zipBytes; });

    int status = 0;
    auto in = juce::URL (def.url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                         .withConnectionTimeoutMs (20000)
                                                         .withStatusCode (&status)
                                                         .withExtraHeaders (have > 0 ? "Range: bytes=" + juce::String (have) + "-" : juce::String()));

    if (in == nullptr)
        return "Couldn't reach drumgizmo.org to download " + def.name + ". Check the internet connection, then get it again.";

    if (status >= 400)
        return "drumgizmo.org didn't send " + def.name + " (it answered " + juce::String (status) + "). Try again later.";

    if (have > 0 && status != 206)
    {
        // It sent the whole file rather than the rest: start again
        zip.deleteFile();
        have = 0;
    }

    juce::FileOutputStream out (zip, 1 << 20);

    if (! out.openedOk())
        return "Couldn't save the download in " + zip.getParentDirectory().getFullPathName() + ".";

    juce::HeapBlock<char> buffer (1 << 20);
    juce::int64 got = have, mark = have;
    auto markTime = juce::Time::getMillisecondCounterHiRes();
    double rate = 0.0;

    while (! threadShouldExit())
    {
        {
            const juce::ScopedLock sl (lock);

            if (cancelled == def.id)
                return "Stopped.";
        }

        const int n = in->read (buffer, 1 << 20);

        if (n <= 0)
            break;

        if (! out.write (buffer, (size_t) n))
            return "Couldn't save the download (is the disk full?).";

        got += n;
        const auto now = juce::Time::getMillisecondCounterHiRes();

        if (now - markTime > 250.0)
        {
            rate = (double) (got - mark) * 1000.0 / (now - markTime);
            mark = got;
            markTime = now;
            set (def.id, [&] (Status& s) { s.bytes = got; s.progress = (float) ((double) got / (double) def.zipBytes); s.bytesPerSecond = rate; });
        }
    }

    out.flush();

    if (got < def.zipBytes)
        return "The " + def.name + " download stopped at " + juce::File::descriptionOfSizeInBytes (got) + " of "
               + juce::File::descriptionOfSizeInBytes (def.zipBytes) + ". Get it again to carry on from there.";

    return {};
}

} // namespace drums
