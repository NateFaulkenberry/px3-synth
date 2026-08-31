#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "CardInner.h"

#include <memory>

class UIConfig;

#include "BreakpointEnvelopeEditor.h"

// Reusable interactive ADSR graph that binds directly to provided parameters.
class EnvelopeComponent final : public juce::Component
{
public:
    EnvelopeComponent(juce::AudioParameterFloat& attackIn,
                      juce::AudioParameterFloat& decayIn,
                      juce::AudioParameterFloat& sustainIn,
                      juce::AudioParameterFloat& releaseIn,
                      juce::AudioParameterBool& enabledIn,
                      juce::ToggleButton& enabledButtonIn,
                      juce::Label& assignLabelIn,
                      juce::ComboBox& assignBoxIn,
                      juce::Slider* amountKnobIn,
                      juce::Label* amountLabelIn,
                      juce::Label* amountValueLabelIn,
                      juce::Colour accentIn,
                      const juce::String& configPrefixIn = "mod.env1");

    void setAccentColour(juce::Colour accentIn);
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    // The card key and title this component draws under. They default to the
    // last segment of configPrefix - "env1", "ENV 1" - which is right for the
    // mod envelopes. AMP ENV overrides them, because there the graph is hosted
    // inside AmpEnvelopeComponent and "amp.env" would derive "env".
    void setCardIdentity(juce::String styleKey, juce::String title);

    // The parent panel content box: reference for percentage dimensions.
    void setPanelContentBounds(juce::Rectangle<int> panelContent);
    void refreshFromParameters();

    // For the layout tests: the frame paint() draws, and where the editor
    // actually sits. They have to be the same rectangle.
    juce::Rectangle<int> debugGraphFrameBounds() const;
    juce::Rectangle<int> debugEditorBounds() const { return breakpointEditor.getBounds(); }
    // The editor itself, so a test can ask what the USER is looking at rather
    // than what the processor would have built.
    const BreakpointEnvelopeEditor& debugEditor() const { return breakpointEditor; }

    // The shape this card is editing. Handed in by the editor, which owns the
    // connection to the processor - this component knows about an envelope, not
    // about where it lives.
    void setShapedEnvelope(const px3::BreakpointEnvelope& envelope);
    std::function<void(const px3::BreakpointEnvelope&)> onEnvelopeEdited;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    BreakpointEnvelopeEditor breakpointEditor;

    enum class DragHandle
    {
        none,
        attack,
        decaySustain,
        release
    };

    struct Geometry
    {
        float left { 0.0f };
        float right { 0.0f };
        float top { 0.0f };
        float bottom { 0.0f };
        float attackRangeWidth { 1.0f };
        float releaseRangeWidth { 1.0f };
        float minDecayGap { 1.0f };
        float minSustainWidth { 1.0f };
        juce::Point<float> start;
        juce::Point<float> attackPoint;
        juce::Point<float> decaySustainPoint;
        juce::Point<float> releasePoint;
        juce::Point<float> end;
    };

    static float clamp01(float v);
    static float timeToVisualNorm(float seconds, float minValue, float maxValue);
    static float visualNormToTime(float norm, float minValue, float maxValue);
    bool isFullHeightGraph() const;
    // AMP ENV's card background does nothing - it is declared alwaysEnabled, so
    // there is no bypass to toggle - and a pointer over dead space is a lie.
    // Only its graph gets the pointer. The mod envelopes toggle on a background
    // click, so their whole card gets it.
    void updateCursorFor(juce::Point<float> position);
    void layoutCardInner();
    Geometry computeGeometry() const;
    static float distSq(juce::Point<float> a, juce::Point<float> b);
    static float distToSegmentSq(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b);
    DragHandle pickHandle(juce::Point<float> p, const Geometry& geom) const;
    juce::Point<float> handlePositionFor(DragHandle handle, const Geometry& geom) const;
    void drawHandleMarker(juce::Graphics& g, juce::Point<float> center, DragHandle handle) const;
    void drawHandleLabel(juce::Graphics& g,
                         juce::Point<float> center,
                         DragHandle handle,
                         const juce::String& id) const;
    juce::String valueTextForHandle(DragHandle handle) const;
    void setParameterFromActualValue(juce::AudioParameterFloat& parameter, float value);
    void applyDragPosition(juce::Point<float> mousePos, const Geometry& geom);

    juce::AudioParameterFloat& attack;
    juce::AudioParameterFloat& decay;
    juce::AudioParameterFloat& sustain;
    juce::AudioParameterFloat& release;
    juce::AudioParameterBool& enabled;
    juce::ToggleButton& enabledButton;
    juce::Label& assignLabel;
    juce::ComboBox& assignBox;
    juce::Slider* amountKnob { nullptr };
    juce::Label* amountLabel { nullptr };
    juce::Label* amountValueLabel { nullptr };
    juce::Colour baseAmountValueTextColour;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;
    DragHandle hoverHandle { DragHandle::none };
    DragHandle dragHandle { DragHandle::none };
    bool draggingSustainSegment { false };
    float sustainDragStartX { 0.0f };
    float sustainDragStartDecayX { 0.0f };
    float sustainDragStartReleaseX { 0.0f };
    float lastAttack { -1.0f };
    float lastDecay { -1.0f };
    float lastSustain { -1.0f };
    float lastRelease { -1.0f };
    bool currentEnabled { true };
    juce::String configPrefix;
    juce::String cardStyleKey;
    juce::String cardTitle;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
};
