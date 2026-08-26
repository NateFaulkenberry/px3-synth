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

private:
    struct ChannelWidgets
    {
        juce::Label title;
        juce::Label stereoTag;
        MixerLevelMeter meter;
        MuteButton mute;
        SoloButton solo;
        FaderSlider fader;
        juce::Label valueLabel;
        juce::Slider pan;
        juce::Label panLabel;
        juce::Slider send;
        juce::Label sendLabel;
        std::unique_ptr<MixerChannelComponent> component;
        bool hasSend { true };
    };

    void configureChannelWidgets(ChannelWidgets& channel, const juce::String& titleText, bool hasSend, bool stereoTagVisible);
    void applyConfigToChannels();
    void timerCallback() override;
    void refreshMeterValues();
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
    ChannelWidgets fxChannel;
    std::array<ChannelWidgets*, 5> channels;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
};
