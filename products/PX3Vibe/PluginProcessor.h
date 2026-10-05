#pragma once

#include "FxPluginProcessor.h"
#include "UniVibe.h"

// PX3 Vibe. The same shared/DSP/Vibe Uni-Vibe the Synth runs in its FX chain.
class PX3VibeAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3VibeAudioProcessor();

    const juce::String getName() const override { return "PX3 Vibe"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // The phase network's lowest stage settles in well under this.
    double getTailLengthSeconds() const override { return 0.2; }

    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterFloat& speed() { return *speedParam; }
    juce::AudioParameterFloat& intensity() { return *intensityParam; }
    juce::AudioParameterChoice& mode() { return *modeParam; }
    juce::AudioParameterFloat& level() { return *levelParam; }
    juce::AudioParameterChoice& stereo() { return *stereoParam; }
    px3::UniVibeSettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    px3::UniVibeSettings settingsForBlock() const;

    px3::UniVibe vibe;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterFloat* speedParam { nullptr };
    juce::AudioParameterFloat* intensityParam { nullptr };
    juce::AudioParameterChoice* modeParam { nullptr };
    juce::AudioParameterFloat* levelParam { nullptr };
    juce::AudioParameterChoice* stereoParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3VibeAudioProcessor)
};
