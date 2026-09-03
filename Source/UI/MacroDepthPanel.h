#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

class UIConfig;

namespace px3::ui
{

// How much of a macro's travel each of its destinations receives.
//
// A transient panel, opened beside the macro knob it belongs to. It edits one
// route per row - a macro-and-parameter PAIR - because that is what the depth
// belongs to: the same parameter driven by two macros has two depths, and one
// macro's several destinations each have their own.
//
// The panel owns no routing state. Every row reads its depth from the
// processor and writes it straight back, so there is no second copy to fall
// out of step with what the DSP is using.
//
// The row count is whatever the macro currently drives - none, one, or dozens.
// The layout answers that by filling columns before it scrolls: the plugin has
// horizontal space, and a single tall column that scrolls at four assignments
// wastes it.
class MacroDepthPanel final : public juce::Component
{
public:
    explicit MacroDepthPanel(PX3SynthAudioProcessor& processorIn);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Which macro this panel is editing. Rebuilds the rows from the
    // processor's current destinations for that macro.
    void setMacro(int macroIndex);
    int getMacro() const noexcept { return macroIndex; }

    // Rebuild the rows from the processor. Called when assignments change
    // underneath the panel - a preset load, or an assignment made elsewhere.
    void refreshFromProcessor();

    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setAccentColour(juce::Colour colour);

    // The size this panel wants for its current row count, given the space it
    // has to sit in. Columns are added before scrolling, so a macro driving
    // eight parameters is two columns of four rather than a scroller.
    juce::Rectangle<int> preferredBoundsWithin(juce::Rectangle<int> available,
                                               juce::Point<int> anchor) const;

    // The panel does not know what closing means - the editor owns the
    // transient state - so it asks.
    std::function<void()> onCloseRequested;

    //---- for the tests ----------------------------------------------------
    juce::String debugHeaderText() const { return header.getText(); }
    int debugRowCount() const { return static_cast<int>(rows.size()); }
    int debugColumnCount() const { return columnsForRows(static_cast<int>(rows.size()), getWidth(), rowAreaHeight()); }
    bool debugIsScrolling() const;
    juce::StringArray debugRowParameterIds() const;
    juce::Slider* debugDepthSliderFor(const juce::String& parameterId);
    juce::Label* debugValueLabelFor(const juce::String& parameterId);
    juce::TextButton& debugCloseButton() { return closeButton; }

private:
    // One route: the parameter it drives, and the control for its depth.
    struct Row
    {
        juce::String parameterId;
        juce::Label name;
        juce::Slider depth { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::Label value;
    };

    int rowHeight() const;
    int columnWidth() const;
    int rowAreaHeight() const;
    int columnsForRows(int rowCount, int width, int height) const;
    void layoutRows();
    void writeDepth(const Row& row);
    void refreshValueLabel(Row& row);

    PX3SynthAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::Colour accent { juce::Colour::fromRGB(90, 220, 200) };
    int macroIndex { -1 };

    juce::Label header;
    juce::Label emptyNotice;
    juce::TextButton closeButton { "Close" };

    // The rows live inside a viewport so that a macro with more destinations
    // than the panel can show scrolls rather than overflowing. With few enough
    // rows the viewport never scrolls and is invisible.
    juce::Viewport viewport;
    juce::Component rowHost;
    std::vector<std::unique_ptr<Row>> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroDepthPanel)
};

} // namespace px3::ui
