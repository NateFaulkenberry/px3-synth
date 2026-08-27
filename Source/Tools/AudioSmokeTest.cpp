// Factory-default audio smoke test.
//
// Deliberately built with PX3_DIAGNOSTICS=0 - the shipping configuration - and
// deliberately sets NO parameters. It answers the one question every other test
// assumes: does a freshly loaded plugin make sound when you press a key?

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <cstdio>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 512;

struct SmokeResult
{
    double peak { 0.0 };
    double rms { 0.0 };
    bool sawNonFinite { false };
};

SmokeResult renderDefaultNote(double sampleRate, int blockSize)
{
    PX3SynthAudioProcessor processor;   // factory defaults only, nothing set
    processor.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    const auto totalSamples = static_cast<int>(1.5 * sampleRate);
    const auto noteOn = static_cast<int>(0.05 * sampleRate);

    SmokeResult result;
    double energy = 0.0;
    long long count = 0;
    auto delivered = false;

    for (int position = 0; position < totalSamples; position += blockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (!delivered && position + blockSize > noteOn)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), juce::jmax(0, noteOn - position));
            delivered = true;
        }
        processor.processBlock(buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
            {
                const auto v = static_cast<double>(data[i]);
                if (!std::isfinite(v)) result.sawNonFinite = true;
                result.peak = std::max(result.peak, std::abs(v));
                energy += v * v;
                ++count;
            }
        }
    }

    result.rms = count > 0 ? std::sqrt(energy / static_cast<double>(count)) : 0.0;
    return result;
}

// Loads the state the standalone actually restores on launch and renders it.
// The standalone persists its session to ~/Library/Application Support, so a
// factory-default test does not represent what the user sees on startup.
SmokeResult renderRestoredSessionNote(const juce::File& settingsFile, bool& loaded, juce::String& report, int midiNote = 60)
{
    loaded = false;
    SmokeResult result;

    if (!settingsFile.existsAsFile())
    {
        report = "settings file not found";
        return result;
    }

    auto xml = juce::parseXML(settingsFile);
    if (xml == nullptr)
    {
        report = "settings file is not valid XML";
        return result;
    }

    juce::String encoded;
    for (auto* child : xml->getChildWithTagNameIterator("VALUE"))
    {
        if (child->getStringAttribute("name") == "filterState")
        {
            encoded = child->getStringAttribute("val");
            break;
        }
    }
    if (encoded.isEmpty())
    {
        report = "no filterState entry in settings";
        return result;
    }

    juce::MemoryBlock block;
    if (!block.fromBase64Encoding(encoded))
    {
        report = "filterState is not decodable base64";
        return result;
    }

    PX3SynthAudioProcessor processor;
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);

    juce::AudioBuffer<float> buffer(2, kBlockSize);
    const auto totalSamples = static_cast<int>(1.5 * kSampleRate);
    const auto noteOn = static_cast<int>(0.05 * kSampleRate);
    double energy = 0.0;
    long long count = 0;
    auto delivered = false;

    for (int position = 0; position < totalSamples; position += kBlockSize)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        if (!delivered && position + kBlockSize > noteOn)
        {
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.9f), juce::jmax(0, noteOn - position));
            delivered = true;
        }
        processor.processBlock(buffer, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer(ch);
            for (int i = 0; i < kBlockSize; ++i)
            {
                const auto v = static_cast<double>(data[i]);
                if (!std::isfinite(v)) result.sawNonFinite = true;
                result.peak = std::max(result.peak, std::abs(v));
                energy += v * v;
                ++count;
            }
        }
    }
    result.rms = count > 0 ? std::sqrt(energy / static_cast<double>(count)) : 0.0;

    // Report the mixer-relevant values that were restored.
    juce::StringArray interesting {
        "mix.osc1.level", "fxReturnGain", "masterGain",
        "osc1Enabled", "osc2Enabled", "osc3Enabled", "subOscEnabled",
        "mix.sub.solo", "mix.osc1.solo", "mix.osc2.solo", "mix.osc3.solo",
        "mix.fx.mute", "mix.fx.solo",
        "delayEnabled", "delayAmount", "reverbEnabled", "reverbAmount",
        "moodEnabled", "moodMix", "fxSendGain",
        "mix.sub.fxSend", "mix.osc1.fxSend", "mix.osc2.fxSend", "mix.osc3.fxSend"
    };
    juce::String lines;
    for (auto* parameter : processor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            const auto id = ranged->getParameterID();
            if (interesting.contains(id))
            {
                lines << "      " << id.paddedRight(' ', 18) << " = "
                      << juce::String(ranged->convertFrom0to1(ranged->getValue()), 4) << "\n";
            }
        }
    }
    report = lines;
    loaded = true;
    return result;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf("FACTORY-DEFAULT AUDIO SMOKE TEST (shipping build, PX3_DIAGNOSTICS=%d)\n",
                (int) PX3_DIAGNOSTICS);
    std::printf("  no parameters are set: this is exactly what a freshly loaded plugin does\n\n");

    auto failures = 0;
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    const int blocks[] = { 64, 512 };

    for (const auto rate : rates)
    {
        for (const auto block : blocks)
        {
            const auto r = renderDefaultNote(rate, block);
            const auto ok = r.peak > 1.0e-4 && !r.sawNonFinite;
            if (!ok) ++failures;
            std::printf("  %6.0f Hz / %4d samples : peak %.6f  rms %.6f  %s%s\n",
                        rate, block, r.peak, r.rms,
                        ok ? "AUDIBLE" : "*** SILENT ***",
                        r.sawNonFinite ? "  NON-FINITE!" : "");
            std::fflush(stdout);
        }
    }

    // The standalone restores its last session, so also render exactly that.
    const auto settings = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Application Support")
                              .getChildFile("PX3 Synth.settings");
    bool loaded = false;
    juce::String report;
    const auto restored = renderRestoredSessionNote(settings, loaded, report);

    std::printf("\n  RESTORED STANDALONE SESSION (%s)\n", settings.getFullPathName().toRawUTF8());
    if (!loaded)
    {
        std::printf("    could not load: %s\n", report.toRawUTF8());
    }
    else
    {
        std::printf("    peak %.6f  rms %.6f  %s\n", restored.peak, restored.rms,
                    restored.peak > 1.0e-4 ? "AUDIBLE" : "*** SILENT ***");
        std::printf("    restored values:\n%s", report.toRawUTF8());
        if (restored.peak <= 1.0e-4) ++failures;
    }

    if (loaded)
    {
        std::printf("\n    level across the keyboard in the restored session:\n");
        for (const auto note : { 36, 48, 60, 72, 84 })
        {
            bool ok = false;
            juce::String ignored;
            const auto r = renderRestoredSessionNote(settings, ok, ignored, note);
            const auto db = r.peak > 1.0e-9 ? 20.0 * std::log10(r.peak) : -144.0;
            std::printf("      note %3d (%7.1f Hz) : peak %.6f  (%7.1f dBFS)  %s\n",
                        note, juce::MidiMessage::getMidiNoteInHertz(note), r.peak, db,
                        r.peak > 1.0e-3 ? "" : "<- effectively inaudible");
        }
    }

    if (loaded)
    {
        std::printf("\n    what-if: clear OSC1 solo, leaving FX solo latched on\n");
        auto xml = juce::parseXML(settings);
        juce::String encoded;
        if (xml != nullptr)
            for (auto* child : xml->getChildWithTagNameIterator("VALUE"))
                if (child->getStringAttribute("name") == "filterState")
                    encoded = child->getStringAttribute("val");

        juce::MemoryBlock block;
        if (block.fromBase64Encoding(encoded))
        {
            PX3SynthAudioProcessor processor;
            processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
            for (auto* parameter : processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                    if (ranged->getParameterID() == "mix.osc1.solo")
                        ranged->setValueNotifyingHost(0.0f);

            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
            processor.prepareToPlay(kSampleRate, kBlockSize);
            juce::AudioBuffer<float> buffer(2, kBlockSize);
            double peak = 0.0;
            auto delivered = false;
            const auto noteOn = static_cast<int>(0.05 * kSampleRate);
            for (int position = 0; position < static_cast<int>(1.5 * kSampleRate); position += kBlockSize)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (!delivered && position + kBlockSize > noteOn)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), juce::jmax(0, noteOn - position));
                    delivered = true;
                }
                processor.processBlock(buffer, midi);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                {
                    const auto* d = buffer.getReadPointer(ch);
                    for (int i = 0; i < kBlockSize; ++i) peak = std::max(peak, std::abs((double) d[i]));
                }
            }
            std::printf("      peak %.8f  %s\n", peak,
                        peak > 1.0e-4 ? "AUDIBLE" : "*** SILENT - all dry muted, FX return is all that is left ***");
        }
    }

    std::printf("\n  %d failure(s)\n", failures);
    return failures;
}
