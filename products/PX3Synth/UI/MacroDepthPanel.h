#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "SpeechBubbleLabel.h"

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
// How a depth row's slider is drawn.
//
// Its own look rather than JUCE's default, for the same reason every other
// control in this synth has one: a stock LinearHorizontal reads as a dialog
// widget dropped into an instrument. A flat track, a filled portion from the
// centre, and a round cap - all of it from UIConfig under "macroDepth.colors".
//
// Filled from the CENTRE, not from the left, because depth is bipolar: a route
// at -60% and one at +60% should look like mirror images rather than like one
// being nearly empty and the other nearly full.
class MacroDepthSliderLook final : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

    juce::Colour track { juce::Colour::fromRGBA(255, 255, 255, 38) };
    juce::Colour fill { juce::Colour::fromRGB(90, 220, 200) };
    juce::Colour thumb { juce::Colour::fromRGB(220, 250, 244) };
    float trackThickness { 3.0f };
    float thumbRadius { 5.0f };
};

class MacroDepthPanel final : public juce::Component
{
public:
    explicit MacroDepthPanel(PX3SynthAudioProcessor& processorIn);
    ~MacroDepthPanel() override;

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

    // Where the macro knob is, in this panel's own coordinates, so the pointer
    // on the left edge aims at it. Set by the editor when the panel is placed.
    void setPointerTargetY(int yInPanelCoordinates);

    //---- for the tests ----------------------------------------------------
    juce::String debugHeaderText() const { return header.getText(); }
    int debugRowCount() const { return static_cast<int>(rows.size()); }
    int debugColumnCount() const { return columnsForRows(static_cast<int>(rows.size()), getWidth(), rowAreaHeight()); }
    bool debugIsScrolling() const;
    juce::StringArray debugRowParameterIds() const;
    juce::Slider* debugDepthSliderFor(const juce::String& parameterId);
    juce::Label* debugValueLabelFor(const juce::String& parameterId);
    juce::TextButton& debugCloseButton() { return closeButton; }
    int debugPointerTargetY() const { return pointerTargetY; }
    juce::String debugEmptyNotice() const { return emptyNotice; }
    juce::LookAndFeel* debugSliderLookAndFeel(const juce::String& parameterId)
    {
        auto* slider = debugDepthSliderFor(parameterId);
        return slider != nullptr ? &slider->getLookAndFeel() : nullptr;
    }

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
    int pointerWidth() const;
    // The bubble this panel is drawn as - the update notice's shape, turned to
    // point left at the macro knob. Built per paint from UIConfig, so styling
    // it is a config edit rather than a rebuild.
    SpeechBubble::Style bubbleStyle() const;
    void applyStyleFromConfig();
    int columnWidth() const;
    int rowAreaHeight() const;
    int columnsForRows(int rowCount, int width, int height) const;
    void layoutRows();
    void writeDepth(const Row& row);
    void refreshValueLabel(Row& row);

    PX3SynthAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::Colour accent { juce::Colour::fromRGB(90, 220, 200) };
    MacroDepthSliderLook sliderLook;
    int pointerTargetY { -1 };
    int macroIndex { -1 };

    juce::Label header;
    // Painted rather than a Label: a Label does not wrap, and this sentence is
    // longer than a narrow panel is wide.
    juce::String emptyNotice;
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
