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
    const px3::ui::ControlStyle& rowControl(int index) const;
    juce::Colour cardAccentColour() const;
    juce::Rectangle<int> powerBounds() const;
    // Which filter this is: drives the card's style block and its title.
    void setInstanceIndex(int oneBasedIndex);
    // The comb's parameters, so the response graph can draw its teeth. Passed
    // rather than read from a processor: this component holds parameter
    // references, not a processor.
    void setCombParameters(juce::AudioParameterFloat& tune,
                           juce::AudioParameterFloat& decay,
                           juce::AudioParameterFloat& damping);
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters();

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    // Called when the card background is clicked. FltPanel owns this filter's
    // power button, so it performs the toggle.
    std::function<void()> onBackgroundClick;

private:
    static float clamp01(float value);
    // The graph is a picture of the response shape, and that shape barely moves
    // between 44.1 and 96 kHz over the audible range. Drawing at a fixed rate
    // keeps the curve identical whatever the host is running at, rather than
    // having it shift when a session is opened at a different rate.
    static constexpr double kGraphSampleRate = 48000.0;

    float cutoffNorm() const;
    float resonanceNorm() const;

    juce::AudioParameterFloat& cutoff;
    juce::AudioParameterFloat& resonance;
    juce::AudioParameterChoice& mode;
    // Comb mode's shape comes from its own controls, not from cutoff and
    // resonance, so the graph needs them to draw anything meaningful.
    juce::AudioParameterFloat* combTune { nullptr };
    juce::AudioParameterFloat* combDecay { nullptr };
    juce::AudioParameterFloat* combDamping { nullptr };
    // Last drawn comb values. The graph is drawn from these parameters, so a
    // change to one has to trigger a repaint - watching only cutoff and
    // resonance left the curve stale while a comb knob was being turned.
    float lastCombTune { -1.0f };
    float lastCombDecay { -1.0f };
    float lastCombDamping { -1.0f };
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
