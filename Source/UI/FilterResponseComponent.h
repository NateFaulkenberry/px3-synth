#pragma once

#include <JuceHeader.h>

// Reusable filter response visualization that reads cutoff, resonance, and
// mode from provided parameters.
class FilterResponseComponent final : public juce::Component
{
public:
    FilterResponseComponent(juce::AudioParameterFloat& cutoffIn,
                            juce::AudioParameterFloat& resonanceIn,
                            juce::AudioParameterChoice& modeIn,
                            juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void refreshFromParameters();

    void paint(juce::Graphics& g) override;

private:
    static float clamp01(float value);
    float cutoffNorm() const;
    float resonanceNorm() const;

    juce::AudioParameterFloat& cutoff;
    juce::AudioParameterFloat& resonance;
    juce::AudioParameterChoice& mode;
    juce::Colour accent;

    int lastModeIndex { -1 };
    float lastCutoff { -1.0f };
    float lastResonance { -1.0f };
};
