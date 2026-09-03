#pragma once

#include "FxPluginProcessor.h"
#include "StereoSpread.h"
#include "StereoSpreadTypes.h"

// PX3 Spread. The same shared/DSP/StereoSpread the Synth runs.
//
// A widener is the effect where a wrapper mistake is most audible and least
// visible: swap the channels and it still "works", collapse to mono and the
// side signal vanishes. The tests check phase and channel routing directly
// rather than only that audio came out.
class PX3SpreadAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3SpreadAudioProcessor();

    const juce::String getName() const override { return "PX3 Spread"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterChoice& mode() { return *modeParam; }
    juce::AudioParameterFloat& amount() { return *amountParam; }
    juce::AudioParameterFloat& tone() { return *toneParam; }
    juce::AudioParameterFloat& width() { return *widthParam; }
    juce::AudioParameterFloat& depth() { return *depthParam; }
    juce::AudioParameterFloat& center() { return *centerParam; }
    juce::AudioParameterFloat& lowWidth() { return *lowWidthParam; }
    juce::AudioParameterFloat& highWidth() { return *highWidthParam; }
    juce::AudioParameterFloat& lowFreq() { return *lowFreqParam; }
    juce::AudioParameterFloat& highFreq() { return *highFreqParam; }
    juce::AudioParameterFloat& mix() { return *mixParam; }
    StereoSpreadSettings debugSettingsForBlock() const { return settingsForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    StereoSpreadSettings settingsForBlock() const;

    px3::StereoSpread spread;

    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterChoice* modeParam { nullptr };
    juce::AudioParameterFloat* amountParam { nullptr };
    juce::AudioParameterFloat* toneParam { nullptr };
    juce::AudioParameterFloat* widthParam { nullptr };
    juce::AudioParameterFloat* depthParam { nullptr };
    juce::AudioParameterFloat* centerParam { nullptr };
    juce::AudioParameterFloat* lowWidthParam { nullptr };
    juce::AudioParameterFloat* highWidthParam { nullptr };
    juce::AudioParameterFloat* lowFreqParam { nullptr };
    juce::AudioParameterFloat* highFreqParam { nullptr };
    juce::AudioParameterFloat* mixParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3SpreadAudioProcessor)
};
