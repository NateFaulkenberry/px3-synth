#pragma once

#include <JuceHeader.h>

#include "../DSP/MidiMapping.h"

#include <memory>
#include <vector>

namespace px3::ui
{

// Bind a slider to a parameter, and leave the parameter's ID on the slider.
//
// Every mappable knob in this synth is a plain juce::Slider bound by a
// SliderParameterAttachment, constructed in six different files with no common
// subclass and no single choke point. MIDI mapping needs to know which sliders
// are parameter controls and which parameter each one drives - and the answer
// must not be a hard-coded list, because a list is a thing somebody forgets to
// update when they add a knob.
//
// So the binding stamps the answer onto the slider as it is made. The property
// bag is where this UI already keeps per-knob metadata - the rotary
// look-and-feel reads "modulatedPos" and "knobBypassed" from it - so this
// extends an existing mechanism rather than introducing a parallel registry.
//
// Use this instead of constructing a SliderParameterAttachment directly. A
// knob bound the old way still works; it is simply invisible to MIDI mapping,
// which is the failure mode you want if one is ever missed.
inline void attachParameterKnob(juce::RangedAudioParameter& parameter,
                                juce::Slider& slider,
                                std::vector<std::unique_ptr<juce::SliderParameterAttachment>>& attachments)
{
    slider.getProperties().set(px3::knob_properties::parameterId, parameter.getParameterID());
    attachments.push_back(std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

// The same stamp for a knob whose attachment is owned somewhere that is not a
// vector - a single unique_ptr member, say.
inline std::unique_ptr<juce::SliderParameterAttachment>
makeParameterKnobAttachment(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    slider.getProperties().set(px3::knob_properties::parameterId, parameter.getParameterID());
    return std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr);
}

// The ID stamped on a slider, or empty if it is not a parameter control.
inline juce::String parameterIdOf(const juce::Slider& slider)
{
    return slider.getProperties()
        .getWithDefault(px3::knob_properties::parameterId, juce::String())
        .toString();
}

inline bool isParameterKnob(const juce::Slider& slider)
{
    return parameterIdOf(slider).isNotEmpty();
}

} // namespace px3::ui
