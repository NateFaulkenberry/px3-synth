#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <array>
#include <memory>

class UIConfig;

// Reusable oscillator UI component that owns oscillator-only layout, mode UI,
// and waveform visualization while using externally-owned controls.
class OscillatorComponent final : public juce::Component
{
public:
    OscillatorComponent(juce::ToggleButton& enabledButtonIn,
                        juce::Slider& pitchIn,
                        juce::Label& pitchLabelIn,
                        juce::Label& pitchValueLabelIn,
                        juce::Slider& macroAIn,
                        juce::Slider& macroBIn,
                        juce::Slider& macroCIn,
                        juce::Label& macroALabelIn,
                        juce::Label& macroBLabelIn,
                        juce::Label& macroCLabelIn,
                        juce::Label& macroAValueLabelIn,
                        juce::Label& macroBValueLabelIn,
                        juce::Label& macroCValueLabelIn,
                        juce::ComboBox& modeBoxIn,
                        juce::Label& modeLabelIn,
                        juce::ComboBox& vowelBoxIn,
                        juce::Label& vowelLabelIn,
                        juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    // Which oscillator this instance is. Drives both the card's title, which is
    // the component's own content, and which style block it reads - so Osc 1,
    // 2 and 3 share one implementation and can still be styled independently.
    void setInstanceIndex(int oneBasedIndex);
    // The panel's content box: the only reference for percentage dimensions.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void refreshFromParameters(bool enabled, int modeIndex, int vowelIndex);
    void advanceAnimation(float deltaPhase);
    // The animation clock. Exposed so a test can assert it only ever moves
    // forwards: the drawn shape is a continuous function of it, so a phase that
    // never jumps is a curve that never jumps.
    double animationPhase() const noexcept { return phase; }

    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void paint(juce::Graphics& g) override;

private:
    void applyModeUi();
    void applyEnabledUi();

    juce::ToggleButton& enabledButton;
    juce::Slider& pitch;
    juce::Label& pitchLabel;
    juce::Label& pitchValueLabel;
    juce::Slider& macroA;
    juce::Slider& macroB;
    juce::Slider& macroC;
    juce::Label& macroALabel;
    juce::Label& macroBLabel;
    juce::Label& macroCLabel;
    juce::Label& macroAValueLabel;
    juce::Label& macroBValueLabel;
    juce::Label& macroCValueLabel;
    juce::ComboBox& modeBox;
    juce::Label& modeLabel;
    juce::ComboBox& vowelBox;
    juce::Label& vowelLabel;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    int instanceIndex { 1 };

    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    bool currentEnabled { true };

    // Double, and deliberately NOT wrapped at 2pi. Wrapping is only invisible
    // for a shape built from whole multiples of the phase; SUPER SAW, WAVETABLE,
    // FORMANT, FM and the rest use fractional multipliers, so subtracting 2pi
    // moved each partial by a part-cycle and the curve jumped every ~2.3s. A
    // free-running phase is also the truer picture: detuned partials really do
    // beat against each other over time.
    double phase { 0.0 };
    int lastModeIndex { -1 };
};
