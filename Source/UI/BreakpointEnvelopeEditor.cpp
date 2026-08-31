#include "BreakpointEnvelopeEditor.h"

namespace
{
// The grab radius, which is deliberately larger than the dot that is drawn. A
// hit area the size of its own graphic is what makes an editor feel fussy - the
// user aims at something they can see and misses something they cannot.
constexpr float kPointGrabRadius = 11.0f;
constexpr float kCurveGrabRadius = 9.0f;

constexpr double kMinimumVisibleSeconds = 0.05;
} // namespace

BreakpointEnvelopeEditor::BreakpointEnvelopeEditor()
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void BreakpointEnvelopeEditor::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    config = std::move(configIn);
    repaint();
}

void BreakpointEnvelopeEditor::setAccentColour(juce::Colour accentIn)
{
    if (accent == accentIn) { return; }
    accent = accentIn;
    repaint();
}

void BreakpointEnvelopeEditor::setConfigPrefix(juce::String prefix)
{
    configPrefix = std::move(prefix);
    repaint();
}

void BreakpointEnvelopeEditor::setEnvelope(const px3::BreakpointEnvelope& newEnvelope)
{
    envelope = newEnvelope;
    selectedPoint = juce::jlimit(-1, envelope.getPointCount() - 1, selectedPoint);
    repaint();
}

void BreakpointEnvelopeEditor::setLivePosition(float normalisedTime)
{
    const auto clamped = normalisedTime < 0.0f ? -1.0f : juce::jlimit(0.0f, 1.0f, normalisedTime);
    if (std::abs(clamped - livePosition) < 0.002f) { return; }
    livePosition = clamped;
    repaint();
}

void BreakpointEnvelopeEditor::setEnvelopeEnabled(bool shouldBeEnabled)
{
    if (envelopeEnabled == shouldBeEnabled) { return; }
    envelopeEnabled = shouldBeEnabled;
    repaint();
}

juce::Colour BreakpointEnvelopeEditor::colourFor(const juce::String& key, juce::Colour fallback) const
{
    return config != nullptr ? config->getColour(configPrefix + ".graph." + key, fallback) : fallback;
}

float BreakpointEnvelopeEditor::floatFor(const juce::String& key, float fallback) const
{
    return config != nullptr ? config->getFloat(configPrefix + ".graph." + key, fallback) : fallback;
}

// The frame's own styling keeps the keys the card already used for it, so an
// existing UIConfig that themed the graph still themes it.
float BreakpointEnvelopeEditor::configFor(const juce::String& key, float fallback) const
{
    return config != nullptr ? config->getFloat(configPrefix + ".visual.graph." + key, fallback)
                             : fallback;
}

juce::Colour BreakpointEnvelopeEditor::configColour(const juce::String& key,
                                                    juce::Colour fallback) const
{
    return config != nullptr ? config->getColour(configPrefix + ".visual.graph." + key, fallback)
                             : fallback;
}

// The colours the card drew its own envelope in, so the editor looks like the
// component it replaced the inside of rather than like a control borrowed from
// somewhere else. A bypassed envelope goes grey with the rest of the card.
juce::Colour BreakpointEnvelopeEditor::curveColour() const
{
    return envelopeEnabled ? accent.brighter(0.25f) : juce::Colour::fromRGB(176, 176, 176);
}

juce::Colour BreakpointEnvelopeEditor::fillColour() const
{
    return (envelopeEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180)).withAlpha(0.28f);
}

juce::Rectangle<float> BreakpointEnvelopeEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced(floatFor("insetX", 8.0f), floatFor("insetY", 10.0f));
}

double BreakpointEnvelopeEditor::visibleSeconds() const
{
    // The envelope fills the width. It used to be drawn into 89% of it, with
    // headroom past the last point so that point was not pinned to the edge -
    // which left the curve visibly short of its own background.
    //
    // The axis is instead held at whatever it was when a drag STARTED, and only
    // allowed to grow. That keeps the last point draggable to the right (the
    // axis grows with it) without the whole curve rescaling under the cursor
    // every time a point moves, and it snaps back to exactly full width the
    // moment the mouse is released.
    const auto fitted = juce::jmax(kMinimumVisibleSeconds, envelope.getTotalSeconds());
    return dragging.target != Target::none ? juce::jmax(fitted, dragAxisSeconds) : fitted;
}

juce::Point<float> BreakpointEnvelopeEditor::toScreen(double timeSeconds, double value) const
{
    const auto area = plotArea();
    const auto x = area.getX() + area.getWidth() * static_cast<float>(timeSeconds / visibleSeconds());
    const auto y = area.getBottom() - area.getHeight() * static_cast<float>(juce::jlimit(0.0, 1.0, value));
    return { x, y };
}

double BreakpointEnvelopeEditor::screenToTime(float x) const
{
    const auto area = plotArea();
    if (area.getWidth() <= 0.0f) { return 0.0; }
    return juce::jmax(0.0, (x - area.getX()) / area.getWidth() * visibleSeconds());
}

double BreakpointEnvelopeEditor::screenToValue(float y) const
{
    const auto area = plotArea();
    if (area.getHeight() <= 0.0f) { return 0.0; }
    return juce::jlimit(0.0, 1.0, static_cast<double>((area.getBottom() - y) / area.getHeight()));
}

juce::Point<float> BreakpointEnvelopeEditor::pointToScreen(int index) const
{
    const auto& point = envelope.getPoint(index);
    return toScreen(point.timeSeconds, point.value);
}

juce::Point<float> BreakpointEnvelopeEditor::curveHandlePosition(int segment) const
{
    const auto& a = envelope.getPoint(segment);
    const auto& b = envelope.getPoint(segment + 1);

    // The handle sits ON the curve, halfway along in time, because that is where
    // the user would grab to bend it. Putting it at the midpoint of the straight
    // line between the two points instead would leave it floating off a bent
    // segment, which reads as an unrelated control.
    const auto midTime = 0.5 * (a.timeSeconds + b.timeSeconds);
    const auto midValue = a.value + (b.value - a.value)
                                        * px3::BreakpointEnvelope::shape(0.5, a.curveToNext);
    return toScreen(midTime, midValue);
}

BreakpointEnvelopeEditor::Hit BreakpointEnvelopeEditor::grabAt(juce::Point<float> position) const
{
    // Points win over curve handles: a breakpoint and the handle of the segment
    // leaving it can overlap on a very short segment, and the point is what a
    // user is more likely to be reaching for.
    for (int i = 0; i < envelope.getPointCount(); ++i)
    {
        if (pointToScreen(i).getDistanceFrom(position) <= kPointGrabRadius)
        {
            return { Target::point, i };
        }
    }

    for (int i = 0; i + 1 < envelope.getPointCount(); ++i)
    {
        if (curveHandlePosition(i).getDistanceFrom(position) <= kCurveGrabRadius)
        {
            return { Target::curve, i };
        }
    }

    return {};
}

void BreakpointEnvelopeEditor::buildCurvePath(juce::Path& path) const
{
    const auto area = plotArea();
    if (area.isEmpty() || envelope.getPointCount() < 2) { return; }

    const auto start = pointToScreen(0);
    path.startNewSubPath(start);

    for (int i = 0; i + 1 < envelope.getPointCount(); ++i)
    {
        const auto& a = envelope.getPoint(i);
        const auto& b = envelope.getPoint(i + 1);

        const auto from = toScreen(a.timeSeconds, a.value);
        const auto to = toScreen(b.timeSeconds, b.value);

        // Sampled at roughly one point per pixel of width. A curve drawn with a
        // handful of segments looks like a polygon at exactly the moment the
        // user is bending it, which is when they are looking hardest.
        const auto span = juce::jmax(1.0f, to.x - from.x);
        const auto steps = juce::jlimit(2, 256, juce::roundToInt(span));

        for (int s = 1; s <= steps; ++s)
        {
            const auto x = static_cast<double>(s) / steps;
            const auto value = a.value + (b.value - a.value)
                                             * px3::BreakpointEnvelope::shape(x, a.curveToNext);
            const auto time = a.timeSeconds + (b.timeSeconds - a.timeSeconds) * x;
            path.lineTo(toScreen(time, value));
        }
    }
}

void BreakpointEnvelopeEditor::resized()
{
    repaint();
}

void BreakpointEnvelopeEditor::paint(juce::Graphics& g)
{
    const auto area = plotArea();
    if (area.isEmpty()) { return; }

    // The editor draws its own background and frame.
    //
    // They used to belong to the card, which meant one rectangle computed in
    // two places and kept in step by hand - and the curve appearing outside the
    // frame is exactly what that costs when they drift. Owned here, the frame
    // and what is drawn inside it cannot disagree: JUCE clips a component to
    // its own bounds, so the content is inside the frame by construction.
    const auto frame = getLocalBounds().toFloat().reduced(0.5f);
    const auto frameRadius = configFor("cornerRadius", 7.0f);

    g.setColour(configColour("fillColour", juce::Colour::fromRGB(14, 14, 18))
                    .withAlpha(configFor("fillAlpha", 170.0f) / 255.0f));
    g.fillRoundedRectangle(frame, frameRadius);

    g.setColour((envelopeEnabled ? curveColour() : juce::Colour::fromRGB(136, 136, 136))
                    .withAlpha(configFor(envelopeEnabled ? "strokeAlphaEnabled"
                                                         : "strokeAlphaDisabled",
                                         envelopeEnabled ? 82.0f : 62.0f) / 255.0f));
    g.drawRoundedRectangle(frame, frameRadius, configFor("strokeThickness", 1.0f));

    // ---- grid -------------------------------------------------------------
    g.setColour(colourFor("gridColor", juce::Colour::fromRGBA(255, 255, 255, 24)));
    const auto gridWidth = floatFor("gridWidth", 1.0f);
    for (int i = 1; i < 4; ++i)
    {
        const auto y = area.getY() + area.getHeight() * static_cast<float>(i) / 4.0f;
        g.fillRect(area.getX(), y, area.getWidth(), gridWidth);
    }

    // ---- the sustain region -----------------------------------------------
    // Shaded rather than marked with a line, because it is a REGION - the
    // envelope holds here for as long as the key is down, which a single
    // vertical rule does not say.
    const auto sustainPoint = envelope.getSustainPoint();
    if (sustainPoint >= 0 && sustainPoint < envelope.getPointCount())
    {
        const auto sustainX = pointToScreen(sustainPoint).x;
        g.setColour(colourFor("sustainRegionColor", curveColour().withAlpha(0.07f)));
        g.fillRect(juce::Rectangle<float>(sustainX, area.getY(),
                                          juce::jmax(0.0f, area.getRight() - sustainX),
                                          area.getHeight()));

        g.setColour(colourFor("sustainLineColor", curveColour().withAlpha(0.35f)));
        g.fillRect(sustainX, area.getY(), floatFor("sustainLineWidth", 1.0f), area.getHeight());
    }

    // ---- the curve ---------------------------------------------------------
    juce::Path curve;
    buildCurvePath(curve);

    // Filled under the line, which is what makes the envelope read as a shape
    // rather than as a graph of one.
    juce::Path filled(curve);
    if (envelope.getPointCount() >= 2)
    {
        filled.lineTo(pointToScreen(envelope.getPointCount() - 1).x, area.getBottom());
        filled.lineTo(pointToScreen(0).x, area.getBottom());
        filled.closeSubPath();

        g.setGradientFill(juce::ColourGradient(
            colourFor("fillTopColor", fillColour()), area.getX(), area.getY(),
            colourFor("fillBottomColor", fillColour().withMultipliedAlpha(0.08f)),
            area.getX(), area.getBottom(), false));
        g.fillPath(filled);
    }

    g.setColour(colourFor("lineColor", curveColour()));
    g.strokePath(curve, juce::PathStrokeType(floatFor("lineWidth", 2.0f),
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // ---- where the envelope actually is ------------------------------------
    if (livePosition >= 0.0f)
    {
        const auto x = area.getX() + area.getWidth() * livePosition;
        g.setColour(colourFor("playheadColor", juce::Colour::fromRGBA(255, 255, 255, 90)));
        g.fillRect(x, area.getY(), floatFor("playheadWidth", 1.0f), area.getHeight());
    }

    // ---- curve handles -----------------------------------------------------
    const auto handleRadius = floatFor("curveHandleRadius", 3.0f);
    for (int i = 0; i + 1 < envelope.getPointCount(); ++i)
    {
        const auto position = curveHandlePosition(i);
        const auto active = (hovered.target == Target::curve && hovered.index == i)
                            || (dragging.target == Target::curve && dragging.index == i);
        const auto bent = std::abs(envelope.getPoint(i).curveToNext) > 1.0e-6;

        // A handle on a straight segment is drawn faintly: it is available, but
        // it is not information.
        g.setColour(colourFor("curveHandleColor", curveColour())
                        .withAlpha(active ? 1.0f : (bent ? 0.75f : 0.3f)));
        const auto radius = active ? handleRadius * 1.5f : handleRadius;
        g.fillEllipse(position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // ---- breakpoints -------------------------------------------------------
    const auto pointRadius = floatFor("pointRadius", 4.0f);
    for (int i = 0; i < envelope.getPointCount(); ++i)
    {
        const auto position = pointToScreen(i);
        const auto isHovered = hovered.target == Target::point && hovered.index == i;
        const auto isSelected = selectedPoint == i;
        const auto isSustain = i == sustainPoint;

        const auto radius = pointRadius * (isHovered || isSelected ? 1.4f : 1.0f);

        g.setColour(colourFor("pointFillColor", juce::Colour::fromRGB(18, 18, 20)));
        g.fillEllipse(position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f);

        g.setColour(isSustain ? colourFor("sustainPointColor", curveColour().brighter(0.3f))
                              : colourFor("pointColor", curveColour()));
        g.drawEllipse(position.x - radius, position.y - radius, radius * 2.0f, radius * 2.0f,
                      floatFor("pointOutlineWidth", 1.8f));
    }

    // ---- readout -----------------------------------------------------------
    // Only while dragging. A permanent readout is clutter on a graph this size,
    // and the numbers only matter while they are being changed.
    if (showReadout && dragging.target != Target::none)
    {
        juce::String text;
        if (dragging.target == Target::point)
        {
            const auto& point = envelope.getPoint(dragging.index);
            text = juce::String(point.timeSeconds * 1000.0, 0) + " ms   "
                   + juce::String(point.value, 2);
        }
        else
        {
            text = "curve " + juce::String(juce::roundToInt(
                       envelope.getPoint(dragging.index).curveToNext * 100.0)) + "%";
        }

        g.setColour(colourFor("readoutColor", juce::Colour::fromRGB(232, 232, 232)));
        g.setFont(juce::FontOptions(floatFor("readoutSize", 10.5f)));
        g.drawText(text, getLocalBounds().reduced(6), juce::Justification::topRight, false);
    }
}

void BreakpointEnvelopeEditor::mouseMove(const juce::MouseEvent& event)
{
    const auto hit = grabAt(event.position);
    if (hit.target != hovered.target || hit.index != hovered.index)
    {
        hovered = hit;
        setMouseCursor(hit.target == Target::none ? juce::MouseCursor::NormalCursor
                                                  : juce::MouseCursor::DraggingHandCursor);
        repaint();
    }
}

void BreakpointEnvelopeEditor::mouseExit(const juce::MouseEvent&)
{
    if (hovered.target != Target::none)
    {
        hovered = {};
        repaint();
    }
}

void BreakpointEnvelopeEditor::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();

    dragging = grabAt(event.position);
    dragOrigin = event.position;
    dragAxisSeconds = juce::jmax(kMinimumVisibleSeconds, envelope.getTotalSeconds());
    showReadout = dragging.target != Target::none;

    if (dragging.target == Target::point)
    {
        selectedPoint = dragging.index;
        dragStartTime = envelope.getPoint(dragging.index).timeSeconds;
        dragStartValue = envelope.getPoint(dragging.index).value;
    }
    else if (dragging.target == Target::curve)
    {
        dragStartCurve = envelope.getPoint(dragging.index).curveToNext;
    }
    else
    {
        selectedPoint = -1;
    }

    repaint();
}

void BreakpointEnvelopeEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging.target == Target::none) { return; }

    // Shift is fine adjustment, matching the rest of the synth's controls.
    const auto scale = event.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto delta = (event.position - dragOrigin) * scale;

    if (dragging.target == Target::point)
    {
        const auto area = plotArea();
        const auto timeDelta = area.getWidth() > 0.0f
                                 ? static_cast<double>(delta.x) / area.getWidth() * visibleSeconds()
                                 : 0.0;
        const auto valueDelta = area.getHeight() > 0.0f
                                  ? -static_cast<double>(delta.y) / area.getHeight()
                                  : 0.0;

        envelope.setPoint(dragging.index, dragStartTime + timeDelta, dragStartValue + valueDelta);
        notifyChanged();
    }
    else if (dragging.target == Target::curve)
    {
        const auto& a = envelope.getPoint(dragging.index);
        const auto& b = envelope.getPoint(dragging.index + 1);

        // Dragging up bends the segment up, whichever direction the segment
        // itself runs. Without the sign flip a falling segment bends the wrong
        // way under the mouse, which feels broken long before it looks it.
        const auto rising = b.value >= a.value;
        const auto amount = -delta.y / juce::jmax(1.0f, plotArea().getHeight() * 0.5f);
        envelope.setCurve(dragging.index,
                          dragStartCurve + (rising ? amount : -amount));
        notifyChanged();
    }

    repaint();
}

void BreakpointEnvelopeEditor::mouseUp(const juce::MouseEvent&)
{
    dragging = {};
    showReadout = false;
    repaint();
}

void BreakpointEnvelopeEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    const auto hit = grabAt(event.position);

    if (hit.target == Target::point)
    {
        if (envelope.removePoint(hit.index))
        {
            selectedPoint = -1;
            notifyChanged();
            repaint();
        }
        return;
    }

    if (hit.target == Target::curve)
    {
        // Double-clicking a curve handle straightens that segment, which is the
        // fastest way back from an experiment.
        envelope.setCurve(hit.index, 0.0);
        notifyChanged();
        repaint();
        return;
    }

    const auto added = envelope.addPoint(screenToTime(event.position.x),
                                         screenToValue(event.position.y));
    if (added >= 0)
    {
        selectedPoint = added;
        notifyChanged();
        repaint();
    }
}

bool BreakpointEnvelopeEditor::keyPressed(const juce::KeyPress& key)
{
    if (selectedPoint < 0) { return false; }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (envelope.removePoint(selectedPoint))
        {
            selectedPoint = -1;
            notifyChanged();
            repaint();
            return true;
        }
        return false;
    }

    const auto fine = key.getModifiers().isShiftDown();
    const auto timeStep = visibleSeconds() * (fine ? 0.002 : 0.01);
    const auto valueStep = fine ? 0.002 : 0.01;

    const auto& point = envelope.getPoint(selectedPoint);
    auto time = point.timeSeconds;
    auto value = point.value;

    if (key == juce::KeyPress::leftKey)        { time -= timeStep; }
    else if (key == juce::KeyPress::rightKey)  { time += timeStep; }
    else if (key == juce::KeyPress::upKey)     { value += valueStep; }
    else if (key == juce::KeyPress::downKey)   { value -= valueStep; }
    else                                       { return false; }

    envelope.setPoint(selectedPoint, time, value);
    notifyChanged();
    repaint();
    return true;
}

void BreakpointEnvelopeEditor::notifyChanged()
{
    if (onEnvelopeChanged != nullptr)
    {
        onEnvelopeChanged(envelope);
    }
}
