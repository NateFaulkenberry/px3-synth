#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3ReverbAudioProcessor::PX3ReverbAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("reverbEnabled", "Reverb Enabled", true));
    // Full rather than the Synth's zero: a standalone reverb that does nothing
    // when inserted reads as broken. Every other default is the Synth's.
    addParameter(amountParam = new juce::AudioParameterFloat("reverbAmount", "Reverb", unit, 0.35f));
    addParameter(sizeParam = new juce::AudioParameterFloat("reverbSize", "Reverb Size", unit, 0.52f));
    addParameter(decayParam = new juce::AudioParameterFloat("reverbDecay", "Reverb Decay", unit, 0.48f));
    addParameter(dampingParam = new juce::AudioParameterFloat("reverbDamping", "Reverb Damping", unit, 0.46f));
    addParameter(preDelayParam = new juce::AudioParameterFloat("reverbPreDelay", "Reverb PreDelay", unit, 0.08f));
    addParameter(modDepthParam = new juce::AudioParameterFloat("reverbModDepth", "Reverb Mod Depth", unit, 0.24f));
    addParameter(modRateParam = new juce::AudioParameterFloat("reverbModRate", "Reverb Mod Rate", unit, 0.18f));
    addParameter(widthParam = new juce::AudioParameterFloat("reverbWidth", "Reverb Width", unit, 0.86f));
    addParameter(cloudFeedbackParam = new juce::AudioParameterFloat("reverbCloudFeedback", "Reverb Cloud Feedback", unit, 0.62f));
    addParameter(cloudDiffusionParam = new juce::AudioParameterFloat("reverbCloudDiffusion", "Reverb Cloud Diffusion", unit, 0.54f));
    addParameter(algorithmParam = new juce::AudioParameterChoice(
        "reverbAlgorithm", "Reverb Mode",
        juce::StringArray { "ROOM", "PLATE", "HALL", "CLOUD" }, 0));
}

void PX3ReverbAudioProcessor::prepareFx(double sampleRate, int)
{
    reverb.prepare(sampleRate);
}

ReverbSettings PX3ReverbAudioProcessor::settingsForBlock() const
{
    ReverbSettings settings;
    settings.enabled = enabledParam->get();
    settings.algorithmIndex = algorithmParam->getIndex();
    settings.amount = amountParam->get();
    settings.size = sizeParam->get();
    settings.decay = decayParam->get();
    settings.damping = dampingParam->get();
    settings.preDelay = preDelayParam->get();
    settings.modDepth = modDepthParam->get();
    settings.modRate = modRateParam->get();
    settings.width = widthParam->get();
    settings.cloudFeedback = cloudFeedbackParam->get();
    settings.cloudDiffusion = cloudDiffusionParam->get();
    return settings;
}

void PX3ReverbAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    reverb.updateForBlock(settingsForBlock(), numSamples);

    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        reverb.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3ReverbAudioProcessor::createEditor()
{
    return new PX3ReverbAudioProcessorEditor(*this);
}
