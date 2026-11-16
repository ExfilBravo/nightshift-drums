// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include <JuceHeader.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "common/TextAudit.h"
#include "plugin/DrumsProcessor.h"

/** The standalone build of Nightshift Drums. With no arguments it is the usual JUCE standalone: a
    window with the plugin, an audio device and MIDI inputs (its Options menu). With

        "Nightshift Drums.exe" --snapshot <out.png> [--page kit|mixer|grooves|kits] [--textaudit [--alltext]]

    it builds the window offscreen, lets the kit load and the window settle, writes a PNG and quits:
    a way to look at the window without one opening. --textaudit lists every piece of text that's too
    small, squeezed, cut short, clipped or faint (--alltext: every piece). */
extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

namespace
{

juce::String after (const juce::StringArray& tokens, const juce::String& flag)
{
    const int i = tokens.indexOf (flag);
    return i >= 0 && i + 1 < tokens.size() ? tokens[i + 1].unquoted() : juce::String();
}

class SnapshotRunner : private juce::Timer
{
public:
    explicit SnapshotRunner (const juce::StringArray& tokens)
        : outFile (after (tokens, "--snapshot")), page (after (tokens, "--page")),
          textAudit (tokens.contains ("--textaudit")), allText (tokens.contains ("--alltext"))
    {
        processor.reset (createPluginFilter());
        processor->setPlayConfigDetails (0, 2, sampleRate, blockSize);
        processor->prepareToPlay (sampleRate, blockSize);
        block.setSize (2, blockSize);
        startTimerHz (30);
    }

    ~SnapshotRunner() override
    {
        stopTimer();
        editor = nullptr;

        if (processor != nullptr)
            processor->releaseResources();
    }

private:
    void timerCallback() override
    {
        // Run the processor (a hit now and then, so meters and lights show a working kit), and give
        // the kit up to ten seconds to load before the picture's taken
        auto* drums = dynamic_cast<drums::DrumsProcessor*> (processor.get());

        for (int rep = 0; rep < 3; ++rep)
        {
            juce::MidiBuffer midi;

            if ((frames + rep) % 12 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (10, frames % 24 < 12 ? 38 : 36, (juce::uint8) 110), 0);

            block.clear();
            processor->processBlock (block, midi);
        }

        ++frames;

        if (editor == nullptr && (frames > 300 || drums == nullptr || drums->getKitState() != drums::DrumsProcessor::KitState::loading))
        {
            editor.reset (processor->createEditorIfNeeded());

            if (editor == nullptr)
            {
                std::cout << "no editor" << std::endl;
                finish (1);
                return;
            }

            if (page.isNotEmpty())
                editor->getProperties().set ("page", page);

            editor->setVisible (true);
            shownAt = frames;
        }

        if (editor == nullptr || frames < shownAt + 30)
            return;

        stopTimer();
        juce::Image image;

        if (textAudit)
            std::cout << nightshift::textaudit::report (nightshift::textaudit::collect (*editor, &image), allText);
        else
            image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

        outFile.deleteFile();
        juce::FileOutputStream stream (outFile);
        const bool ok = stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
        std::cout << (ok ? "snapshot " : "snapshot failed ") << outFile.getFullPathName()
                  << "  (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
        finish (ok ? 0 : 1);
    }

    static void finish (int code)
    {
        if (auto* app = juce::JUCEApplication::getInstance())
        {
            app->setApplicationReturnValue (code);
            app->quit();
        }
    }

    static constexpr double sampleRate = 48000.0;
    static constexpr int blockSize = 512;

    juce::File outFile;
    juce::String page;
    bool textAudit = false, allText = false;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::AudioBuffer<float> block;
    int frames = 0, shownAt = 0;
};

} // namespace

//==============================================================================
class DrumsStandaloneApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return JucePlugin_Name; }
    const juce::String getApplicationVersion() override    { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto tokens = juce::StringArray::fromTokens (commandLine, true);

        if (tokens.contains ("--snapshot"))
        {
            snapshot = std::make_unique<SnapshotRunner> (tokens);
            return;
        }

        juce::PropertiesFile::Options options;
        options.applicationName = getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Nightshift Drums";
        appProperties.setStorageParameters (options);

        window = std::make_unique<juce::StandaloneFilterWindow> (getApplicationName(), juce::Colour (0xff08090e),
                                                                 appProperties.getUserSettings(), false);
        window->setVisible (true);
    }

    void shutdown() override
    {
        snapshot = nullptr;
        window = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->pluginHolder->savePluginState();

        quit();
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::StandaloneFilterWindow> window;
    std::unique_ptr<SnapshotRunner> snapshot;
};

START_JUCE_APPLICATION (DrumsStandaloneApp)
