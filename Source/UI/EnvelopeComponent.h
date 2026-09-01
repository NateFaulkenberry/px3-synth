#pragma once

#include <JuceHeader.h>

#include "Card.h"
#include "ChipLabel.h"
#include "CardInner.h"

#include <array>
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

    // The knobs are drawn by a look-and-feel this component does not own, so
    // they let go of it before it can outlive them.
    ~EnvelopeComponent() override;

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

    // The knob row under the graph, for the tests: how many were built, where
    // they sit, and what they read.
    int debugAdsrKnobCount() const { return adsrKnobsBuilt ? 4 : 0; }
    const juce::Slider& debugAdsrKnob(int i) const
    { return adsrKnobs[static_cast<std::size_t>(juce::jlimit(0, 3, i))].knob; }
    juce::String debugAdsrKnobName(int i) const
    { return adsrKnobs[static_cast<std::size_t>(juce::jlimit(0, 3, i))].label.getText(); }
    const juce::Label& debugAdsrKnobLabel(int i) const
    { return adsrKnobs[static_cast<std::size_t>(juce::jlimit(0, 3, i))].label; }
    // For the tests: the mode selector, and whether the ADSR knobs are showing.
    juce::ComboBox& debugModeBox() { return modeBox; }
    bool debugAdsrKnobsVisible() const
    { return adsrKnobsBuilt && adsrKnobs[0].knob.isVisible(); }

    const juce::Label& debugAdsrKnobReadout(int i) const
    { return adsrKnobs[static_cast<std::size_t>(juce::jlimit(0, 3, i))].readout; }

    // The shape this card is editing. Handed in by the editor, which owns the
    // connection to the processor - this component knows about an envelope, not
    // about where it lives.
    void setShapedEnvelope(const px3::BreakpointEnvelope& envelope);

    // ATTACK | DECAY | SUSTAIN | RELEASE below the graph, bound to the same
    // four parameters it edits. Off unless the owning card asks.
    void setAdsrKnobsVisible(bool shouldShow);

    // The envelope mode selector. The card owns the box; the owner supplies
    // what happens when it changes, because only the owner knows which slot
    // this card is editing.
    void setEnvelopeMode(px3::BreakpointEnvelope::Mode mode);
    std::function<void(px3::BreakpointEnvelope::Mode)> onEnvelopeModeChanged;

    // The editor's shared rotary look-and-feel. Handed down rather than
    // constructed here: every knob in the plugin is drawn by ONE of these, and
    // a second instance is a second set of colours to keep in step.
    void setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel);

    void setEnvelopeProgress(EnvelopePosition progress)
    {
        breakpointEditor.setProgress(progress);
    }
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
    // ATTACK | DECAY | SUSTAIN | RELEASE, under the graph. Owned here rather
    // than handed in like the amount knob, because both cards want the same
    // four and neither has anywhere else to put them. Built only when the
    // card's config asks for them: see adsrKnobsWanted().
    struct AdsrKnob
    {
        juce::Slider knob;
        px3::ui::ChipLabel label;
        juce::Label readout;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };
    std::array<AdsrKnob, 4> adsrKnobs;
    bool adsrKnobsBuilt { false };
    bool showAdsrKnobs { false };
    juce::LookAndFeel* knobLookAndFeel { nullptr };

    bool adsrKnobsWanted() const;
    void buildAdsrKnobs();
    void layoutAdsrKnobs();
    void refreshAdsrReadouts();

    // Which cardInner row holds what. The graph is the last row when there are
    // no knobs and the second to last when there are; deriving it in one place
    // is what keeps the drawn graph and the draggable graph on one rectangle.
    int graphRowIndex() const;

    // Where the editor sits: its row, less a gap at the bottom when the knob
    // row is below it.
    juce::Rectangle<int> graphBounds() const;

    // ADSR | Breakpoint. Styled through the shared combo style, like the
    // assignment boxes beside it.
    juce::ComboBox modeBox;
    px3::ui::ChipLabel modeLabel;
    px3::BreakpointEnvelope::Mode envelopeMode { px3::BreakpointEnvelope::Mode::adsr };
    void applyModeToControls();
    void layoutModeSelector();

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
