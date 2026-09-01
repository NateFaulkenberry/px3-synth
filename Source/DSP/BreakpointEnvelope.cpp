#include "BreakpointEnvelope.h"

#include <algorithm>
#include <cmath>

namespace px3
{
namespace
{
double clampValue(double v)
{
    // Also catches NaN: a comparison against NaN is false, so the fallback is
    // taken rather than propagated.
    return std::isfinite(v) ? juce::jlimit(0.0, 1.0, v) : 0.0;
}

double clampTime(double t)
{
    return std::isfinite(t) ? juce::jmax(0.0, t) : 0.0;
}

double clampCurve(double c)
{
    return std::isfinite(c) ? juce::jlimit(-1.0, 1.0, c) : 0.0;
}
} // namespace

BreakpointEnvelope::BreakpointEnvelope()
{
    // Written out rather than delegating to fromAdsr, which constructs one of
    // these and would call straight back into here - infinite recursion, and a
    // stack overflow the moment anything made an envelope.
    const EnvelopeSettings defaults;
    const auto attack = static_cast<double>(defaults.attackSeconds);
    const auto decay = static_cast<double>(defaults.decaySeconds);

    pointCount = 4;
    points[0] = { 0.0, 0.0, 0.0 };
    points[1] = { attack, 1.0, 0.0 };
    points[2] = { attack + decay, defaults.sustainLevel, 0.0 };
    points[3] = { attack + decay + defaults.releaseSeconds, 0.0, 0.0 };
    sustainPoint = 2;
}

BreakpointEnvelope BreakpointEnvelope::fromAdsr(const EnvelopeSettings& settings)
{
    const auto attack = clampTime(settings.attackSeconds);
    const auto decay = clampTime(settings.decaySeconds);
    const auto sustain = clampValue(settings.sustainLevel);
    const auto release = clampTime(settings.releaseSeconds);

    // The four points of section C: start, peak, sustain, end.
    //
    // A hold stage was added between the attack and the decay and then removed
    // from both envelopes. It defaulted to zero length, which put a fifth
    // handle exactly on top of the attack handle, and it meant two skeletons
    // existed - so every piece of code touching an envelope had to know which
    // one it had. Neither cost bought anything.
    BreakpointEnvelope envelope;
    envelope.pointCount = 4;
    envelope.points[0] = { 0.0, 0.0, 0.0 };
    envelope.points[1] = { attack, 1.0, 0.0 };
    envelope.points[2] = { attack + decay, sustain, 0.0 };
    envelope.points[3] = { attack + decay + release, 0.0, 0.0 };
    envelope.sustainPoint = 2;
    return envelope;
}

EnvelopeSettings BreakpointEnvelope::toAdsr() const
{
    EnvelopeSettings settings;

    if (pointCount != 4 || sustainPoint != 2)
    {
        return settings;
    }

    settings.attackSeconds = static_cast<float>(points[1].timeSeconds - points[0].timeSeconds);
    settings.decaySeconds = static_cast<float>(points[2].timeSeconds - points[1].timeSeconds);
    settings.sustainLevel = static_cast<float>(points[2].value);
    settings.releaseSeconds = static_cast<float>(points[3].timeSeconds - points[2].timeSeconds);
    return settings;
}

bool BreakpointEnvelope::isPlainAdsr() const
{
    if (pointCount != 4 || sustainPoint != 2)
    {
        return false;
    }

    for (int i = 0; i < pointCount; ++i)
    {
        if (std::abs(points[static_cast<std::size_t>(i)].curveToNext) > 1.0e-9)
        {
            return false;
        }
    }

    return points[0].timeSeconds <= 1.0e-9
        && points[0].value <= 1.0e-9
        && points[1].value >= 1.0 - 1.0e-9
        && points[static_cast<std::size_t>(pointCount - 1)].value <= 1.0e-9;
}

const BreakpointEnvelope::Point& BreakpointEnvelope::getPoint(int index) const noexcept
{
    return points[static_cast<std::size_t>(juce::jlimit(0, juce::jmax(0, pointCount - 1), index))];
}

void BreakpointEnvelope::sortAndClamp()
{
    pointCount = juce::jlimit(kMinPoints, kMaxPoints, pointCount);

    std::stable_sort(points.begin(), points.begin() + pointCount,
                     [](const Point& a, const Point& b) { return a.timeSeconds < b.timeSeconds; });

    // The first point anchors the envelope at time zero. Everything after it is
    // held non-decreasing, so a point can be dragged onto its neighbour - a
    // zero-length segment is a legitimate instant jump - but never past it.
    points[0].timeSeconds = 0.0;
    for (int i = 1; i < pointCount; ++i)
    {
        auto& point = points[static_cast<std::size_t>(i)];
        point.timeSeconds = juce::jmax(clampTime(point.timeSeconds),
                                       points[static_cast<std::size_t>(i - 1)].timeSeconds);
    }

    for (int i = 0; i < pointCount; ++i)
    {
        auto& point = points[static_cast<std::size_t>(i)];
        point.value = clampValue(point.value);
        point.curveToNext = clampCurve(point.curveToNext);
    }

    // A sustain on the last point would leave nothing to release through.
    sustainPoint = juce::jlimit(0, pointCount - 2, sustainPoint);

    anchorEnds();
}

void BreakpointEnvelope::anchorEnds()
{
    // The envelope begins and ends at silence. Both are structural rather than
    // editable: the first point is the note-on instant, and the last is where
    // the release has finished.
    //
    // Free-form editing is what got past this. Adding a point unlocks every
    // point's LEVEL, so the ends could be dragged up; removing the added point
    // put the ADSR skeleton back carrying levels the skeleton could never have
    // been given directly. On screen that is a curve that has come away from
    // the bottom of its own graph. In the DSP it is a click at note-on, and a
    // note that never reaches silence.
    points[0].timeSeconds = 0.0;
    points[0].value = 0.0;
    points[static_cast<std::size_t>(pointCount - 1)].value = 0.0;
}

void BreakpointEnvelope::setPoint(int index, double timeSeconds, double value)
{
    if (index < 0 || index >= pointCount)
    {
        return;
    }

    auto& point = points[static_cast<std::size_t>(index)];
    point.value = clampValue(value);

    // The first point stays at zero: it is the note-on instant, not a stage.
    point.timeSeconds = index == 0 ? 0.0 : clampTime(timeSeconds);

    // Held between its neighbours rather than re-sorted, so dragging a point
    // past another does not silently renumber every point under the mouse.
    if (index > 0)
    {
        point.timeSeconds = juce::jmax(point.timeSeconds,
                                       points[static_cast<std::size_t>(index - 1)].timeSeconds);
    }
    if (index + 1 < pointCount)
    {
        point.timeSeconds = juce::jmin(point.timeSeconds,
                                       points[static_cast<std::size_t>(index + 1)].timeSeconds);
    }

    // Deliberately not sortAndClamp(): re-sorting here would renumber points
    // under the mouse mid-drag. The ends still have to hold.
    anchorEnds();
}

void BreakpointEnvelope::setCurve(int index, double curve)
{
    if (index < 0 || index >= pointCount)
    {
        return;
    }
    points[static_cast<std::size_t>(index)].curveToNext = clampCurve(curve);
}

void BreakpointEnvelope::setPoints(const Point* newPoints, int count, int newSustainPoint)
{
    if (newPoints == nullptr || count < kMinPoints)
    {
        return;
    }

    pointCount = juce::jmin(count, kMaxPoints);
    for (int i = 0; i < pointCount; ++i)
    {
        points[static_cast<std::size_t>(i)] = newPoints[i];
    }

    sustainPoint = newSustainPoint;
    sortAndClamp();
}

int BreakpointEnvelope::addPoint(double timeSeconds, double value)
{
    if (pointCount >= kMaxPoints)
    {
        return -1;
    }

    const auto time = clampTime(timeSeconds);

    // Where it lands in time, which is what splits a segment rather than
    // appending to the end.
    auto insertAt = pointCount;
    for (int i = 0; i < pointCount; ++i)
    {
        if (points[static_cast<std::size_t>(i)].timeSeconds > time)
        {
            insertAt = i;
            break;
        }
    }
    insertAt = juce::jmax(1, insertAt);   // never before the anchor

    // The curve of the segment being split carries to BOTH halves, so inserting
    // a point on a bent segment leaves it looking as it did rather than
    // straightening half of it.
    const auto inheritedCurve = insertAt > 0
                                  ? points[static_cast<std::size_t>(insertAt - 1)].curveToNext
                                  : 0.0;

    for (int i = pointCount; i > insertAt; --i)
    {
        points[static_cast<std::size_t>(i)] = points[static_cast<std::size_t>(i - 1)];
    }

    points[static_cast<std::size_t>(insertAt)] = { time, clampValue(value), inheritedCurve };
    ++pointCount;

    if (sustainPoint >= insertAt)
    {
        ++sustainPoint;   // the sustain follows the point it was on
    }

    sortAndClamp();
    return insertAt;
}

bool BreakpointEnvelope::canRemovePoint(int index) const
{
    if (index <= 0 || index >= pointCount)
    {
        return false;   // the anchor is structural
    }
    if (index == pointCount - 1)
    {
        return false;   // so is the end
    }
    if (pointCount <= kMinPoints + 1)
    {
        return false;
    }
    return index != sustainPoint;   // and so is the point the envelope holds at
}

bool BreakpointEnvelope::removePoint(int index)
{
    if (! canRemovePoint(index))
    {
        return false;
    }

    for (int i = index; i + 1 < pointCount; ++i)
    {
        points[static_cast<std::size_t>(i)] = points[static_cast<std::size_t>(i + 1)];
    }
    --pointCount;

    if (sustainPoint > index)
    {
        --sustainPoint;
    }

    sortAndClamp();
    return true;
}

double BreakpointEnvelope::getTotalSeconds() const
{
    return pointCount > 0 ? points[static_cast<std::size_t>(pointCount - 1)].timeSeconds : 0.0;
}

double BreakpointEnvelope::valueAt(double timeSeconds) const
{
    if (pointCount <= 0)
    {
        return 0.0;
    }

    const auto time = clampTime(timeSeconds);
    if (time <= points[0].timeSeconds)
    {
        return points[0].value;
    }

    for (int i = 0; i + 1 < pointCount; ++i)
    {
        const auto& a = points[static_cast<std::size_t>(i)];
        const auto& b = points[static_cast<std::size_t>(i + 1)];
        if (time >= b.timeSeconds)
        {
            continue;
        }

        const auto duration = b.timeSeconds - a.timeSeconds;
        if (duration <= 1.0e-12)
        {
            return b.value;   // an instant jump is a legitimate shape
        }

        const auto x = (time - a.timeSeconds) / duration;
        return a.value + (b.value - a.value) * shape(x, a.curveToNext);
    }

    return points[static_cast<std::size_t>(pointCount - 1)].value;
}

//==============================================================================

void BreakpointEnvelope::Snapshot::rebuild(const BreakpointEnvelope& envelope, double)
{
    segmentCount = juce::jmax(0, envelope.getPointCount() - 1);
    sustainSegment = juce::jlimit(0, juce::jmax(0, segmentCount - 1), envelope.getSustainPoint());

    for (int i = 0; i < segmentCount; ++i)
    {
        const auto& a = envelope.getPoint(i);
        const auto& b = envelope.getPoint(i + 1);
        const auto duration = b.timeSeconds - a.timeSeconds;

        auto& segment = segments[static_cast<std::size_t>(i)];
        segment.startTime = a.timeSeconds;
        // The reciprocal is taken here, once, rather than as a divide per
        // sample per voice. Zero marks a zero-length segment, which is a jump.
        segment.invDuration = duration > 1.0e-12 ? 1.0 / duration : 0.0;
        segment.startValue = a.value;
        segment.valueSpan = b.value - a.value;
        segment.curve = a.curveToNext;
    }

    const auto sustainIndex = envelope.getSustainPoint();
    sustainSeconds = envelope.getPoint(sustainIndex).timeSeconds;
    sustainValue = static_cast<float>(envelope.getPoint(sustainIndex).value);
    releaseSeconds = juce::jmax(0.0, envelope.getTotalSeconds() - sustainSeconds);
}

double BreakpointEnvelope::Snapshot::evaluate(int first, int last, double seconds,
                                              double fallbackValue) const noexcept
{
    for (int i = first; i <= last && i < segmentCount; ++i)
    {
        const auto& segment = segments[static_cast<std::size_t>(i)];
        const auto local = seconds - segment.startTime;
        if (local < 0.0)
        {
            return segment.startValue;
        }

        if (segment.invDuration <= 0.0)
        {
            continue;   // zero-length: nothing spends time here
        }

        const auto x = local * segment.invDuration;
        if (x >= 1.0)
        {
            continue;
        }

        return segment.startValue + segment.valueSpan * shape(x, segment.curve);
    }

    return fallbackValue;
}

double BreakpointEnvelope::Snapshot::firstSegmentEnd() const noexcept
{
    if (segmentCount <= 0) { return 0.0; }

    const auto& first = segments[0];
    return first.invDuration > 0.0 ? first.startTime + 1.0 / first.invDuration
                                   : first.startTime;
}

float BreakpointEnvelope::Snapshot::valueAtHeld(double seconds, float fromValue) const noexcept
{
    const auto plain = valueAtHeld(seconds);

    const auto attackEnd = firstSegmentEnd();
    if (fromValue <= 1.0e-6f || attackEnd <= 0.0 || seconds >= attackEnd)
    {
        return plain;
    }

    // The attack, rescaled to run from `fromValue` to the peak instead of from
    // zero to the peak. At the end of the segment the two agree exactly, so
    // the lift disappears at the attack's own corner and nothing downstream of
    // it moves.
    const auto peak = static_cast<double>(segments[0].startValue + segments[0].valueSpan);
    const auto span = peak - static_cast<double>(fromValue);
    const auto reach = peak - segments[0].startValue;
    if (std::abs(reach) < 1.0e-12)
    {
        return plain;
    }

    const auto travelled = (static_cast<double>(plain) - segments[0].startValue) / reach;
    return static_cast<float>(static_cast<double>(fromValue) + travelled * span);
}

float BreakpointEnvelope::Snapshot::valueAtHeld(double seconds) const noexcept
{
    if (segmentCount <= 0)
    {
        return 0.0f;
    }

    if (seconds >= sustainSeconds)
    {
        return sustainValue;   // held here until the key is released
    }

    return static_cast<float>(evaluate(0, sustainSegment - 1, seconds, sustainValue));
}

float BreakpointEnvelope::Snapshot::valueAtReleased(double seconds, float fromValue) const noexcept
{
    if (segmentCount <= 0 || releaseSeconds <= 0.0)
    {
        return 0.0f;
    }

    if (seconds >= releaseSeconds)
    {
        return static_cast<float>(segments[static_cast<std::size_t>(segmentCount - 1)].startValue
                                  + segments[static_cast<std::size_t>(segmentCount - 1)].valueSpan);
    }

    const auto raw = evaluate(sustainSegment, segmentCount - 1,
                              seconds + sustainSeconds, 0.0);

    // The release runs its own shape, then is offset so it STARTS from where the
    // envelope actually was. Releasing during a slow attack must not jump to the
    // sustain level on the way out.
    //
    // The offset fades out across the release rather than being applied flat, so
    // the envelope still arrives exactly at its final value.
    const auto offset = static_cast<double>(fromValue) - sustainValue;
    const auto remaining = 1.0 - juce::jlimit(0.0, 1.0, seconds / releaseSeconds);
    return static_cast<float>(juce::jlimit(0.0, 1.0, raw + offset * remaining));
}

float BreakpointEnvelope::Snapshot::releaseProgress(double seconds) const noexcept
{
    if (releaseSeconds <= 0.0)
    {
        return 1.0f;
    }
    return static_cast<float>(juce::jlimit(0.0, 1.0, seconds / releaseSeconds));
}

BreakpointEnvelope withoutHoldStage(const BreakpointEnvelope& envelope)
{
    // The five-point AHDSR skeleton: point 2 ends the hold, and the envelope
    // holds at point 3. Removing point 2 leaves attack, decay, sustain and
    // release, with the sustain index moving down with it.
    if (envelope.getPointCount() != 5 || envelope.getSustainPoint() != 3)
    {
        return envelope;
    }

    BreakpointEnvelope::Point points[4];
    points[0] = envelope.getPoint(0);
    points[1] = envelope.getPoint(1);
    points[2] = envelope.getPoint(3);
    points[3] = envelope.getPoint(4);

    // The attack keeps its own curve; the hold segment's is dropped with the
    // hold, and the decay is the segment that used to leave point 2.
    points[1].curveToNext = envelope.getPoint(2).curveToNext;

    BreakpointEnvelope result;
    result.setPoints(points, 4, 2);
    return result;
}

} // namespace px3
