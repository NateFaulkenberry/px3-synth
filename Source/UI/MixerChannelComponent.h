#pragma once

#include <JuceHeader.h>

#include <memory>

class UIConfig;

class MixerChannelComponent final : public juce::Component
{
public:
    struct Controls
    {
        juce::Label* title { nullptr };
        juce::Component* meter { nullptr };
        juce::Button* mute { nullptr };
        juce::Button* solo { nullptr };
        juce::Slider* fader { nullptr };
        juce::Label* valueLabel { nullptr };
        juce::Slider* pan { nullptr };
        juce::Label* panLabel { nullptr };
        juce::Slider* send { nullptr };
        juce::Label* sendLabel { nullptr };
        juce::Label* stereoTag { nullptr };
        bool hasSend { true };
    };

    explicit MixerChannelComponent(Controls controlsIn);

    void resized() override;

    void applyLayoutFromConfig(const std::shared_ptr<const UIConfig>& config);

private:
    Controls controls;
    int sectionSpacing { 6 };
    int buttonGap { 4 };
    int footerLabelHeight { 12 };
    int meterHeight { 12 };
};
