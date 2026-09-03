#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3DelayAudioProcessor::PX3DelayAudioProcessor()
{
    // Ranges, defaults, names and IDs copied from the Synth's declarations
    // verbatim, so the same control means the same thing in both products.
    // The one intentional difference is the default for Amount: inside the
    // Synth the delay is one stage of a chain and starts at zero, but a
    // standalone delay that does nothing when you insert it reads as broken.
    addParameter(amountParam = new juce::AudioParameterFloat(
        "delayAmount", "Delay Amount", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    addParameter(timeParam = new juce::AudioParameterFloat(
        "delayTime", "Delay Time", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));
    addParameter(feedbackParam = new juce::AudioParameterFloat(
        "delayFeedback", "Delay Feedback", juce::NormalisableRange<float>(0.0f, 1.0f), 0.38f));
    addParameter(enabledParam = new juce::AudioParameterBool(
        "delayEnabled", "Delay Enabled", true));
    addParameter(algorithmParam = new juce::AudioParameterChoice(
        "delayAlgorithm", "Delay Algorithm",
        juce::StringArray { "Granular", "Tape", "Analog/BBD", "Ping-Pong",
                            "Stereo", "Modulated", "Diffusion" }, 0));
    addParameter(granularModeParam = new juce::AudioParameterChoice(
        "granularMode", "Granular Mode",
        juce::StringArray { "CLASSIC", "CLOUD", "SHIMMER", "RHYTHMIC" }, 0));
    addParameter(syncDivisionParam = new juce::AudioParameterChoice(
        "granularSyncDivision", "Granular Sync",
        juce::StringArray { "Free", "1 Bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T" }, 0));
}

void PX3DelayAudioProcessor::prepareFx(double sampleRate, int)
{
    delay.prepare(sampleRate);
    delay.reset();
}

DelaySettings PX3DelayAudioProcessor::settingsForBlock() const
{
    DelaySettings settings;
    settings.enabled = enabledParam->get();
    settings.algorithmIndex = algorithmParam->getIndex();
    settings.granularModeIndex = granularModeParam->getIndex();
    settings.syncDivisionIndex = syncDivisionParam->getIndex();
    // Straight from the parameter. The Synth reads the same values through its
    // modulation accumulator, which is what lets an LFO move the delay time
    // there; there is nothing to modulate them with here.
    settings.amount = amountParam->get();
    settings.timeControl = timeParam->get();
    settings.feedbackControl = feedbackParam->get();
    settings.bpm = hostBpm();
    return settings;
}

void PX3DelayAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    delay.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;

    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    // The per-sample loop lives here, where Delay's type is known and
    // processSampleFrame inlines. The one virtual the audio thread crossed to
    // get here was called once, for the whole block.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        delay.processSampleFrame(inL, inR, outL, outR);

        left[i] = outL;
        // Mono: the two sides are folded back down, so a mono track hears the
        // whole effect rather than only its left half.
        if (stereo) { right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3DelayAudioProcessor::createEditor()
{
    return new PX3DelayAudioProcessorEditor(*this);
}
