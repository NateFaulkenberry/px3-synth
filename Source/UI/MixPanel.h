#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

class MixPanel final : public juce::Component
{
public:
    MixPanel(juce::Slider& subOscGainFaderIn,
             juce::Label& subOscGainLabelIn,
             juce::Slider& osc1GainFaderIn,
             juce::Label& osc1GainLabelIn,
             juce::Slider& osc2GainFaderIn,
             juce::Label& osc2GainLabelIn,
             juce::Slider& osc3GainFaderIn,
             juce::Label& osc3GainLabelIn,
             juce::Colour panelAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void advanceAnimation(float deltaPhase);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    void configureFader(juce::Slider& slider);
    void configureLabel(juce::Label& label, const juce::String& text);

    juce::Slider& subOscGainFader;
    juce::Label& subOscGainLabel;
    juce::Slider& osc1GainFader;
    juce::Label& osc1GainLabel;
    juce::Slider& osc2GainFader;
    juce::Label& osc2GainLabel;
    juce::Slider& osc3GainFader;
    juce::Label& osc3GainLabel;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
