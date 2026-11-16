// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#pragma once

#include <JuceHeader.h>
#include "Catalog.h"
#include "Importer.h"

namespace drums
{

/** The kits on this computer, and getting the ones that aren't: one shared by every Nightshift Drums
    in a host (SharedResourcePointer), so two of them never fetch the same kit twice.

    Getting a kit downloads its DrumGizmo zip (carrying on where it stopped, if it was stopped),
    checks it against drumgizmo.org's MD5, makes the Nightshift Drums kit from it and deletes the
    zip. One kit at a time, on a thread of its own. Listeners hear about every change on the
    message thread. */
class Library : public juce::ChangeBroadcaster,
                private juce::Thread,
                private juce::AsyncUpdater
{
public:
    Library();
    ~Library() override;

    enum class State { missing, queued, downloading, checking, importing, ready, failed };

    struct Status
    {
        State state = State::missing;
        float progress = 0.0f;              // of the step it's on, 0..1
        juce::int64 bytes = 0, total = 0;   // downloaded so far, of the zip's size
        double bytesPerSecond = 0.0;
        juce::String message;               // why it failed
    };

    Status getStatus (const juce::String& kitId) const;
    bool isReady (const juce::String& kitId) const      { return getStatus (kitId).state == State::ready; }
    juce::File folderFor (const juce::String& kitId) const;

    /** Where the kits live (settable; remembered for every Nightshift Drums). */
    juce::File getKitsFolder() const;
    void setKitsFolder (const juce::File&);

    /** Starts getting a kit (after any already under way). */
    void install (const juce::String& kitId);

    /** Stops getting a kit; what's downloaded so far stays, to carry on from. */
    void cancel (const juce::String& kitId);

    /** Deletes an installed kit. */
    bool remove (const juce::String& kitId);

    /** Looks at the kits folder again. */
    void refresh();

    /** For tests: kits from this folder, not remembered (the settings file is never written). */
    void setKitsFolderForTests (const juce::File& folder)
    {
        {
            const juce::ScopedLock sl (lock);
            kitsFolder = folder;
        }

        scan();
    }

    /** For tests: get kits from these zips instead of the internet (kit id -> zip). */
    void useLocalZips (std::map<juce::String, juce::File> zips)     { const juce::ScopedLock sl (lock); localZips = std::move (zips); }

    /** Waits until nothing is queued or under way (tests and command-line tools). */
    bool waitUntilIdle (int timeoutMs);

private:
    mutable juce::CriticalSection lock;
    std::map<juce::String, Status> statuses;
    juce::StringArray queue;
    juce::String working, cancelled;
    std::map<juce::String, juce::File> localZips;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::File kitsFolder;

    void run() override;
    void handleAsyncUpdate() override       { sendChangeMessage(); }
    void set (const juce::String& kitId, std::function<void (Status&)>);
    juce::String fetch (const KitDefinition&, const juce::File& zip);
    juce::String getOne (const KitDefinition&);
    void scan();
};

} // namespace drums
