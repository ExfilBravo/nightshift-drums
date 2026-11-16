// SPDX-License-Identifier: GPL-3.0-or-later
// Nightshift Drums - Copyright (C) 2026 NightWriter.

#include "TestKit.h"

namespace drums::testkit
{

const std::vector<juce::String>& micNames()
{
    static const std::vector<juce::String> names { "OHLeft", "OHRight", "SnareTop", "KDrumInside" };
    return names;
}

const std::vector<Instrument>& instruments()
{
    static const std::vector<Instrument> all {
        { "Snare", "", { 0.01, 0.05, 0.2, 0.5, 1.0 }, 0.3, { true, true, true, false } },
        { "KDrumR", "", { 0.1, 0.5, 1.0 }, 0.25, { true, true, false, true } },
        { "HihatClosed", "hihat", { 0.2, 0.6, 1.0 }, 0.1, { true, true, false, false } },
        { "HihatOpen", "hihat", { 0.5, 1.0 }, 1.5, { true, true, false, false } },
    };

    return all;
}

float sample (int instrument, int hit, int mic, int frame)
{
    const auto& in = instruments()[(size_t) instrument];
    const double amplitude = std::sqrt (in.powers[(size_t) hit]) * (in.main[(size_t) mic] ? 0.8 : 0.15);
    const double freq = 180.0 + 40.0 * mic + 25.0 * instrument, phase = 0.37 * hit + 0.11 * mic;
    const double decay = std::exp (-(double) frame / (rate * in.seconds * 0.35));
    return (float) (amplitude * decay * std::sin (juce::MathConstants<double>::twoPi * freq * frame / rate + phase));
}

namespace
{
    juce::MemoryBlock wavOf (const juce::AudioBuffer<float>& audio)
    {
        juce::MemoryBlock block;

        {
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (block, false);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                          .withSampleRate (rate)
                                                          .withNumChannels (audio.getNumChannels())
                                                          .withBitsPerSample (32)
                                                          .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            jassert (writer != nullptr);
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
        }

        return block;
    }
}

bool writeZip (const juce::File& zip)
{
    juce::ZipFile::Builder builder;
    const auto now = juce::Time::getCurrentTime();
    auto add = [&] (const juce::MemoryBlock& data, const juce::String& path)
    {
        builder.addEntry (std::make_unique<juce::MemoryInputStream> (data, true), 6, path, now);
    };

    juce::String kit;
    kit << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<drumkit name=\"TestKit\" description=\"for tests\">\n  <channels>\n";

    for (auto& m : micNames())
        kit << "    <channel name=\"" << m << "\"/>\n";

    kit << "  </channels>\n  <instruments>\n";

    for (size_t i = 0; i < instruments().size(); ++i)
    {
        const auto& in = instruments()[i];
        kit << "    <instrument name=\"" << in.name << "\"" << (juce::String (in.group).isNotEmpty() ? juce::String (" group=\"") + in.group + "\"" : juce::String())
            << " file=\"" << in.name << "/" << in.name << ".xml\">\n";

        for (size_t m = 0; m < micNames().size(); ++m)
            kit << "      <channelmap in=\"" << micNames()[m] << "\" out=\"" << micNames()[m] << "\"" << (in.main[m] ? " main=\"true\"" : "") << "/>\n";

        kit << "    </instrument>\n";

        juce::String xml;
        xml << "<?xml version='1.0' encoding='UTF-8'?>\n<instrument version=\"2.0\" name=\"" << in.name << "\">\n <samples>\n";

        for (size_t h = 0; h < in.powers.size(); ++h)
        {
            const auto file = juce::String ("samples/") + juce::String ((int) h + 1) + "-" + in.name + ".wav";
            xml << "  <sample name=\"" << in.name << "-" << (int) h + 1 << "\" power=\"" << in.powers[h] << "\">\n";

            for (size_t m = 0; m < micNames().size(); ++m)
                xml << "   <audiofile channel=\"" << micNames()[m] << "\" file=\"" << file << "\" filechannel=\"" << (int) m + 1 << "\"/>\n";

            xml << "  </sample>\n";

            juce::AudioBuffer<float> audio ((int) micNames().size(), (int) (in.seconds * rate));

            for (int m = 0; m < audio.getNumChannels(); ++m)
                for (int f = 0; f < audio.getNumSamples(); ++f)
                    audio.setSample (m, f, sample ((int) i, (int) h, m, f));

            add (wavOf (audio), juce::String ("TestKit/") + in.name + "/" + file);
        }

        xml << " </samples>\n</instrument>\n";
        add (juce::MemoryBlock (xml.toRawUTF8(), xml.getNumBytesAsUTF8()), juce::String ("TestKit/") + in.name + "/" + in.name + ".xml");
    }

    kit << "  </instruments>\n</drumkit>\n";
    add (juce::MemoryBlock (kit.toRawUTF8(), kit.getNumBytesAsUTF8()), "TestKit/TestKit.xml");

    zip.deleteFile();
    juce::FileOutputStream out (zip);
    return out.openedOk() && builder.writeToStream (out, nullptr);
}

KitDefinition definition()
{
    KitDefinition k;
    k.id = "testkit";
    k.name = "Test kit";
    k.folder = "TestKit";
    k.credit = "made by drumstool";
    k.kitFile = "TestKit/TestKit.xml";
    k.pictureWidth = 1200;
    k.pictureHeight = 675;

    Piece snare;
    snare.id = "snare";
    snare.name = "Snare";
    snare.articulations = { { "Snare", "Snare", { 38 } } };
    snare.shape.cx = 500; snare.shape.cy = 400; snare.shape.rx = 80; snare.shape.ry = 50; snare.shape.depth = 40;

    Piece kick;
    kick.id = "kick";
    kick.name = "Kick";
    kick.articulations = { { "Kick", "KDrumR", { 36 } } };
    kick.shape.cx = 640; kick.shape.cy = 470; kick.shape.rx = 80; kick.shape.ry = 90;

    Piece hihat;
    hihat.id = "hihat";
    hihat.name = "Hi-hat";
    hihat.articulations = { { "Closed", "HihatClosed", { 42 } }, { "Open", "HihatOpen", { 46 } } };
    hihat.edge = 1;
    hihat.edgeFrom = 0.62f;
    hihat.shape.cx = 280; hihat.shape.cy = 320; hihat.shape.rx = 90; hihat.shape.ry = 34;

    k.pieces = { snare, kick, hihat };
    return k;
}

} // namespace drums::testkit
