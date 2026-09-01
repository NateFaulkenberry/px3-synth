#pragma once

#include <JuceHeader.h>

#include <limits>

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

    // How far the envelope being played has got, in its OWN seconds. Fed from
    // the DSP's runtime state, never from a timer of the editor's own - a
    // second clock would drift against the one making the sound.
    // Where the playing envelope has got to. The DSP's own position type, so
    // there is nothing to copy field by field and nothing to fall out of step.
    void setProgress(EnvelopePosition progress);
    EnvelopePosition getProgress() const noexcept { return liveProgress; }

    // Where the fill currently reaches, on the DISPLAY time axis. Exposed so a
    // test can check it against the curve rather than against a screenshot.
    double progressDisplayTime() const;

    // The drawn curve, optionally stopped part way and closed down to the
    // baseline - which is how the progress fill is made. One sampler serves
    // both, so the fill's upper edge IS the curve rather than a second
    // approximation of it that could drift out of agreement.
    void buildCurvePath(juce::Path& path,
                        double untilDisplayTime = std::numeric_limits<double>::max(),
                        bool closeToBaseline = false) const;

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
    // `sustain` is a handle with no point of its own. The sustain LEVEL and the
    // decay TIME are the same breakpoint's two coordinates, so one handle was
    // the only control for both - drag it sideways for decay, upwards for
    // sustain. That is one control doing two jobs, and it is why the label had
    // to read "DECAY / SUSTAIN".
    //
    // The editor now draws the held phase explicitly, as a flat stretch after
    // the decay, and puts the sustain handle on it. The breakpoint itself is
    // then the decay handle alone.
    // `sustain` is the bar on the held stretch: the sustain LEVEL.
    // `releaseStart` is the far end of that stretch: where the release begins.
    //
    // Neither has a breakpoint of its own. The sustain level and the decay time
    // are the two coordinates of one point, and the held stretch is drawn
    // rather than stored - so both need a handle the model does not provide.
    enum class Target { none, point, curve, sustain, releaseStart };
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

    // The sustain handle, on the held stretch. Only meaningful for an ADSR
    // skeleton, where a held phase is a thing that exists.
    juce::Point<float> sustainHandlePosition() const;
    juce::Point<float> releaseStartHandlePosition() const;
    bool hasSustainHandle() const;

    // What each breakpoint of a plain ADSR shape controls. Empty for points
    // whose role has no name, and for envelopes that have been edited past the
    // shape these names describe.
    juce::String roleLabelFor(int index) const;

    // How wide the held stretch is drawn. Display only: the envelope holds
    // there for as long as the key is down, which is not a duration the model
    // has or should have.
    double heldDisplaySeconds() const;

    // How wide the held stretch is, once the user has said. Negative means
    // "not yet said", and the width follows the envelope's own length.
    double heldSecondsOverride { -1.0 };

    // Where a point is drawn on the time axis, which is its real time plus the
    // held stretch once past the sustain point.
    double displayTimeArriving(int index) const;
    double displayTimeLeaving(int index) const;

private:
    juce::Colour curveColour() const;
    juce::Colour fillColour() const;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(double timeSeconds, double value) const;
    double screenToTime(float x) const;
    double screenToValue(float y) const;
    double visibleSeconds() const;

    // One sampler for both the drawn curve and the progress fill.
    //
    // `untilDisplayTime` stops the walk part way along; `closeToBaseline` drops
    // to the bottom of the plot and closes, turning the same points into an
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
    EnvelopePosition liveProgress;

    // Reused across paints. Path::clear keeps the storage, so the fill costs no
    // allocation per frame once the shape has been drawn once.
    juce::Path progressFillPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BreakpointEnvelopeEditor)
};
