#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// The rounded "chip" label used throughout the plugin.
//
// It exists because setting only Label::backgroundColourId and
// outlineColourId gets you JUCE's default rendering, which is a SQUARE
// rectangle. The labels the editor owns were drawn by a class that rounded
// them; the ones ModPanel and AmpEnvelopeComponent owned were not, which is why
// LFO 2/3 and ENV 1/2/3 had square chips while everything else was rounded.
//
// One definition, so the two cannot drift apart again.
class ChipLabel : public juce::Label
{
public:
    void paint(juce::Graphics& g) override
    {
        if (getText().isEmpty())
        {
            return;
        }

        const auto compactLabel = static_cast<bool>(getProperties().getWithDefault("compactLabel", false));
        const auto horizontalPadding = compactLabel ? 4.0f : 8.0f;
        auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 54));
        g.fillRoundedRectangle(area, kCornerRadius);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 96));
        g.drawRoundedRectangle(area, kCornerRadius, 1.0f);

        g.setColour(findColour(juce::Label::textColourId));
        g.setFont(getFont());
        g.drawText(getText(),
                   area.reduced(horizontalPadding, 0.0f).toNearestInt(),
                   juce::Justification::centred,
                   true);
    }

    static constexpr float kCornerRadius = 7.0f;
};

} // namespace px3::ui
