#pragma once

#include <JuceHeader.h>

#include "LfoComponent.h"
#include "OscillatorComponent.h"
#include "SubOscComponent.h"

#include <array>
#include <memory>

class UIConfig;

class OscPanel final : public juce::Component
{
public:
    OscPanel(juce::ToggleButton& subEnabledButton,
             juce::Label& subEnabledLabel,
             juce::ComboBox& subOctaveBox,
             juce::Label& subOctaveLabel,
             juce::ComboBox& subWaveformBox,
             juce::Label& subWaveformLabel,
             juce::Slider& osc1MacroA,
             juce::Slider& osc1MacroB,
             juce::Slider& osc1MacroC,
             juce::ToggleButton& osc1EnabledButton,
             juce::Label& osc1EnabledLabel,
             juce::Label& osc1MacroALabel,
             juce::Label& osc1MacroBLabel,
             juce::Label& osc1MacroCLabel,
             juce::ComboBox& osc1ModeBox,
             juce::Label& osc1ModeLabel,
             juce::ComboBox& osc1VowelBox,
             juce::Label& osc1VowelLabel,
             juce::Slider& osc2MacroA,
             juce::Slider& osc2MacroB,
             juce::Slider& osc2MacroC,
             juce::ToggleButton& osc2EnabledButton,
             juce::Label& osc2EnabledLabel,
             juce::Label& osc2MacroALabel,
             juce::Label& osc2MacroBLabel,
             juce::Label& osc2MacroCLabel,
             juce::ComboBox& osc2ModeBox,
             juce::Label& osc2ModeLabel,
             juce::ComboBox& osc2VowelBox,
             juce::Label& osc2VowelLabel,
             juce::Slider& osc3MacroA,
             juce::Slider& osc3MacroB,
             juce::Slider& osc3MacroC,
             juce::ToggleButton& osc3EnabledButton,
             juce::Label& osc3EnabledLabel,
             juce::Label& osc3MacroALabel,
             juce::Label& osc3MacroBLabel,
             juce::Label& osc3MacroCLabel,
             juce::ComboBox& osc3ModeBox,
             juce::Label& osc3ModeLabel,
             juce::ComboBox& osc3VowelBox,
             juce::Label& osc3VowelLabel,
             juce::Label& lfoAssignLabel,
             juce::ComboBox& lfoAssignBox,
             juce::Slider& lfoRateKnob,
             juce::Label& lfoRateLabel,
             juce::Label& lfoRateValueLabel,
             juce::ComboBox& lfoWaveformBox,
             juce::Label& lfoWaveformLabel,
             juce::Colour subAccent,
             juce::Colour oscAccent,
             juce::Colour lfoAccent);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshOscillatorFromParameters(int oscIndex, bool enabled, int modeIndex, int vowelIndex);
    void refreshLfoFromParameters(float rateHz, int waveformIndex);
    void refreshSubOscFromParameters(bool enabled, int octaveIndex, int waveformIndex);
    void advanceAnimation(float oscDeltaPhase, float lfoDeltaPhase);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

private:
    std::unique_ptr<SubOscComponent> subOscComponent;
    std::array<std::unique_ptr<OscillatorComponent>, 3> oscillatorComponents;
    std::unique_ptr<LfoComponent> lfoComponent;

    juce::Colour accent;
    juce::Colour subHeaderAccent;
    juce::Colour oscHeaderAccent;
    juce::Colour lfoHeaderAccent;
    std::shared_ptr<const UIConfig> uiConfig;
};
