#pragma once

#include "FxPluginProcessor.h"
#include "Lucy.h"
#include "LucyControlModel.h"
#include "LucyTypes.h"

// PX3 Lucy. The same shared/DSP/Lucy the Synth runs - the spectral loss,
// packet corruption and freeze described in docs/LUCY_DSP_DESIGN.md.
class PX3LucyAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3LucyAudioProcessor();

    const juce::String getName() const override { return "PX3 Lucy"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // A frozen spectrum sustains, and the reverb has its own tail.
    double getTailLengthSeconds() const override { return 15.0; }

    // The six primary knobs.
    juce::AudioParameterFloat& global() { return *globalParam; }
    juce::AudioParameterFloat& loss() { return *lossParam; }
    juce::AudioParameterFloat& speed() { return *speedParam; }
    juce::AudioParameterFloat& filter() { return *filterParam; }
    juce::AudioParameterFloat& filterFreq() { return *filterFreqParam; }
    juce::AudioParameterFloat& verb() { return *verbParam; }

    // Their alternate functions, in the same order.
    juce::AudioParameterFloat& freezer() { return *freezerParam; }
    juce::AudioParameterFloat& lossGain() { return *lossGainParam; }
    juce::AudioParameterFloat& autoGain() { return *autoGainParam; }
    juce::AudioParameterFloat& gateThreshold() { return *gateThresholdParam; }
    juce::AudioParameterFloat& limiterThreshold() { return *limiterThresholdParam; }
    juce::AudioParameterFloat& verbDecay() { return *verbDecayParam; }

    juce::AudioParameterFloat& spread() { return *spreadParam; }
    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterBool& filterInvert() { return *filterInvertParam; }
    juce::AudioParameterBool& verbPost() { return *verbPostParam; }
    juce::AudioParameterBool& gate() { return *gateParam; }
    juce::AudioParameterBool& slow() { return *slowParam; }
    juce::AudioParameterChoice& mode() { return *modeParam; }
    juce::AudioParameterChoice& packets() { return *packetsParam; }
    juce::AudioParameterChoice& slope() { return *slopeParam; }
    juce::AudioParameterChoice& weighting() { return *weightingParam; }
    // OFF / SOLID / SLUSHY as one control. Two booleans could express a fourth
    // combination - slushy while not frozen - that meant nothing.
    juce::AudioParameterChoice& freeze() { return *freezeParam; }

    px3::LucyUserParameters debugUserParameters() const { return userParametersForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    px3::LucyUserParameters userParametersForBlock() const;

    px3::Lucy lucy;

    juce::AudioParameterFloat* globalParam { nullptr };
    juce::AudioParameterFloat* lossParam { nullptr };
    juce::AudioParameterFloat* speedParam { nullptr };
    juce::AudioParameterFloat* filterParam { nullptr };
    juce::AudioParameterFloat* filterFreqParam { nullptr };
    juce::AudioParameterFloat* verbParam { nullptr };
    juce::AudioParameterFloat* verbDecayParam { nullptr };
    juce::AudioParameterFloat* freezerParam { nullptr };
    juce::AudioParameterFloat* gateThresholdParam { nullptr };
    juce::AudioParameterFloat* limiterThresholdParam { nullptr };
    juce::AudioParameterFloat* autoGainParam { nullptr };
    juce::AudioParameterFloat* lossGainParam { nullptr };
    juce::AudioParameterFloat* spreadParam { nullptr };
    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterBool* filterInvertParam { nullptr };
    juce::AudioParameterBool* verbPostParam { nullptr };
    juce::AudioParameterBool* gateParam { nullptr };
    juce::AudioParameterBool* slowParam { nullptr };
    juce::AudioParameterChoice* modeParam { nullptr };
    juce::AudioParameterChoice* packetsParam { nullptr };
    juce::AudioParameterChoice* slopeParam { nullptr };
    juce::AudioParameterChoice* weightingParam { nullptr };
    juce::AudioParameterChoice* freezeParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3LucyAudioProcessor)
};
