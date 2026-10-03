#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <memory>
#include <utility>
#include <vector>

class UIConfig;

class DelayComponent final : public juce::Component
{
public:
    DelayComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Slider& amountKnobIn,
                        juce::Label& amountLabelIn,
                        juce::ComboBox& algorithmBoxIn,
                        juce::Label& algorithmLabelIn,
                        juce::ComboBox& syncBoxIn,
                        juce::Label& syncLabelIn,
                        juce::ComboBox& modeBoxIn,
                        juce::Label& modeLabelIn,
                        juce::Slider& timeKnobIn,
                        juce::Label& timeLabelIn,
                        juce::Slider& feedbackKnobIn,
                        juce::Label& feedbackLabelIn,
                        juce::Colour accentIn);

    // The algorithm-specific controls, handed over the same way as the rest:
    // TAPE shows QUALITY / WOBBLE / SLIP, MODULATED shows MOD DEPTH, and the
    // other algorithms show neither (the row folds away). Optional: a delay
    // built without them lays out exactly as before.
    struct AlgorithmControls
    {
        juce::Slider* quality { nullptr };
        juce::Label* qualityLabel { nullptr };
        juce::Slider* wobble { nullptr };
        juce::Label* wobbleLabel { nullptr };
        juce::Slider* slip { nullptr };
        juce::Label* slipLabel { nullptr };
        juce::Slider* modDepth { nullptr };
        juce::Label* modDepthLabel { nullptr };
    };
    void setAlgorithmControls(const AlgorithmControls& controls);
    // The algorithm index (0 Granular, 1 Tape, ... 5 Modulated), which decides
    // which algorithm controls are shown.
    void setAlgorithm(int algorithmIndex);
    int visibleAlgorithmControlCount() const noexcept;

    void setAccentColour(juce::Colour accentIn);
    void setActive(bool enabled, bool granularModeSelectable);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    juce::ToggleButton& enabledButton;
    juce::Slider& amountKnob;
    juce::Label& amountLabel;
    juce::ComboBox& algorithmBox;
    juce::Label& algorithmLabel;
    juce::ComboBox& syncBox;
    juce::Label& syncLabel;
    juce::ComboBox& modeBox;
    juce::Label& modeLabel;
    juce::Slider& timeKnob;
    juce::Label& timeLabel;
    juce::Slider& feedbackKnob;
    juce::Label& feedbackLabel;
    AlgorithmControls algorithmControls;
    int algorithm { 0 };
    std::vector<std::pair<juce::Slider*, juce::Label*>> shownAlgorithmControls() const;

    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool isActive { true };
};
