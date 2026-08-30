#pragma once

#include <JuceHeader.h>

#include "../DSP/BusInsertTypes.h"
#include "BusEqGraph.h"
#include "FetPanelStyle.h"
#include "VuMeterComponent.h"
#include "Card.h"
#include "MixerControls.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class PX3SynthAudioProcessor;
class UIConfig;

namespace px3::ui
{

// The sheets' close control: an X in a ring, built as a Path for the same
// reasons the power symbol is - two primitives cost less than parsing artwork,
// stay crisp at any size, and can be tinted per sheet without a second asset.
//
// It deliberately mirrors BypassButton's seat and ring so the two read as
// members of one family: the power button turns a section on, this one shuts a
// sheet, and nothing else on the panel is a circular glyph.
class SheetCloseButton final : public juce::Button
{
public:
    struct Style
    {
        int size { 24 };
        int offsetX { 0 };
        int offsetY { 0 };
        float ringWidth { 1.6f };
        float glyphWidth { 2.0f };
        float glyphInset { 0.32f };   // fraction of the button, per side
        juce::Colour seat { juce::Colour::fromRGBA(12, 14, 20, 190) };
        juce::Colour ring { juce::Colour::fromRGBA(237, 241, 247, 150) };
        juce::Colour glyph { juce::Colour::fromRGB(237, 241, 247) };
        juce::Colour hover { juce::Colour::fromRGB(185, 191, 200) };
    };

    SheetCloseButton();

    void applyStyle(const Style& styleIn);
    const Style& getStyle() const noexcept { return style; }

private:
    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    Style style;
};

// The two bus-insert sheets: a four band parametric EQ and a FET compressor.
//
// Both are BUS AGNOSTIC. Each is constructed once and retargeted with setBus,
// which rebuilds its parameter attachments against that bus's parameter set. A
// third bus is a third entry in the processor's insert array and one more
// mixer strip with buttons - not another overlay.
//
// Neither draws the preset browser's panel. They share its backdrop, its scrim
// and its click-outside-to-close behaviour, because those are what make a sheet
// a sheet; the faces are their own.
class BusInsertOverlay : public juce::Component
{
public:
    BusInsertOverlay(PX3SynthAudioProcessor& processorIn);
    ~BusInsertOverlay() override;

    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    // The editor owns the knob look, as it does for the mixer.
    void setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel);
    // Rebuilds every attachment against the given bus. Safe to call while
    // visible, though in practice it is called as the sheet opens.
    void setBus(int bus);
    int getBus() const noexcept { return busIndex; }

    // The sheet's title: the bus's name, which the editor supplies because the
    // mixer owns the strip names.
    void setBusName(juce::String name);

    // Called as the sheet is shown and hidden, so anything that costs something
    // to run - the spectrum tap - only runs while it is on screen.
    virtual void setSheetVisible(bool shown);

    // Applies "is this processor bypassed" to every control on the face.
    //
    // Public and separate from the timer that normally drives it because the
    // timer is a PRIVATE base - juce::Timer is inherited privately here, so a
    // caller outside cannot reach timerCallback even by dynamic_cast, and this
    // is otherwise only observable by waiting for a message loop.
    virtual void refreshControlEnablement() {}

    std::function<void()> onClose;

protected:
    // Where a subclass hangs its own attachments. Cleared before each rebuild.
    virtual void rebuildForBus() = 0;
    virtual juce::String sheetTitle() const = 0;
    // The block under `cards` this sheet reads its frame from.
    virtual juce::String cardStyleKey() const = 0;

    // The sheet wears the same frame as every card in the plugin: a border, a
    // padding gap over a translucent background, and then one solid panel
    // inside it. The card system already owns the first two; what a card puts
    // in the third place is a two-part gloss, and these sheets put a single
    // solid fill there instead - the "inner overlay" the controls sit on.
    struct InnerOverlayStyle
    {
        float margin { 6.0f };
        float radius { 6.0f };
        juce::Colour colour { juce::Colour::fromRGB(12, 14, 18) };
        float opacity { 1.0f };

        juce::Colour effectiveColour() const
        {
            return colour.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, opacity));
        }
    };

    // Inside the card's padding, inset by the inner overlay's own margin. This
    // is where a subclass lays its controls out.
    juce::Rectangle<int> innerOverlayBounds() const;
    // The strip between the card title and the inner overlay, where the enable
    // and close buttons live.
    juce::Rectangle<int> headerBounds() const;
    // This bus's identity colour, taken from its mixer strip so the sheet and
    // the strip that opened it are visibly the same channel.
    juce::Colour busAccentColour() const;

    void refreshCardStyle();
    // Restyles ON and CLOSE from busInserts.{enableButton,closeButton}, with a
    // per-sheet override under busInserts.<eq|comp>.
    void refreshHeaderButtonStyles();
    // The header row, laid out from the two buttons' own configured sizes.
    void layoutHeaderButtons();
    void paint(juce::Graphics& g) override;

    PX3SynthAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    int busIndex { 0 };
    juce::String busName { "DRY" };

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAttachments;

    SheetCloseButton closeButton;
    MixerToggleButton enableButton { "ON" };
    MixerToggleButton::Style enableStyle;
    // Both header controls are placed by coordinate from the content box's top
    // right, so the header row can be zero-height and they still land somewhere
    // deliberate.
    int enableOffsetX { 0 };
    int enableOffsetY { 0 };
    juce::LookAndFeel* knobLookAndFeel { nullptr };

    px3::ui::CardHost card;
    InnerOverlayStyle innerStyle;

    virtual void knobLookAndFeelChanged() {}
    // A live config reload replaces the UIConfig object, and anything a
    // subclass owns that reads config has to be told - the alternative is the
    // stale-style bug the Card system exists to prevent.
    virtual void uiConfigChanged() {}

protected:
    // Attachments point at the SUBCLASS's sliders and buttons, which are gone
    // by the time a base destructor runs - so each subclass has to release them
    // itself. Doing it in ~BusInsertOverlay crashed on shutdown with a sheet
    // open, which is exactly the bug the editor lifecycle test was written for
    // the first time.
    void clearAttachments();

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BusInsertOverlay)
};

// ---------------------------------------------------------------------------
// EQ
// ---------------------------------------------------------------------------
class BusEqOverlay final : public BusInsertOverlay,
                           private juce::Timer
{
public:
    explicit BusEqOverlay(PX3SynthAudioProcessor& processorIn);
    ~BusEqOverlay() override;

    void resized() override;

private:
    static constexpr int kBandCount = 4;

    void rebuildForBus() override;
    juce::String sheetTitle() const override { return busName + " EQ"; }
    juce::String cardStyleKey() const override { return "busInsertEq"; }
    void setSheetVisible(bool shown) override;
    void timerCallback() override;
    void refreshReadouts();

public:
    void refreshControlEnablement() override;

private:

    struct BandStrip
    {
        juce::ComboBox type;
        juce::Slider frequency;
        juce::Slider gain;
        juce::Slider q;
        juce::Label caption;
        juce::Label frequencyValue;
        juce::Label gainValue;
        juce::Label qValue;
        juce::Label frequencyCaption;
        juce::Label gainCaption;
        juce::Label qCaption;
    };

    void knobLookAndFeelChanged() override;
    void uiConfigChanged() override;

    std::array<BandStrip, kBandCount> bands;
    BusEqGraph graph;
    // The cached state, plus whether it has ever been applied.
    //
    // The flag alone is not enough: it starts false and both processors default
    // to disabled, so "has it changed" is false on the first poll and the
    // controls would never actually be greyed out - they would simply stay in
    // whatever state they were constructed in, which is enabled.
    bool controlsLive { false };
    bool enablementApplied { false };
};

// ---------------------------------------------------------------------------
// Compressor
// ---------------------------------------------------------------------------
class BusCompOverlay final : public BusInsertOverlay,
                             private juce::Timer
{
public:
    explicit BusCompOverlay(PX3SynthAudioProcessor& processorIn);
    ~BusCompOverlay() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void rebuildForBus() override;
    juce::String sheetTitle() const override { return busName + " COMP"; }
    juce::String cardStyleKey() const override { return "busInsertComp"; }
    void timerCallback() override;

public:
    void refreshControlEnablement() override;

private:

    // A moving-coil VU face reading gain reduction, which on this unit runs
    // right to left - the needle falls as the compressor works.
    px3::CompMeterMode meterMode() const;

    juce::Slider input;
    juce::Slider output;
    juce::Slider attack;
    juce::Slider release;
    juce::Slider mix;

    juce::Label inputCaption;
    juce::Label outputCaption;
    juce::Label attackCaption;
    juce::Label releaseCaption;
    juce::Label mixCaption;
    juce::Label mixValue;

    std::array<juce::TextButton, 5> ratioButtons;
    // GR / IN / OUT, under the movement, switching what it is wired to.
    std::array<juce::TextButton, 3> meterModeButtons;
    juce::TextButton linkButton { "LINK" };

    void knobLookAndFeelChanged() override;
    void uiConfigChanged() override;

    // Panel furniture is painted rather than laid out as components, so
    // resized() records where each piece went and paint() draws onto it.
    // The movement is its own component: it caches its face, animates its
    // needle on real elapsed time, and repaints without touching the panel.
    VuMeterComponent meter;
    juce::Rectangle<int> meterArea;
    juce::Rectangle<float> inputKnobArea;
    juce::Rectangle<float> outputKnobArea;
    juce::Rectangle<float> ratioBankArea;
    juce::Rectangle<float> mixBankArea;
    juce::Rectangle<float> mixLabelArea;
    juce::Rectangle<int> meterModeArea;

    FetPushButtonLookAndFeel pushLook;
    // The cached state, plus whether it has ever been applied.
    //
    // The flag alone is not enough: it starts false and both processors default
    // to disabled, so "has it changed" is false on the first poll and the
    // controls would never actually be greyed out - they would simply stay in
    // whatever state they were constructed in, which is enabled.
    bool controlsLive { false };
    bool enablementApplied { false };
};

} // namespace px3::ui
