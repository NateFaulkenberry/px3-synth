#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

// Reusable filter response visualization that reads cutoff, resonance, and
// mode from provided parameters.
class FilterComponent final : public juce::Component
{
public:
    FilterComponent(juce::AudioParameterFloat& cutoffIn,
                    juce::AudioParameterFloat& resonanceIn,
                    juce::AudioParameterChoice& modeIn,
                    juce::AudioParameterBool& enabledIn,
                    juce::String instanceLabelIn,
                    juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setGraphBounds(juce::Rectangle<int> boundsIn);
    void refreshFromParameters();

    void paint(juce::Graphics& g) override;

private:
    static float clamp01(float value);
    float cutoffNorm() const;
    float resonanceNorm() const;

    juce::AudioParameterFloat& cutoff;
    juce::AudioParameterFloat& resonance;
    juce::AudioParameterChoice& mode;
    juce::AudioParameterBool& enabled;
    juce::String instanceLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::Rectangle<int> graphBounds;

    bool currentEnabled { true };
    int lastModeIndex { -1 };
    float lastCutoff { -1.0f };
    float lastResonance { -1.0f };
};
