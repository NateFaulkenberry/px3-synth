#pragma once

#include <JuceHeader.h>

#include "../DSP/BreakpointEnvelope.h"
#include "UIConfig.h"

// The graphical breakpoint editor.
//
// Self-contained on purpose: it owns an envelope, edits it, and reports it. It
// knows nothing about parameters, voices or presets, which is what lets one
// implementation serve AMP ENV and ENV 1/2/3 without any of them being able to
// reach into another.
//
// The curve it draws is the same function the DSP evaluates - sampled from
// BreakpointEnvelope::shape - so there is no drawn shape and played shape to
// drift apart.
class BreakpointEnvelopeEditor final : public juce::Component
{
public:
    BreakpointEnvelopeEditor();

    void setUIConfig(std::shared_ptr<const UIConfig> configIn);
    void setAccentColour(juce::Colour accentIn);

    // Greyed out with the rest of the card when the envelope is bypassed, the
    // same way the card's own drawing was.
    void setEnvelopeEnabled(bool shouldBeEnabled);
    void setConfigPrefix(juce::String prefix);

    void setEnvelope(const px3::BreakpointEnvelope& envelope);
    const px3::BreakpointEnvelope& getEnvelope() const noexcept { return envelope; }

    // Fires on every edit, including during a drag: the caller decides whether
    // to write a parameter now or on mouse-up.
    std::function<void(const px3::BreakpointEnvelope&)> onEnvelopeChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // Exposed for the tests: what the mouse would grab at a point, and where a
    // given breakpoint has been drawn.
    enum class Target { none, point, curve };
    struct Hit
    {
        Target target { Target::none };
        int index { -1 };
    };
    // Named grabAt rather than hitTest: juce::Component::hitTest is virtual and
    // asks a different question - whether a point is inside the component at
    // all - so overloading across that boundary hides it.
    Hit grabAt(juce::Point<float> position) const;
    juce::Point<float> pointToScreen(int index) const;

    // Where a point is DRAWN, which is not always where it is. Two points at
    // the same time land on the same pixel - HOLD defaults to zero, so at INIT
    // the hold point sits exactly on the attack point - and one handle covering
    // another is one handle you cannot reach. See the definition.
    juce::Point<float> drawnPointPosition(int index) const;

    // What each breakpoint of a plain ADSR shape controls. Empty for points
    // whose role has no name, and for envelopes that have been edited past the
    // shape these names describe.
    juce::String roleLabelFor(int index) const;

private:
    juce::Colour curveColour() const;
    juce::Colour fillColour() const;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(double timeSeconds, double value) const;
    double screenToTime(float x) const;
    double screenToValue(float y) const;
    double visibleSeconds() const;

    void buildCurvePath(juce::Path& path) const;
    juce::Point<float> curveHandlePosition(int segment) const;

    juce::Colour colourFor(const juce::String& key, juce::Colour fallback) const;
    float floatFor(const juce::String& key, float fallback) const;
    float configFor(const juce::String& key, float fallback) const;
    juce::Colour configColour(const juce::String& key, juce::Colour fallback) const;

    void notifyChanged();

    px3::BreakpointEnvelope envelope;
    std::shared_ptr<const UIConfig> config;
    juce::String configPrefix { "mod.env1" };
    juce::Colour accent { juce::Colour::fromRGB(120, 186, 255) };
    bool envelopeEnabled { true };

    Hit hovered;
    Hit dragging;
    int selectedPoint { -1 };

    // Where the drag started, so a drag is relative to the grab point rather
    // than snapping the breakpoint to the cursor on the first pixel of movement.
    juce::Point<float> dragOrigin;
    double dragStartTime { 0.0 };
    double dragStartValue { 0.0 };
    double dragStartCurve { 0.0 };

    // The time axis at the moment a drag began. Held for the drag so the
    // curve does not rescale under the cursor as points move.
    double dragAxisSeconds { 0.0 };

    bool showReadout { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakpointEnvelopeEditor)
};
