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
    void setConfigPrefix(juce::String prefix);

    void setEnvelope(const px3::BreakpointEnvelope& envelope);
    const px3::BreakpointEnvelope& getEnvelope() const noexcept { return envelope; }

    // Fires on every edit, including during a drag: the caller decides whether
    // to write a parameter now or on mouse-up.
    std::function<void(const px3::BreakpointEnvelope&)> onEnvelopeChanged;

    // Where the envelope currently is, drawn as a moving marker. -1 hides it.
    void setLivePosition(float normalisedTime);

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

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(double timeSeconds, double value) const;
    double screenToTime(float x) const;
    double screenToValue(float y) const;
    double visibleSeconds() const;

    void buildCurvePath(juce::Path& path) const;
    juce::Point<float> curveHandlePosition(int segment) const;

    juce::Colour colourFor(const juce::String& key, juce::Colour fallback) const;
    float floatFor(const juce::String& key, float fallback) const;

    void notifyChanged();

    px3::BreakpointEnvelope envelope;
    std::shared_ptr<const UIConfig> config;
    juce::String configPrefix { "mod.env1" };
    juce::Colour accent { juce::Colour::fromRGB(120, 186, 255) };

    Hit hovered;
    Hit dragging;
    int selectedPoint { -1 };

    // Where the drag started, so a drag is relative to the grab point rather
    // than snapping the breakpoint to the cursor on the first pixel of movement.
    juce::Point<float> dragOrigin;
    double dragStartTime { 0.0 };
    double dragStartValue { 0.0 };
    double dragStartCurve { 0.0 };

    float livePosition { -1.0f };
    bool showReadout { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakpointEnvelopeEditor)
};
