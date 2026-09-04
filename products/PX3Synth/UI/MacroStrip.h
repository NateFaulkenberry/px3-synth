#pragma once

#include <JuceHeader.h>

#include "ChipLabel.h"
#include "ParameterKnob.h"
#include "ToggleChipButton.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class PX3SynthAudioProcessor;
class UIConfig;

// The four macro knobs, stacked, on the left of every panel.
//
// ONE of these exists. It is placed outside the rectangle the panels are laid
// out in, so "the same four macros on every panel" is not something the code
// arranges - there is nothing else for a panel to show. Switching panels
// cannot reset or duplicate macro state because switching panels does not
// touch this component at all.
class MacroStrip final : public juce::Component
{
public:
    // The processor is only forward-declared here, so its kMacroCount cannot
    // size this strip's array. A static_assert in the .cpp keeps the two in
    // step, and fails the build rather than silently sizing the strip wrong.
    static constexpr int kCount = 5;

    MacroStrip(PX3SynthAudioProcessor& processorIn, juce::LookAndFeel* knobLookAndFeel);
    ~MacroStrip() override;

    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void resized() override;
    void paint(juce::Graphics& g) override;

    // Which macro is being assigned to, or -1. Drives the highlight on the
    // strip; the destinations are highlighted by the editor.
    void setAssigningMacro(int macroIndex);
    int getAssigningMacro() const noexcept { return assigningMacro; }

    // Opening and closing a macro's depth panel. The strip does not know what
    // a depth panel is - the editor owns that - so it asks, and is told back
    // which macro's panel is open so the buttons can show it.
    std::function<void(int)> onDepthToggled;
    void setDepthPanelMacro(int macroIndex);

    // Which macro's depth button is under a point in this strip's coordinates,
    // or -1. The editor needs this because while a depth panel is open its
    // scrim swallows every click outside the panel, so the buttons cannot
    // receive their own - see the hit test in the editor's mouseDown.
    int depthButtonAt(juce::Point<int> pointInStrip) const;

    // For the layout tests: the caption ABOVE a knob, and the button below it.
    juce::Label& debugCaption(int macroIndex)
    { return entries[static_cast<std::size_t>(juce::jlimit(0, kCount - 1, macroIndex))].caption; }
    juce::Button& debugDepthButton(int macroIndex)
    { return entries[static_cast<std::size_t>(juce::jlimit(0, kCount - 1, macroIndex))].depth; }

    juce::Slider& knob(int macroIndex);
    const juce::Slider& knob(int macroIndex) const;

    // The width the strip wants, from config.
    static int preferredWidth(const UIConfig* config);

private:
    PX3SynthAudioProcessor& processor;

    struct Entry
    {
        juce::Slider knob;
        px3::ui::ChipLabel caption;
        px3::ui::ToggleChipButton depth;
    };
    std::array<Entry, kCount> entries;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> attachments;

    std::shared_ptr<const UIConfig> uiConfig;
    int assigningMacro { -1 };
    int depthPanelMacro { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroStrip)
};
