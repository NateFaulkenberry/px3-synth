#pragma once

#include <JuceHeader.h>

namespace px3::ui
{
struct ComponentCardStyle
{
    float borderPadding { 6.0f };
    float cornerRadius { 8.0f };

    float fillInset { 0.0f };

    juce::Colour backgroundColour { juce::Colours::black };
    float backgroundAlpha { 0.08f };

    juce::Colour topFillColour { juce::Colours::black };
    float topFillAlpha { 0.10f };
    float topFillHeightRatio { 0.5f };

    bool drawOutline { false };
    juce::Colour outlineColour { juce::Colours::white };
    float outlineAlpha { 1.0f };
    float outlineThickness { 1.2f };
};

juce::Rectangle<float> drawComponentCard(juce::Graphics& g,
                                         juce::Rectangle<float> bounds,
                                         const ComponentCardStyle& style);

} // namespace px3::ui
