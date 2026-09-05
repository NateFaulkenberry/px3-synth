#pragma once

#include "FxPluginProcessor.h"
#include "Doom.h"
#include "DoomControlModel.h"
#include "DoomTypes.h"

// PX3 Doom. The same shared/DSP/Doom the Synth runs - the two-channel
// micro-looper and wet processor described in docs/DOOM_DSP_DESIGN.md.
class PX3DoomAudioProcessor final : public px3::fx::FxPluginProcessor
{
public:
    PX3DoomAudioProcessor();

    const juce::String getName() const override { return "PX3 Doom"; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    // A frozen wet channel repeats indefinitely; the loop keeps playing.
    double getTailLengthSeconds() const override { return 15.0; }

    juce::AudioParameterFloat& mix() { return *mixParam; }
    juce::AudioParameterFloat& clock() { return *clockParam; }
    juce::AudioParameterFloat& loopLength() { return *loopLengthParam; }
    juce::AudioParameterFloat& loopModify() { return *loopModifyParam; }
    juce::AudioParameterFloat& overdub() { return *overdubParam; }
    juce::AudioParameterFloat& fade() { return *fadeParam; }
    juce::AudioParameterFloat& wetTime() { return *wetTimeParam; }
    juce::AudioParameterFloat& wetModify() { return *wetModifyParam; }
    juce::AudioParameterFloat& cross() { return *crossParam; }
    juce::AudioParameterFloat& glue() { return *glueParam; }
    juce::AudioParameterFloat& eq() { return *eqParam; }
    juce::AudioParameterFloat& balance() { return *balanceParam; }
    juce::AudioParameterFloat& blend() { return *blendParam; }
    juce::AudioParameterFloat& spread() { return *spreadParam; }
    juce::AudioParameterBool& enabled() { return *enabledParam; }
    juce::AudioParameterBool& freeze() { return *freezeParam; }
    juce::AudioParameterBool& loopActive() { return *loopActiveParam; }
    juce::AudioParameterBool& wetActive() { return *wetActiveParam; }
    juce::AudioParameterBool& loopHalf() { return *loopHalfParam; }
    juce::AudioParameterBool& clockSmooth() { return *clockSmoothParam; }
    juce::AudioParameterChoice& routing() { return *routingParam; }
    juce::AudioParameterChoice& loopMode() { return *loopModeParam; }
    juce::AudioParameterChoice& wetMode() { return *wetModeParam; }
    juce::AudioParameterChoice& crossSource() { return *crossSourceParam; }
    px3::DoomUserParameters debugUserParameters() const { return userParametersForBlock(); }

protected:
    void prepareFx(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void processFxBlock(juce::AudioBuffer<float>& buffer) override;

private:
    px3::DoomUserParameters userParametersForBlock() const;

    px3::Doom doom;

    juce::AudioParameterFloat* mixParam { nullptr };
    juce::AudioParameterFloat* clockParam { nullptr };
    juce::AudioParameterFloat* loopLengthParam { nullptr };
    juce::AudioParameterFloat* loopModifyParam { nullptr };
    juce::AudioParameterFloat* overdubParam { nullptr };
    juce::AudioParameterFloat* fadeParam { nullptr };
    juce::AudioParameterFloat* wetTimeParam { nullptr };
    juce::AudioParameterFloat* wetModifyParam { nullptr };
    juce::AudioParameterFloat* crossParam { nullptr };
    juce::AudioParameterFloat* glueParam { nullptr };
    juce::AudioParameterFloat* eqParam { nullptr };
    juce::AudioParameterFloat* balanceParam { nullptr };
    juce::AudioParameterFloat* blendParam { nullptr };
    juce::AudioParameterFloat* spreadParam { nullptr };
    juce::AudioParameterBool* enabledParam { nullptr };
    juce::AudioParameterBool* freezeParam { nullptr };
    juce::AudioParameterBool* loopActiveParam { nullptr };
    juce::AudioParameterBool* wetActiveParam { nullptr };
    juce::AudioParameterBool* loopHalfParam { nullptr };
    juce::AudioParameterBool* clockSmoothParam { nullptr };
    juce::AudioParameterChoice* routingParam { nullptr };
    juce::AudioParameterChoice* loopModeParam { nullptr };
    juce::AudioParameterChoice* wetModeParam { nullptr };
    juce::AudioParameterChoice* crossSourceParam { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PX3DoomAudioProcessor)
};
