#include "PluginProcessor.h"
#include "PluginEditor.h"

PX3VibeAudioProcessor::PX3VibeAudioProcessor()
{
    const auto unit = juce::NormalisableRange<float>(0.0f, 1.0f);
    const px3::UniVibeSettings defaults;

    // The same ids and ranges as the Synth's fx.vibe.*. Two defaults differ on
    // purpose: an inserted effect is switched ON (in the Synth VIBE starts off,
    // because INTENSITY 0 still colours), at the pedal's own mid INTENSITY.
    addParameter(enabledParam = new juce::AudioParameterBool("fx.vibe.enabled", "Vibe Enabled", true));
    addParameter(speedParam = new juce::AudioParameterFloat("fx.vibe.speed", "Vibe Speed", unit, defaults.speed));
    addParameter(intensityParam = new juce::AudioParameterFloat("fx.vibe.intensity", "Vibe Intensity", unit, defaults.intensity));
    addParameter(modeParam = new juce::AudioParameterChoice("fx.vibe.mode", "Vibe Mode", px3::UniVibe::modeNames(), 0));
    addParameter(levelParam = new juce::AudioParameterFloat(
        "fx.vibe.level", "Vibe Level", juce::NormalisableRange<float>(-12.0f, 12.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    addParameter(stereoParam = new juce::AudioParameterChoice("fx.vibe.stereo", "Vibe Stereo", px3::UniVibe::stereoNames(), 0));
}

px3::UniVibeSettings PX3VibeAudioProcessor::settingsForBlock() const
{
    px3::UniVibeSettings settings;
    settings.enabled = enabledParam->get();
    settings.speed = speedParam->get();
    settings.intensity = intensityParam->get();
    settings.mode = modeParam->getIndex();
    settings.levelDb = levelParam->get();
    settings.stereo = stereoParam->getIndex();
    return settings;
}

void PX3VibeAudioProcessor::prepareFx(double sampleRate, int)
{
    // Settings first, so the fades start where the parameters are.
    vibe.updateForBlock(settingsForBlock());
    vibe.prepare(sampleRate);
}

void PX3VibeAudioProcessor::processFxBlock(juce::AudioBuffer<float>& buffer)
{
    vibe.updateForBlock(settingsForBlock());

    const auto numSamples = buffer.getNumSamples();
    const auto stereo = buffer.getNumChannels() > 1;
    auto* left = buffer.getWritePointer(0);
    auto* right = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto inL = left[i];
        const auto inR = stereo ? right[i] : inL;

        float outL = inL, outR = inR;
        vibe.processSampleFrame(inL, inR, outL, outR);

        if (stereo) { left[i] = outL; right[i] = outR; }
        else        { left[i] = 0.5f * (outL + outR); }
    }
}

juce::AudioProcessorEditor* PX3VibeAudioProcessor::createEditor()
{
    return new PX3VibeAudioProcessorEditor(*this);
}
