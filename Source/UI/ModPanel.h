#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>

#include "EnvelopeComponent.h"
#include "LfoComponent.h"
#include "PluginProcessor.h"

class UIConfig;

class ModPanel final : public juce::Component
{
public:
    ModPanel(PX3SynthAudioProcessor& processorIn,
             juce::AudioParameterFloat& attack,
             juce::AudioParameterFloat& decay,
             juce::AudioParameterFloat& sustain,
             juce::AudioParameterFloat& release,
             juce::AudioParameterBool& envEnabled,
             juce::ToggleButton& envEnabledButton,
             juce::Label& envEnabledLabel,
             juce::Label& envAssignLabel,
             juce::ComboBox& envAssignBox,
             juce::ToggleButton& lfoEnabledButton,
             juce::Label& lfoEnabledLabel,
             juce::Label& lfoAssignLabel,
             juce::ComboBox& lfoAssignBox,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::LookAndFeel* sharedLfoKnobLookAndFeel,
             juce::Colour panelAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void refreshLfoFromParameters(bool enabled, float rateHz, int waveformIndex);
    void advanceAnimation(float lfoDeltaSeconds);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    int getPreferredContentWidth() const;
    int getPreferredContentHeight() const;

private:
    struct LfoBundle
    {
        juce::ToggleButton enabledButton;
        juce::Label enabledLabel;
        juce::Label assignLabel;
        juce::ComboBox assignBox;
        juce::Slider rateKnob;
        juce::Label rateLabel;
        juce::Label rateValueLabel;
        juce::ComboBox waveformBox;
        juce::Label waveformLabel;
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
        std::unique_ptr<juce::SliderParameterAttachment> rateAttachment;
        std::unique_ptr<juce::ComboBoxParameterAttachment> waveformAttachment;
        std::unique_ptr<LfoComponent> component;
    };

    struct EnvBundle
    {
        juce::ToggleButton enabledButton;
        juce::Label enabledLabel;
        juce::Label assignLabel;
        juce::ComboBox assignBox;
        std::unique_ptr<juce::ButtonParameterAttachment> enabledAttachment;
        std::unique_ptr<EnvelopeComponent> component;
    };

    void configureOwnedLfoBundle(int lfoIndex, LfoBundle& bundle);
    void configureOwnedEnvBundle(int envIndex, EnvBundle& bundle);

    PX3SynthAudioProcessor& processor;
    std::unique_ptr<EnvelopeComponent> envelopeGraph;
    std::unique_ptr<LfoComponent> lfoComponent;
    std::array<LfoBundle, 2> extraLfos;
    std::array<EnvBundle, 2> extraEnvelopes;

    juce::Colour accent;
    juce::Colour lfoHeaderAccent;
    juce::LookAndFeel* lfoKnobLookAndFeel { nullptr };
    std::shared_ptr<const UIConfig> uiConfig;
};