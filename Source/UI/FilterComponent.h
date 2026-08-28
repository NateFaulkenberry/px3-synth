#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <memory>

class UIConfig;

// Reusable filter response visualization that reads cutoff, resonance, and
// mode from provided parameters.
class FilterComponent final : public juce::Component
{
public:
    FilterComponent(juce::AudioParameterFloat& cutoffIn,
                    juce::AudioParameterFloat& resonanceIn,
                    juce::AudioParameterChoice& modeIn,
                    juce::AudioParameterBool& enabledIn,
                    juce::String instanceLabelIn,
                    juce::Colour accentIn);

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    // The filter's controls belong to FltPanel, not to this component - they
    // are its siblings. So this lays out the card and its rows and hands the
    // panel the row rectangles, in this component's own coordinates, instead of
    // the panel computing an interior layout that has to match what is drawn
    // here. Row 3 is the response graph, which this component draws itself.
    void layoutCardInner();
    juce::Rectangle<int> rowBounds(int index) const;
    juce::FlexBox rowFlex(int index) const;
    juce::FlexItem::Margin rowGap(int index) const;
    // Which filter this is: drives the card's style block and its title.
    void setInstanceIndex(int oneBasedIndex);
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters();

    void paint(juce::Graphics& g) override;

private:
    static float clamp01(float value);
    float cutoffNorm() const;
    float resonanceNorm() const;

    juce::AudioParameterFloat& cutoff;
    juce::AudioParameterFloat& resonance;
    juce::AudioParameterChoice& mode;
    juce::AudioParameterBool& enabled;
    juce::String instanceLabel;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
    int instanceIndex { 1 };

    bool currentEnabled { true };
    int lastModeIndex { -1 };
    float lastCutoff { -1.0f };
    float lastResonance { -1.0f };
};
