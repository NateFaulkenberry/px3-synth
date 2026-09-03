#pragma once

#include "FxPluginProcessor.h"
#include "Reverb.h"
#include "ReverbTypes.h"

// PX3 Reverb. The same shared/DSP/Reverb the Synth runs - the project's own
// Dattorro/zita-derived algorithms, not juce::dsp::Reverb.
//
// Worth recording: nine of these parameters - size, decay, damping, pre-delay,
// the two modulation controls, width and the two cloud controls - exist in the
// Synth and feed its DSP, but have NO user interface there. They are
// host-automatable only. The standalone gives them controls, which is not a
// change to the Synth so much as the first time these have been reachable by
// hand.
class PX3ReverbAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3ReverbAudioProcessor();

    const juce::String getName() const override { return "PX3 Reverb"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // A hall at a long decay rings for a while, and a host that trims the tail
    // early cuts it off.
    double getTailLengthSeconds() const override { return 20.0; }

    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterChoice& algorithm() { return *algorithmParam; }
    juce::AudioParameterFloat& amount() { return *amountParam; }
    juce::AudioParameterFloat& size() { return *sizeParam; }
    juce::AudioParameterFloat& decay() { return *decayParam; }
    juce::AudioParameterFloat& damping() { return *dampingParam; }
    juce::AudioParameterFloat& preDelay() { return *preDelayParam; }
    juce::AudioParameterFloat& modDepth() { return *modDepthParam; }
    juce::AudioParameterFloat& modRate() { return *modRateParam; }
    juce::AudioParameterFloat& width() { return *widthParam; }
    juce::AudioParameterFloat& cloudFeedback() { return *cloudFeedbackParam; }
    juce::AudioParameterFloat& cloudDiffusion() { return *cloudDiffusionParam; }
    ReverbSettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    ReverbSettings settingsForBlock() const;

    ::Reverb reverb;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* algorithmParam { nullptr };
    juce::AudioParameterFloat* amountParam { nullptr };
    juce::AudioParameterFloat* sizeParam { nullptr };
    juce::AudioParameterFloat* decayParam { nullptr };
    juce::AudioParameterFloat* dampingParam { nullptr };
    juce::AudioParameterFloat* preDelayParam { nullptr };
    juce::AudioParameterFloat* modDepthParam { nullptr };
    juce::AudioParameterFloat* modRateParam { nullptr };
    juce::AudioParameterFloat* widthParam { nullptr };
    juce::AudioParameterFloat* cloudFeedbackParam { nullptr };
    juce::AudioParameterFloat* cloudDiffusionParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3ReverbAudioProcessor)
};
