#pragma once

#include <JuceHeader.h>

namespace px3::ui
{

// How every PX3 knob is drawn.
//
// Shared because a knob is a knob: the brushed face, the value ring, the
// pointer, the modulation ring and the MIDI/macro labels are the ecosystem's
// visual language, not the Synth's. An effect product that used JUCE's default
// rotary would be a PX3 panel with somebody else's controls in it.
//
// It reads no config of its own - it is shared by every knob and has no prefix
// to read under - so the colours that vary are set by whoever owns it.
class KnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // The macro colours, resolved from UIConfig by the owner and set here.
    juce::Colour macroAccent { juce::Colour::fromRGB(34, 214, 200) };
    juce::Colour macroLabelBackground { juce::Colour::fromRGBA(226, 249, 246, 219) };
    juce::Colour macroLabelText { juce::Colour::fromRGB(12, 46, 43) };

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPos,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

} // namespace px3::ui
