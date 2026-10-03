#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3SpreadAudioProcessor::PX3SpreadAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("fx.spread.enabled", "Spread Enabled", true));
    // Full rather than the Synth's zero: one stage of a chain starts silent, a
    // standalone widener that does nothing when inserted reads as broken.
    addParameter(amountParam = new juce::AudioParameterFloat("fx.spread.amount", "Spread Amount", unit, 1.0f));
    addParameter(widthParam = new juce::AudioParameterFloat("fx.spread.width", "Spread Width", unit, 0.6f));
    addParameter(depthParam = new juce::AudioParameterFloat("fx.spread.depth", "Spread Depth", unit, 0.4f));
    addParameter(centerParam = new juce::AudioParameterFloat("fx.spread.center", "Spread Center", unit, 0.7f));
    addParameter(lowWidthParam = new juce::AudioParameterFloat("fx.spread.low.width", "Spread Low Width", unit, 0.0f));
    addParameter(highWidthParam = new juce::AudioParameterFloat("fx.spread.high.width", "Spread High Width", unit, 0.8f));
    addParameter(lowFreqParam = new juce::AudioParameterFloat("fx.spread.low.freq", "Spread Low Freq", unit, 0.55f));
    addParameter(highFreqParam = new juce::AudioParameterFloat("fx.spread.high.freq", "Spread High Freq", unit, 0.5f));
    addParameter(mixParam = new juce::AudioParameterFloat("fx.spread.mix", "Spread Mix", unit, 1.0f));
    // Bipolar, like the Synth's: a tilt on the SIDE signal only.
    addParameter(toneParam = new juce::AudioParameterFloat(
        "fx.spread.tone", "Spread Tone", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    addParameter(modeParam = new juce::AudioParameterChoice(
        "fx.spread.mode", "Spread Mode",
        juce::StringArray { "CLASSIC", "WIDE", "DEEP", "MONO SAFE" }, 0));
}

void PX3SpreadAudioProcessor::prepareFx(double sampleRate, int)
{
    spread.prepare(sampleRate);
}

StereoSpreadSettings PX3SpreadAudioProcessor::settingsForBlock() const
{
    StereoSpreadSettings settings;
    settings.enabled = enabledParam->get();
    settings.modeIndex = modeParam->getIndex();
    settings.amount = amountParam->get();
    settings.tone = toneParam->get();
    settings.width = widthParam->get();
    settings.depth = depthParam->get();
    settings.center = centerParam->get();
    settings.lowWidth = lowWidthParam->get();
    settings.highWidth = highWidthParam->get();
    settings.lowFreq = lowFreqParam->get();
    settings.highFreq = highFreqParam->get();
    settings.mix = mixParam->get();
    return settings;
}

void PX3SpreadAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    spread.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        spread.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3SpreadAudioProcessor::createEditor()
{
    return new PX3SpreadAudioProcessorEditor(*this);
}
