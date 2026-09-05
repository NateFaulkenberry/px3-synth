#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3LucyAudioProcessor::PX3LucyAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);
    // The one range that is NOT unit: LOSS GAIN is in DECIBELS, plus or minus
    // 36, and copying it as a unit range would move what every stored value
    // means.
    const auto decibels = juce::NormalisableRange<float>(-36.0f, 36.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("lucyEnabled", "Lucy Enabled", true));

    // ---- the six primary knobs -------------------------------------------
    //
    // GLOBAL starts at zero: adding an effect must not change what an existing
    // patch sounds like. Every other default is the setting that makes the
    // control useful the moment GLOBAL is raised.
    addParameter(globalParam = new juce::AudioParameterFloat("lucyGlobal", "Lucy Global", unit, 0.0f));
    addParameter(lossParam = new juce::AudioParameterFloat("lucyLoss", "Lucy Loss", unit, 0.55f));
    addParameter(speedParam = new juce::AudioParameterFloat("lucySpeed", "Lucy Speed", unit, 0.5f));
    // Zero is NO filtering at all, which is what makes this a width control.
    addParameter(filterParam = new juce::AudioParameterFloat("lucyFilter", "Lucy Filter", unit, 0.0f));
    addParameter(filterFreqParam = new juce::AudioParameterFloat("lucyFreq", "Lucy Freq", unit, 0.5f));
    addParameter(verbParam = new juce::AudioParameterFloat("lucyVerb", "Lucy Verb", unit, 0.0f));

    // ---- their alternate functions ----------------------------------------
    addParameter(gateThresholdParam = new juce::AudioParameterFloat(
        "lucyGateThreshold", "Lucy Gate Threshold", unit, 0.25f));
    addParameter(freezerParam = new juce::AudioParameterFloat("lucyFreezer", "Lucy Freezer", unit, 1.0f));
    addParameter(verbDecayParam = new juce::AudioParameterFloat("lucyDecay", "Lucy Decay", unit, 0.45f));
    // The LIMITER's threshold. Named in full because the loss coder has a
    // masking threshold of its own; that one is derived and never a parameter.
    addParameter(limiterThresholdParam = new juce::AudioParameterFloat(
        "lucyLimiterThreshold", "Lucy Limiter Threshold", unit, 0.8f));
    addParameter(autoGainParam = new juce::AudioParameterFloat("lucyAutoGain", "Lucy Auto Gain", unit, 0.75f));
    addParameter(lossGainParam = new juce::AudioParameterFloat("lucyLossGain", "Lucy Loss Gain", decibels, 0.0f));

    // ---- toggles and categories -------------------------------------------
    addParameter(filterInvertParam = new juce::AudioParameterBool("lucyFilterInvert", "Lucy Filter Invert", false));
    addParameter(verbPostParam = new juce::AudioParameterBool("lucyVerbPost", "Lucy Verb Post", false));
    addParameter(gateParam = new juce::AudioParameterBool("lucyGate", "Lucy Gate", false));
    addParameter(slowParam = new juce::AudioParameterBool("lucySlow", "Lucy Slow", false));

    addParameter(modeParam = new juce::AudioParameterChoice(
        "lucyMode", "Lucy Mode", juce::StringArray { "STANDARD", "INVERSE", "JITTER" }, 0));
    addParameter(packetsParam = new juce::AudioParameterChoice(
        "lucyPackets", "Lucy Packets", juce::StringArray { "CLEAN", "LOSS", "REPEAT" }, 0));
    addParameter(slopeParam = new juce::AudioParameterChoice(
        "lucySlope", "Lucy Slope", juce::StringArray { "6 dB", "24 dB", "96 dB" }, 1));
    addParameter(weightingParam = new juce::AudioParameterChoice(
        "lucyWeighting", "Lucy Weighting", juce::StringArray { "DARK", "NEUTRAL", "BRIGHT" }, 1));
    // One control with three states rather than two booleans, which could
    // express "slushy while not frozen" - a combination that meant nothing.
    addParameter(freezeParam = new juce::AudioParameterChoice(
        "lucyFreeze", "Lucy Freeze", juce::StringArray { "OFF", "SOLID", "SLUSHY" }, 0));

    addParameter(spreadParam = new juce::AudioParameterFloat("lucySpread", "Lucy Spread", unit, 0.5f));
}

void PX3LucyAudioProcessor::prepareFx(double sampleRate, int)
{
    lucy.prepare(sampleRate);
}

px3::LucyUserParameters PX3LucyAudioProcessor::userParametersForBlock() const
{
    px3::LucyUserParameters user;
    user.enabled = enabledParam->get();

    user.global = globalParam->get();
    user.loss = lossParam->get();
    user.speed = speedParam->get();

    user.filter = filterParam->get();
    user.filterFreq = filterFreqParam->get();
    user.filterInvert = filterInvertParam->get();
    user.slope = static_cast<px3::LucyFilterSlope>(slopeParam->getIndex());

    user.verb = verbParam->get();
    user.verbPost = verbPostParam->get();
    user.verbDecay = verbDecayParam->get();

    user.packets = static_cast<px3::LucyPacketMode>(packetsParam->getIndex());
    user.mode = static_cast<px3::LucyLossMode>(modeParam->getIndex());
    user.weighting = static_cast<px3::LucyWeighting>(weightingParam->getIndex());
    user.freeze = static_cast<px3::LucyFreezeMode>(freezeParam->getIndex());

    user.gate = gateParam->get();
    user.slow = slowParam->get();

    user.gateThreshold = gateThresholdParam->get();
    user.freezer = freezerParam->get();
    user.limiterThreshold = limiterThresholdParam->get();
    user.autoGain = autoGainParam->get();
    user.lossGainDb = lossGainParam->get();
    user.spread = spreadParam->get();
    return user;
}

void PX3LucyAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    lucy.updateForBlock(userParametersForBlock());

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
