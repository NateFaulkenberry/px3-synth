#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3LucyAudioProcessor::PX3LucyAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);
    // Two that are NOT unit ranges, and copying them as one would have moved
    // what every stored value means: weighting is a bipolar tilt, and gain is
    // in DECIBELS, plus or minus 36.
    const auto bipolar = juce::NormalisableRange<float>(-1.0f, 1.0f);
    const auto decibels = juce::NormalisableRange<float>(-36.0f, 36.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("lucyEnabled", "Lucy Enabled", true));
    addParameter(filterInvertParam = new juce::AudioParameterBool("lucyFilterInvert", "Lucy Filter Invert", false));
    addParameter(verbPostParam = new juce::AudioParameterBool("lucyVerbPost", "Lucy Verb Post", false));
    addParameter(freezeParam = new juce::AudioParameterBool("lucyFreeze", "Lucy Freeze", false));
    addParameter(freezeSlushyParam = new juce::AudioParameterBool("lucyFreezeSlushy", "Lucy Freeze Slushy", false));
    addParameter(gateParam = new juce::AudioParameterBool("lucyGate", "Lucy Gate", false));
    addParameter(slowParam = new juce::AudioParameterBool("lucySlow", "Lucy Slow", false));
    addParameter(globalParam = new juce::AudioParameterFloat("lucyGlobal", "Lucy Global", unit, 0.0f));
    addParameter(lossParam = new juce::AudioParameterFloat("lucyLoss", "Lucy Loss", unit, 0.55f));
    addParameter(speedParam = new juce::AudioParameterFloat("lucySpeed", "Lucy Speed", unit, 0.5f));
    addParameter(filterParam = new juce::AudioParameterFloat("lucyFilter", "Lucy Filter", unit, 0.0f));
    addParameter(filterFreqParam = new juce::AudioParameterFloat("lucyFilterFreq", "Lucy Filter Freq", unit, 0.5f));
    addParameter(verbParam = new juce::AudioParameterFloat("lucyVerb", "Lucy Verb", unit, 0.0f));
    addParameter(verbDecayParam = new juce::AudioParameterFloat("lucyVerbDecay", "Lucy Verb Decay", unit, 0.45f));
    addParameter(freezerParam = new juce::AudioParameterFloat("lucyFreezer", "Lucy Freezer", unit, 1.0f));
    addParameter(gateCutoffParam = new juce::AudioParameterFloat("lucyGateCutoff", "Lucy Gate Cutoff", unit, 0.25f));
    addParameter(thresholdParam = new juce::AudioParameterFloat("lucyThreshold", "Lucy Threshold", unit, 0.8f));
    addParameter(autoGainParam = new juce::AudioParameterFloat("lucyAutoGain", "Lucy Auto Gain", unit, 0.75f));
    addParameter(weightingParam = new juce::AudioParameterFloat("lucyWeighting", "Lucy Weighting", bipolar, 0.0f));
    addParameter(gainParam = new juce::AudioParameterFloat("lucyGain", "Lucy Gain", decibels, 0.0f));
    addParameter(spreadParam = new juce::AudioParameterFloat("lucySpread", "Lucy Spread", unit, 0.5f));
    addParameter(modeParam = new juce::AudioParameterChoice(
        "lucyMode", "Lucy Mode", juce::StringArray { "STANDARD", "INVERSE", "JITTER" }, 0));
    addParameter(packetsParam = new juce::AudioParameterChoice(
        "lucyPackets", "Lucy Packets", juce::StringArray { "CLEAN", "LOSS", "REPEAT" }, 0));
    addParameter(slopeParam = new juce::AudioParameterChoice(
        "lucySlope", "Lucy Slope", juce::StringArray { "6 dB", "24 dB", "96 dB" }, 1));
}

void PX3LucyAudioProcessor::prepareFx(double sampleRate, int)
{
    lucy.prepare(sampleRate);
}

LucySettings PX3LucyAudioProcessor::settingsForBlock() const
{
    LucySettings settings;
    settings.enabled = enabledParam->get();
    settings.filterInvert = filterInvertParam->get();
    settings.verbPost = verbPostParam->get();
    settings.freeze = freezeParam->get();
    settings.freezeSlushy = freezeSlushyParam->get();
    settings.gate = gateParam->get();
    settings.slow = slowParam->get();
    settings.global = globalParam->get();
    settings.loss = lossParam->get();
    settings.speed = speedParam->get();
    settings.filterWidth = filterParam->get();
    settings.filterFreq = filterFreqParam->get();
    settings.verb = verbParam->get();
    settings.verbDecay = verbDecayParam->get();
    settings.freezer = freezerParam->get();
    settings.gateCutoff = gateCutoffParam->get();
    settings.threshold = thresholdParam->get();
    settings.autoGain = autoGainParam->get();
    settings.weighting = weightingParam->get();
    settings.gainDb = gainParam->get();
    settings.spread = spreadParam->get();
    settings.modeIndex = modeParam->getIndex();
    settings.packetIndex = packetsParam->getIndex();
    settings.slopeIndex = slopeParam->getIndex();
    return settings;
}

void PX3LucyAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    lucy.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        lucy.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3LucyAudioProcessor::createEditor()
{
    return new PX3LucyAudioProcessorEditor(*this);
}
