#pragma once

#include <JuceHeader.h>

#include "MixerControls.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

class PX3SynthAudioProcessor;
class UIConfig;

namespace px3::ui
{

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

    std::function<void()> onClose;

protected:
    // Where a subclass hangs its own attachments. Cleared before each rebuild.
    virtual void rebuildForBus() = 0;
    virtual juce::String sheetTitle() const = 0;

    void paint(juce::Graphics& g) override;

    PX3SynthAudioProcessor& processor;
    std::shared_ptr<const UIConfig> uiConfig;
    int busIndex { 0 };
    juce::String busName { "DRY" };

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAttachments;

    juce::TextButton closeButton { "CLOSE" };
    MixerToggleButton enableButton { "ON" };
    juce::LookAndFeel* knobLookAndFeel { nullptr };

    virtual void knobLookAndFeelChanged() {}

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
    void paint(juce::Graphics& g) override;

private:
    static constexpr int kBandCount = 4;

    void rebuildForBus() override;
    juce::String sheetTitle() const override { return busName + " EQ"; }
    void timerCallback() override;
    void refreshReadouts();

    // The response curve, in the sheet's own coordinates. Read from the live
    // processor rather than recomputed here, so what is drawn is what is
    // running.
    void paintCurve(juce::Graphics& g, juce::Rectangle<float> area) const;

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

    std::array<BandStrip, kBandCount> bands;
    juce::Rectangle<int> curveArea;
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
    void timerCallback() override;

    // A moving-coil VU face reading gain reduction, which on this unit runs
    // right to left - the needle falls as the compressor works.
    void paintMeter(juce::Graphics& g, juce::Rectangle<float> area) const;

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
    MixerToggleButton linkButton { "LINK" };

    void knobLookAndFeelChanged() override;

    juce::Rectangle<int> meterArea;
    float meterDb { 0.0f };
};

} // namespace px3::ui
