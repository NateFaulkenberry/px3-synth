#pragma once

#include <JuceHeader.h>

#include "EnvelopeTypes.h"

#include <array>

namespace px3
{

// A multi-stage envelope described by the points it passes through.
//
// ADSR is a special case of this, not a separate thing: four points, the third
// of them marked as the sustain. That is why the existing attack/decay/sustain/
// release parameters can keep describing the envelope and keep being automated
// while the model underneath them gains points and curves.
//
// One representation, shared by the editor and the DSP. The editor draws the
// same function the audio thread evaluates - there is no drawn curve and
// evaluated curve to drift apart.
class BreakpointEnvelope
{
public:
    // Sixteen is well past what an envelope stays legible at, and small enough
    // that a snapshot is a cheap copy rather than an allocation.
    static constexpr int kMaxPoints = 16;
    static constexpr int kMinPoints = 2;

    // How far the curve amount bends a segment. exp(-3) to exp(3) is a range of
    // 20:1 either way, which reaches a convincing exponential without ever
    // approaching the step function a power curve degenerates into.
    static constexpr double kCurveRange = 3.0;

    struct Point
    {
        double timeSeconds { 0.0 };
        double value { 0.0 };

        // How the segment LEAVING this point bends, -1..+1. Zero is exactly
        // linear. Ignored on the final point, which has no segment after it.
        double curveToNext { 0.0 };
    };

    BreakpointEnvelope();

    // The ADSR the four existing parameters describe. This is the default and
    // the migration path both: an old preset has no stored points, so it
    // becomes exactly this and sounds as it always did.
    static BreakpointEnvelope fromAdsr(const EnvelopeSettings& settings);

    // The same shape without the hold stage: four points, sustain at index 2.
    // AMP ENV uses this; the modulation envelopes keep the five-point form.
    static BreakpointEnvelope fromAdsrWithoutHold(const EnvelopeSettings& settings);

    // The reverse projection, so dragging a point can write the parameters back.
    // Only meaningful for an envelope that still has the ADSR shape.
    EnvelopeSettings toAdsr() const;

    // True when this is still four points in the ADSR arrangement, and so is
    // fully described by the four parameters.
    bool isPlainAdsr() const;

    int getPointCount() const noexcept { return pointCount; }
    const Point& getPoint(int index) const noexcept;
    int getSustainPoint() const noexcept { return sustainPoint; }

    // Every mutator keeps the invariants: points ordered by time, values in
    // range, sustain index valid. A caller cannot put the envelope into a state
    // the DSP has to defend against, which is why the DSP does not.
    void setPoint(int index, double timeSeconds, double value);
    void setCurve(int index, double curve);

    // Replaces every point at once.
    //
    // Deserialization needs this: building a stored envelope by removing the
    // default points and adding the stored ones does not work, because the
    // mutators correctly refuse to remove the anchor, the end and the sustain -
    // which leaves the defaults mixed in with what was loaded.
    void setPoints(const Point* newPoints, int count, int newSustainPoint);

    // Returns the index of the new point, or -1 if it could not be added. The
    // point is inserted in time order and SPLITS the segment it lands in rather
    // than being appended.
    int addPoint(double timeSeconds, double value);

    // Returns true if it was removed. The first point, the last point and the
    // sustain point are structural: removing them would leave an envelope that
    // cannot be evaluated or cannot be held.
    bool removePoint(int index);

    bool canRemovePoint(int index) const;

    double getTotalSeconds() const;

    // Where the envelope is at a given time, ignoring note-off. Used by the
    // editor for drawing and by the tests; the audio thread uses Snapshot.
    double valueAt(double timeSeconds) const;

    // One segment's curve, on its own, for a normalised position 0..1.
    //
    // y = x / (x + r(1-x)) with r = exp(-kCurveRange * curve). Monotone and
    // bounded for any r > 0, exactly linear at curve 0, and symmetric: +c and
    // -c are mirror images through the diagonal, which is what makes one
    // normalised control feel like one control.
    static double shape(double x, double curve) noexcept
    {
        if (curve > -1.0e-9 && curve < 1.0e-9)
        {
            return x;   // exactly linear, and no exp() for the common case
        }
        const auto r = std::exp(-kCurveRange * curve);
        const auto denominator = x + r * (1.0 - x);
        return denominator > 1.0e-12 ? x / denominator : x;
    }

    // What the audio thread evaluates: the same envelope with every division
    // already done.
    //
    // Fixed capacity rather than a vector so a voice can hold one by value and
    // a block can copy one without allocating.
    class Snapshot
    {
    public:
        void rebuild(const BreakpointEnvelope& envelope, double sampleRate);

        // Audio thread. `seconds` is time since note-on for the held phase, or
        // since note-off for the release phase.
        float valueAtHeld(double seconds) const noexcept;

        // The release phase, offset so it begins at `fromValue` rather than at
        // the sustain point.
        //
        // Releasing during the attack of a slow envelope must not jump to the
        // sustain level first: that is a click, and it is the usual way a
        // multi-stage envelope sounds broken.
        float valueAtReleased(double seconds, float fromValue) const noexcept;


        // 0 at note-off, 1 at the end of the release. Kept because
        // release-dependent processing schedules off it deliberately, so its
        // timing does not move when the envelope's curve changes.
        float releaseProgress(double seconds) const noexcept;


    private:
        struct Segment
        {
            double startTime { 0.0 };
            double invDuration { 0.0 };   // 0 for a zero-length segment
            double startValue { 0.0 };
            double valueSpan { 0.0 };
            double curve { 0.0 };
        };

        double evaluate(int first, int last, double seconds,
                        double fallbackValue) const noexcept;

        std::array<Segment, kMaxPoints> segments {};
        int segmentCount { 0 };
        int sustainSegment { 0 };
        double sustainSeconds { 0.0 };
        double releaseSeconds { 0.0 };
        float sustainValue { 0.0f };
    };

private:
    void sortAndClamp();

    std::array<Point, kMaxPoints> points {};
    int pointCount { 0 };
    int sustainPoint { 0 };
};

// Any envelope, with the hold stage taken out.
//
// The AMP ENV slot must never hold one, and "is this a plain ADSR" is not a
// safe test for that: the moment a point has been dragged the shape stops
// answering yes, while still carrying the extra breakpoint. So this asks only
// whether the AHDSR skeleton is present and collapses it if so, whatever the
// values are. Anything else is returned untouched - a genuinely free-form
// envelope has no hold stage to identify.
BreakpointEnvelope withoutHoldStage(const BreakpointEnvelope& envelope);

} // namespace px3
