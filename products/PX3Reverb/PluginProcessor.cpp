#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ReverbPresets.h"

PX3ReverbAudioProcessor::PX3ReverbAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);

    addParameter(enabledParam = new juce::AudioParameterBool("fx.reverb.enabled", "Reverb Enabled", true));
    // Part-wet rather than the Synth's zero: a standalone reverb that does
    // nothing when inserted reads as broken. Every other default is the Synth's.
    addParameter(amountParam = new juce::AudioParameterFloat("fx.reverb.amount", "Reverb", unit, 0.35f));
    addParameter(algorithmParam = new juce::AudioParameterChoice("fx.reverb.algorithm", "Reverb Mode",
                                                                 px3::reverb::typeChoices(), 0));
    for (const auto& spec : px3::reverb::kParameterSpecs)
    {
        auto* algorithmChoice = algorithmParam;
        auto* parameter = new juce::AudioParameterFloat(
            spec.id, spec.name, unit, spec.defaultValue,
            px3::reverb::attributesFor(spec.control, [algorithmChoice] { return algorithmChoice->getIndex(); }));
        controlParams[static_cast<std::size_t>(spec.control)] = parameter;
        addParameter(parameter);
    }
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
    for (const auto& spec : px3::reverb::kParameterSpecs)
        px3::reverb::settingsField(settings, spec.control) = controlParams[static_cast<std::size_t>(spec.control)]->get();
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

void PX3ReverbAudioProcessor::applyPreset(int type, const juce::String& name)
{
    const auto* preset = px3::reverb::findPreset(type, name);
    if (preset == nullptr) { return; }
    const auto set = [](juce::RangedAudioParameter& parameter, float normalised)
    {
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost(normalised);
        parameter.endChangeGesture();
    };
    set(*algorithmParam, algorithmParam->convertTo0to1(static_cast<float>(preset->type)));
    for (const auto& spec : px3::reverb::kParameterSpecs)
        set(*controlParams[static_cast<std::size_t>(spec.control)], px3::reverb::presetValue(*preset, spec.control));
    presetSelection = juce::String(px3::reverb::kTypeNames[preset->type]) + "/" + preset->name;
}

void PX3ReverbAudioProcessor::writeExtraState(juce::ValueTree& state) const
{
    if (presetSelection.isNotEmpty()) { state.setProperty("reverbPreset", presetSelection, nullptr); }
}

void PX3ReverbAudioProcessor::readExtraState(const juce::ValueTree& state)
{
    presetSelection = state.getProperty("reverbPreset").toString();
}

juce::AudioProcessorEditor* PX3ReverbAudioProcessor::createEditor()
{
    return new PX3ReverbAudioProcessorEditor(*this);
}
