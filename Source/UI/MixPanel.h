#pragma once

#include <JuceHeader.h>

#include "MixerChannelComponent.h"
#include "MixerControls.h"

#include <array>
#include <memory>
#include <vector>

class PX3SynthAudioProcessor;

class UIConfig;

class MixPanel final : public juce::Component,
                       private juce::Timer
{
public:
    MixPanel(PX3SynthAudioProcessor& processorIn,
             juce::LookAndFeel* knobLookAndFeelIn,
             juce::Colour panelAccent);

    ~MixPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshFromParameters();
    void advanceAnimation(float deltaPhase);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    // Raised when a strip's EQ or COMP button is clicked. The panel does not
    // own the overlays - the editor does, because they cover the whole window.
    std::function<void(int bus, bool wantsEq)> onOpenBusInsert;

private:
    struct ChannelWidgets
    {
        juce::Label title;
        juce::Label stereoTag;
        MixerLevelMeter meter;
        MuteButton mute;
        SoloButton solo;
        PhaseButton phase;
        FaderSlider fader;
        juce::Label valueLabel;
        PanKnob pan;
        juce::Label panLabel;
        juce::Label panValueLabel;
        juce::Slider send;
        juce::Label sendLabel;
        juce::Label sendValueLabel;
        // Only the dry and FX strips build these; the rest leave them null so
        // the channel component skips the corners.
        std::unique_ptr<InsertButton> eqInsert;
        std::unique_ptr<InsertButton> compInsert;
        std::unique_ptr<MixerChannelComponent> component;
        bool hasSend { true };
    };

    void configureChannelWidgets(ChannelWidgets& channel, const juce::String& titleText, bool hasSend, bool stereoTagVisible);
    // Gives a strip its two insert buttons. Called only for the buses that have
    // inserts; adding a third bus is one more call.
    void addInsertButtons(ChannelWidgets& channel, int bus);
    // Whether each insert's button has been pressed yet, per bus.
    //
    // The first press engages an insert that is off, so opening the overlay
    // does not land on a bypassed unit where the first edit changes nothing
    // audible. Every press after that only opens it - once someone has
    // switched a unit off deliberately, re-opening it must not switch it back
    // on underneath them.
    //
    // Deliberately NOT persisted. It is a fact about this editor session, not
    // about the patch, and putting it in the parameter set would serialise UI
    // bookkeeping into every preset and DAW session.
    std::array<bool, 2> eqButtonPressed { { false, false } };
    std::array<bool, 2> compButtonPressed { { false, false } };
    void refreshInsertButtonStates();
    void applyConfigToChannels();
    void timerCallback() override;
    void refreshMeterValues();
    void refreshKnobReadouts(ChannelWidgets& channel);
    static juce::String linearGainToDbText(float linearGain);
    static MixerToggleButton::Style buttonStyleFromConfig(const std::shared_ptr<const UIConfig>& uiConfig,
                                                           const juce::String& pathPrefix,
                                                           const MixerToggleButton::Style& fallback);

    PX3SynthAudioProcessor& processor;
    juce::LookAndFeel* knobLookAndFeel { nullptr };
    ChannelWidgets subChannel;
    ChannelWidgets osc1Channel;
    ChannelWidgets osc2Channel;
    ChannelWidgets osc3Channel;
    ChannelWidgets dryChannel;
    ChannelWidgets fxChannel;
    // Six strips: the four sources, the dry bus, then the FX return. The dry
    // channel sits between them because that is where it sits in the signal
    // path - the sources sum into it, and it meets the FX return after.
    std::array<ChannelWidgets*, 6> channels;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
