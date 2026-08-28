#pragma once

#include <JuceHeader.h>

#include "Card.h"

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

    // Foundation for the shared Card system. The mixer channel now draws the
    // same frame as every other component and reads a style block of its own,
    // so it participates in the system without its internals being redesigned:
    // its controls, and its existing title Label, are untouched. The card's own
    // title is intentionally left empty here - moving the title off the Label
    // belongs to the phase that reworks mixer internals.
    void setCardStyleKey(juce::String key);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setPanelContentBounds(juce::Rectangle<int> panelContent);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void refreshCardStyle();

    Controls controls;
    std::shared_ptr<const UIConfig> uiConfig;
    juce::String cardStyleKey { "mixerChannel" };
    px3::ui::CardHost card;
    int sectionSpacing { 6 };
    int buttonGap { 4 };
    int footerLabelHeight { 12 };
    int meterHeight { 12 };
};
