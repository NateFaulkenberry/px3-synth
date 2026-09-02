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

    // Live means the knob accepts input: enabled AND taking clicks. Checking
    // isEnabled alone would miss a knob left interactive while drawn grey.
    bool debugAdsrKnobsLive() const
    {
        if (! adsrKnobsBuilt) { return false; }
        for (const auto& entry : adsrKnobs)
        {
            auto takesClicks = false;
            auto takesChildClicks = false;
            entry.knob.getInterceptsMouseClicks(takesClicks, takesChildClicks);

            if (! entry.knob.isEnabled() || ! takesClicks)
            {
                return false;
            }
        }
        return true;
    }

    const juce::Label& debugAdsrKnobReadout(int i) const
    { return adsrKnobs[static_cast<std::size_t>(juce::jlimit(0, 3, i))].readout; }

    // Everything the ADSR group occupies - labels, knobs and readouts - as one
    // rectangle, so a test can measure the gap between it and the TYPE
    // selector rather than recompute the layout's own arithmetic.
    // The row the four knobs are laid out in, so a test can ask what share of
    // it they take without recomputing the layout.
    juce::Rectangle<int> debugAdsrRowBounds() const
    { return inner.rowContent(juce::jmax(0, inner.rowCount() - 1)); }

    juce::Rectangle<int> debugAdsrGroupBounds() const
    {
        if (! adsrKnobsBuilt) { return {}; }

        juce::Rectangle<int> group;
        for (const auto& entry : adsrKnobs)
        {
            group = group.getUnion(entry.label.getBounds())
                         .getUnion(entry.knob.getBounds())
                         .getUnion(entry.readout.getBounds());
        }
        return group;
    }

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

    // Drop the TYPE selector and stay in ADSR. Asked for in code by whoever
    // owns the card rather than read from UIConfig, for the same reason the
    // knob row is: it changes what the bottom row contains, and a card whose
    // layout depends on a file that may not have loaded yet lays itself out
    // differently depending on timing.
    void setAdsrOnly(bool shouldBeAdsrOnly);
    bool isAdsrOnly() const { return adsrOnly; }
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
    void mouseUp(const juce::MouseEvent& event) override;

private:
    BreakpointEnvelopeEditor breakpointEditor;

    // Just the graph's rectangle. It used to carry the positions of an
    // A / D-S / R handle set as well, for a second ADSR editor this card ran
    // itself; BreakpointEnvelopeEditor replaced that in both modes.
    struct Geometry
    {
        float left { 0.0f };
        float right { 0.0f };
        float top { 0.0f };
        float bottom { 0.0f };
    };

    bool isFullHeightGraph() const;
    // AMP ENV's card background does nothing - it is declared alwaysEnabled, so
    // there is no bypass to toggle - and a pointer over dead space is a lie.
    // Only its graph gets the pointer. The mod envelopes toggle on a background
    // click, so their whole card gets it.
    void updateCursorFor(juce::Point<float> position);
    void layoutCardInner();
    Geometry computeGeometry() const;

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
    // The one place that decides whether the four knobs are live: bypass AND
    // mode, together. See the comment on the definition.
    void applyAdsrKnobState();
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
    float lastAttack { -1.0f };
    float lastDecay { -1.0f };
    float lastSustain { -1.0f };
    float lastRelease { -1.0f };
    bool currentEnabled { true };
    bool adsrOnly { false };
    juce::String configPrefix;
    juce::String cardStyleKey;
    juce::String cardTitle;
    px3::ui::CardHost card;
    px3::ui::CardInner inner;
};
