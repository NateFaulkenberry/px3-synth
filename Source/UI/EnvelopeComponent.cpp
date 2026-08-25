#include "EnvelopeComponent.h"

#include "UIConfig.h"

#include <cmath>

EnvelopeComponent::EnvelopeComponent(juce::AudioParameterFloat& attackIn,
                                                                         juce::AudioParameterFloat& decayIn,
                                                                         juce::AudioParameterFloat& sustainIn,
                                                                         juce::AudioParameterFloat& releaseIn,
                                                                         juce::AudioParameterBool& enabledIn,
                                                                         juce::ToggleButton& enabledButtonIn,
                                                                         juce::Label& enabledLabelIn,
                                                                         juce::Label& assignLabelIn,
                                                                         juce::ComboBox& assignBoxIn,
                                                                         juce::Colour accentIn)
        : attack(attackIn),
            decay(decayIn),
            sustain(sustainIn),
            release(releaseIn),
            enabled(enabledIn),
            enabledButton(enabledButtonIn),
            enabledLabel(enabledLabelIn),
            assignLabel(assignLabelIn),
            assignBox(assignBoxIn),
            accent(accentIn)
{
        addAndMakeVisible(enabledButton);
        addAndMakeVisible(enabledLabel);
        addAndMakeVisible(assignLabel);
        addAndMakeVisible(assignBox);
        baseEnabledLabelTextColour = enabledLabel.findColour(juce::Label::textColourId);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    refreshFromParameters();
}

void EnvelopeComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void EnvelopeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    const auto textColour = uiConfig != nullptr
                                ? uiConfig->getColour("mod.env.visual.onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232);
    const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.onLabel.fontSize", 11.5f) : 11.5f;
    const auto text = uiConfig != nullptr ? uiConfig->getString("mod.env.visual.onLabel.text", "ON") : juce::String("ON");
    enabledLabel.setText(text, juce::dontSendNotification);
    enabledLabel.setColour(juce::Label::textColourId, textColour);
    enabledLabel.setFont(juce::FontOptions(fontSize));
    baseEnabledLabelTextColour = textColour;

    repaint();
}

void EnvelopeComponent::refreshFromParameters()
{
    const auto a = attack.get();
    const auto d = decay.get();
    const auto s = sustain.get();
    const auto r = release.get();
    const auto nextEnabled = enabled.get();

    if (std::abs(a - lastAttack) > 0.0001f
        || std::abs(d - lastDecay) > 0.0001f
        || std::abs(s - lastSustain) > 0.0001f
        || std::abs(r - lastRelease) > 0.0001f
        || nextEnabled != currentEnabled)
    {
        lastAttack = a;
        lastDecay = d;
        lastSustain = s;
        lastRelease = r;
        currentEnabled = nextEnabled;
        enabledButton.setToggleState(currentEnabled, juce::dontSendNotification);
        assignLabel.setEnabled(currentEnabled);
        assignBox.setEnabled(currentEnabled);
        enabledLabel.setColour(juce::Label::textColourId,
                               currentEnabled ? baseEnabledLabelTextColour : juce::Colour::fromRGB(176, 176, 176));
        if (!currentEnabled)
        {
            hoverHandle = DragHandle::none;
            dragHandle = DragHandle::none;
            draggingSustainSegment = false;
        }
        setMouseCursor(currentEnabled ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvelopeComponent::paint(juce::Graphics& g)
{
    auto card = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, card.getWidth());
    card = card.withSizeKeepingCentre(cardWidth, card.getHeight());
    const auto cardBounds = card.toFloat();
    if (cardBounds.isEmpty())
    {
        return;
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    const auto bgTintAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.bgTintAlpha", 0.10f) : 0.10f;
    const auto enabledBgTintColour = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.bgTintColour", effectiveAccent)
                                                         : effectiveAccent;
    const auto topFillAlpha = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.topFillAlpha", 0.10f) : 0.10f;
    const auto enabledTopFillColour = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.topFillColour", effectiveAccent)
                                                          : effectiveAccent;
    const auto cardCornerRadius = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.cardCornerRadius", 8.0f) : 8.0f;
    const auto outerStrokeThickness = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.outerStrokeThickness", 1.2f) : 1.2f;
    const auto outerStrokeColour = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.outerStrokeColour", juce::Colour::fromRGB(220, 232, 252))
                                                       : juce::Colour::fromRGB(220, 232, 252);
    const auto outerStrokeAlphaEnabled = uiConfig != nullptr ? uiConfig->getInt("mod.env.visual.outerStrokeAlphaEnabled", 88) : 88;
    const auto outerStrokeAlphaDisabled = uiConfig != nullptr ? uiConfig->getInt("mod.env.visual.outerStrokeAlphaDisabled", 66) : 66;
    const auto bgTintColour = currentEnabled ? enabledBgTintColour : juce::Colour::fromRGB(112, 112, 112);
    const auto topFillColour = currentEnabled ? enabledTopFillColour : juce::Colour::fromRGB(136, 136, 136);

    const auto innerFillBounds = cardBounds.reduced(6.0f);
    g.setColour(bgTintColour.withAlpha(bgTintAlpha));
    g.fillRoundedRectangle(innerFillBounds, cardCornerRadius);
    g.setColour(topFillColour.withAlpha(topFillAlpha));
    juce::Path topFill;
    const auto topHalf = innerFillBounds.withTrimmedBottom(innerFillBounds.getHeight() * 0.5f);
    topFill.addRoundedRectangle(topHalf.getX(),
                                topHalf.getY(),
                                topHalf.getWidth(),
                                topHalf.getHeight(),
                                cardCornerRadius,
                                cardCornerRadius,
                                true,
                                true,
                                false,
                                false);
    g.fillPath(topFill);
    g.setColour(outerStrokeColour.withAlpha(static_cast<float>(currentEnabled ? outerStrokeAlphaEnabled : outerStrokeAlphaDisabled) / 255.0f));
    g.drawRoundedRectangle(cardBounds, cardCornerRadius, outerStrokeThickness);

    const auto geom = computeGeometry();
    const auto graphArea = juce::Rectangle<float>(geom.left - 6.0f,
                                                  geom.top - 5.0f,
                                                  (geom.right - geom.left) + 12.0f,
                                                  (geom.bottom - geom.top) + 10.0f);
    const auto graphCornerRadius = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.graph.cornerRadius", 7.0f) : 7.0f;
    const auto graphFillColour = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.graph.fillColour", juce::Colour::fromRGB(14, 14, 18))
                                                     : juce::Colour::fromRGB(14, 14, 18);
    const auto graphFillAlpha = uiConfig != nullptr ? uiConfig->getInt("mod.env.visual.graph.fillAlpha", 170) : 170;
    const auto graphStrokeThickness = uiConfig != nullptr ? uiConfig->getFloat("mod.env.visual.graph.strokeThickness", 1.0f) : 1.0f;
    const auto graphStrokeAlphaEnabled = uiConfig != nullptr ? uiConfig->getInt("mod.env.visual.graph.strokeAlphaEnabled", 82) : 82;
    const auto graphStrokeAlphaDisabled = uiConfig != nullptr ? uiConfig->getInt("mod.env.visual.graph.strokeAlphaDisabled", 62) : 62;
    const auto graphStrokeColourEnabled = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.graph.strokeColourEnabled", effectiveAccent)
                                                              : effectiveAccent;
    const auto graphStrokeColourDisabled = uiConfig != nullptr ? uiConfig->getColour("mod.env.visual.graph.strokeColourDisabled", juce::Colour::fromRGB(136, 136, 136))
                                                               : juce::Colour::fromRGB(136, 136, 136);
    const auto graphStrokeColour = currentEnabled ? graphStrokeColourEnabled : graphStrokeColourDisabled;
    const auto graphStrokeAlpha = currentEnabled ? graphStrokeAlphaEnabled : graphStrokeAlphaDisabled;

    g.setColour(graphFillColour.withAlpha(static_cast<float>(graphFillAlpha) / 255.0f));
    g.fillRoundedRectangle(graphArea, graphCornerRadius);
    g.setColour(graphStrokeColour.withAlpha(static_cast<float>(graphStrokeAlpha) / 255.0f));
    g.drawRoundedRectangle(graphArea, graphCornerRadius, graphStrokeThickness);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    for (int i = 1; i < 6; ++i)
    {
        const auto x = juce::jmap(static_cast<float>(i), 0.0f, 6.0f, geom.left, geom.right);
        g.drawVerticalLine(static_cast<int>(std::lround(x)), geom.top, geom.bottom);
    }

    for (int i = 0; i <= 4; ++i)
    {
        const auto y = juce::jmap(static_cast<float>(i), 0.0f, 4.0f, geom.top, geom.bottom);
        g.drawHorizontalLine(static_cast<int>(std::lround(y)), geom.left, geom.right);
    }

    g.setColour(juce::Colour::fromRGBA(210, 210, 210, 75));
    g.setFont(juce::FontOptions(9.0f));
    g.drawText("100%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>(geom.top) - 6, 32, 12), juce::Justification::centredRight);
    g.drawText("50%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>((geom.top + geom.bottom) * 0.5f) - 6, 32, 12), juce::Justification::centredRight);
    g.drawText("0%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>(geom.bottom) - 6, 32, 12), juce::Justification::centredRight);

    juce::Path envPath;
    envPath.startNewSubPath(geom.start);
    envPath.lineTo(geom.attackPoint);
    envPath.lineTo(geom.decaySustainPoint);
    envPath.lineTo(geom.releasePoint);
    envPath.lineTo(geom.end);

    g.setColour(effectiveAccent.withAlpha(0.28f));
    g.strokePath(envPath, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour(currentEnabled ? accent.brighter(0.25f) : juce::Colour::fromRGB(176, 176, 176));
    g.strokePath(envPath, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawHandleMarker(g, geom.attackPoint, DragHandle::attack);
    drawHandleMarker(g, geom.decaySustainPoint, DragHandle::decaySustain);
    drawHandleMarker(g, geom.releasePoint, DragHandle::release);

    drawHandleLabel(g, geom.attackPoint, DragHandle::attack, "A");
    drawHandleLabel(g, geom.decaySustainPoint, DragHandle::decaySustain, "D/S");
    drawHandleLabel(g, geom.releasePoint, DragHandle::release, "R");

    const auto active = dragHandle != DragHandle::none ? dragHandle : hoverHandle;
    if (active != DragHandle::none)
    {
        const auto handlePos = handlePositionFor(active, geom);
        const auto text = valueTextForHandle(active);

        auto bubble = juce::Rectangle<float>(0.0f, 0.0f, 122.0f, 30.0f);
        bubble.setCentre(handlePos.translated(0.0f, -24.0f));
        bubble = bubble.withPosition(juce::jlimit(cardBounds.getX(), cardBounds.getRight() - bubble.getWidth(), bubble.getX()),
                 juce::jlimit(cardBounds.getY(), cardBounds.getBottom() - bubble.getHeight(), bubble.getY()));

        g.setColour(juce::Colour::fromRGBA(9, 14, 9, 232));
        g.fillRoundedRectangle(bubble, 6.0f);
        g.setColour(effectiveAccent.withAlpha(0.78f));
        g.drawRoundedRectangle(bubble, 6.0f, 1.0f);
        g.setColour(juce::Colour::fromRGB(232, 242, 232));
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(text, bubble.toNearestInt(), juce::Justification::centred);
    }
}

void EnvelopeComponent::mouseMove(const juce::MouseEvent& event)
{
    if (!currentEnabled)
    {
        return;
    }

    hoverHandle = pickHandle(event.position, computeGeometry());
    repaint();
}

void EnvelopeComponent::mouseExit(const juce::MouseEvent&)
{
    if (dragHandle == DragHandle::none)
    {
        hoverHandle = DragHandle::none;
        repaint();
    }
}

void EnvelopeComponent::mouseDown(const juce::MouseEvent& event)
{
    if (!currentEnabled)
    {
        return;
    }

    const auto geom = computeGeometry();
    dragHandle = pickHandle(event.position, geom);
    hoverHandle = dragHandle;
    draggingSustainSegment = false;
    if (dragHandle == DragHandle::none)
    {
        return;
    }

    if (dragHandle == DragHandle::decaySustain)
    {
        constexpr float dotHitRadius = 13.0f;
        const auto dotHitSq = dotHitRadius * dotHitRadius;
        const auto dsDotDistSq = distSq(event.position, geom.decaySustainPoint);
        if (dsDotDistSq > dotHitSq)
        {
            draggingSustainSegment = true;
            sustainDragStartX = event.position.getX();
            sustainDragStartDecayX = geom.decaySustainPoint.getX();
            sustainDragStartReleaseX = geom.releasePoint.getX();
        }
    }

    if (dragHandle == DragHandle::attack)
    {
        attack.beginChangeGesture();
    }
    else if (dragHandle == DragHandle::decaySustain)
    {
        decay.beginChangeGesture();
        sustain.beginChangeGesture();
    }
    else if (dragHandle == DragHandle::release)
    {
        release.beginChangeGesture();
    }

    applyDragPosition(event.position, geom);
}

void EnvelopeComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (!currentEnabled)
    {
        return;
    }

    if (dragHandle == DragHandle::none)
    {
        return;
    }

    applyDragPosition(event.position, computeGeometry());
}

void EnvelopeComponent::mouseUp(const juce::MouseEvent&)
{
    if (dragHandle == DragHandle::attack)
    {
        attack.endChangeGesture();
    }
    else if (dragHandle == DragHandle::decaySustain)
    {
        decay.endChangeGesture();
        sustain.endChangeGesture();
    }
    else if (dragHandle == DragHandle::release)
    {
        release.endChangeGesture();
    }

    dragHandle = DragHandle::none;
    draggingSustainSegment = false;
    repaint();
}

void EnvelopeComponent::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!currentEnabled)
    {
        return;
    }

    const auto handle = pickHandle(event.position, computeGeometry());
    if (handle == DragHandle::none)
    {
        return;
    }

    if (handle == DragHandle::attack)
    {
        attack.beginChangeGesture();
        attack.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(attack).getDefaultValue());
        attack.endChangeGesture();
    }
    else if (handle == DragHandle::decaySustain)
    {
        decay.beginChangeGesture();
        sustain.beginChangeGesture();
        decay.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(decay).getDefaultValue());
        sustain.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(sustain).getDefaultValue());
        decay.endChangeGesture();
        sustain.endChangeGesture();
    }
    else if (handle == DragHandle::release)
    {
        release.beginChangeGesture();
        release.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(release).getDefaultValue());
        release.endChangeGesture();
    }

    refreshFromParameters();
}

float EnvelopeComponent::clamp01(float v)
{
    return juce::jlimit(0.0f, 1.0f, v);
}

float EnvelopeComponent::timeToVisualNorm(float seconds, float minValue, float maxValue)
{
    const auto clamped = juce::jlimit(minValue, maxValue, seconds);
    const auto denom = std::log(juce::jmax(minValue * 1.001f, maxValue) / minValue);
    if (denom <= 0.0f)
    {
        return 0.0f;
    }
    return clamp01(std::log(clamped / minValue) / denom);
}

float EnvelopeComponent::visualNormToTime(float norm, float minValue, float maxValue)
{
    const auto clampedNorm = clamp01(norm);
    return minValue * std::pow(maxValue / minValue, clampedNorm);
}

EnvelopeComponent::Geometry EnvelopeComponent::computeGeometry() const
{
    Geometry geom;
    auto card = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, card.getWidth());
    card = card.withSizeKeepingCentre(cardWidth, card.getHeight());
    auto graphLayout = card.toFloat().reduced(10.0f, 10.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(24.0f);
    graphLayout.removeFromTop(6.0f);
    graphLayout.removeFromTop(8.0f);
    graphLayout.removeFromBottom(10.0f);

    geom.left = graphLayout.getX() + 6.0f;
    geom.right = graphLayout.getRight() - 6.0f;
    geom.top = graphLayout.getY() + 5.0f;
    geom.bottom = graphLayout.getBottom() - 5.0f;

    const auto totalWidth = juce::jmax(20.0f, geom.right - geom.left);
    geom.attackRangeWidth = totalWidth * 0.30f;
    geom.releaseRangeWidth = totalWidth * 0.30f;
    geom.minDecayGap = juce::jmax(10.0f, totalWidth * 0.04f);
    geom.minSustainWidth = juce::jmax(12.0f, totalWidth * 0.10f);

    const auto attackRange = attack.getNormalisableRange();
    const auto decayRange = decay.getNormalisableRange();
    const auto releaseRange = release.getNormalisableRange();

    const auto attackNorm = timeToVisualNorm(attack.get(), attackRange.start, attackRange.end);
    const auto decayNorm = timeToVisualNorm(decay.get(), decayRange.start, decayRange.end);
    const auto releaseNorm = timeToVisualNorm(release.get(), releaseRange.start, releaseRange.end);
    const auto sustainNorm = clamp01(sustain.get());

    const auto xAttack = geom.left + attackNorm * geom.attackRangeWidth;
    auto xRelease = geom.right - releaseNorm * geom.releaseRangeWidth;
    xRelease = juce::jmax(xAttack + geom.minDecayGap + geom.minSustainWidth, xRelease);

    const auto xDecayMin = xAttack + geom.minDecayGap;
    const auto xDecayMax = juce::jmax(xDecayMin + 1.0f, xRelease - geom.minSustainWidth);
    const auto xDecay = xDecayMin + decayNorm * (xDecayMax - xDecayMin);
    const auto ySustain = juce::jmap(sustainNorm, geom.bottom, geom.top);

    geom.start = { geom.left, geom.bottom };
    geom.attackPoint = { xAttack, geom.top };
    geom.decaySustainPoint = { xDecay, ySustain };
    geom.releasePoint = { xRelease, ySustain };
    geom.end = { geom.right, geom.bottom };
    return geom;
}

float EnvelopeComponent::distSq(juce::Point<float> a, juce::Point<float> b)
{
    const auto dx = a.getX() - b.getX();
    const auto dy = a.getY() - b.getY();
    return dx * dx + dy * dy;
}

float EnvelopeComponent::distToSegmentSq(juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
{
    const auto ab = b - a;
    const auto ap = p - a;
    const auto abLenSq = ab.getX() * ab.getX() + ab.getY() * ab.getY();

    if (abLenSq <= 0.0001f)
    {
        return distSq(p, a);
    }

    const auto tRaw = (ap.getX() * ab.getX() + ap.getY() * ab.getY()) / abLenSq;
    const auto t = juce::jlimit(0.0f, 1.0f, tRaw);
    const auto closest = a + ab * t;
    return distSq(p, closest);
}

EnvelopeComponent::DragHandle EnvelopeComponent::pickHandle(juce::Point<float> p,
                                                                          const Geometry& geom) const
{
    constexpr float hitRadius = 13.0f;
    const auto hitSq = hitRadius * hitRadius;

    const auto a = distSq(p, geom.attackPoint);
    const auto ds = distSq(p, geom.decaySustainPoint);
    const auto r = distSq(p, geom.releasePoint);

    auto best = DragHandle::none;
    auto bestSq = hitSq;

    if (a <= bestSq)
    {
        bestSq = a;
        best = DragHandle::attack;
    }
    if (ds <= bestSq)
    {
        bestSq = ds;
        best = DragHandle::decaySustain;
    }
    if (r <= bestSq)
    {
        best = DragHandle::release;
    }

    // Allow dragging anywhere on the sustain line by mapping line hits to D/S.
    if (best == DragHandle::none)
    {
        constexpr float lineHitRadius = 8.0f;
        const auto lineHitSq = lineHitRadius * lineHitRadius;
        const auto lineDistSq = distToSegmentSq(p, geom.decaySustainPoint, geom.releasePoint);
        if (lineDistSq <= lineHitSq)
        {
            return DragHandle::decaySustain;
        }
    }

    return best;
}

juce::Point<float> EnvelopeComponent::handlePositionFor(DragHandle handle,
                                                                const Geometry& geom) const
{
    if (handle == DragHandle::attack)
    {
        return geom.attackPoint;
    }
    if (handle == DragHandle::decaySustain)
    {
        return geom.decaySustainPoint;
    }
    if (handle == DragHandle::release)
    {
        return geom.releasePoint;
    }
    return geom.start;
}

void EnvelopeComponent::drawHandleMarker(juce::Graphics& g,
                                                juce::Point<float> center,
                                                DragHandle handle) const
{
    const auto active = (handle == dragHandle) || (handle == hoverHandle);
    const auto radius = active ? 6.0f : 5.0f;
    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    g.setColour(effectiveAccent.withAlpha(active ? 1.0f : 0.86f));
    g.fillEllipse(center.getX() - radius, center.getY() - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(juce::Colour::fromRGBA(12, 12, 12, 220));
    g.drawEllipse(center.getX() - radius, center.getY() - radius, radius * 2.0f, radius * 2.0f, 1.2f);
}

void EnvelopeComponent::drawHandleLabel(juce::Graphics& g,
                                                juce::Point<float> center,
                                                DragHandle handle,
                                                const juce::String& id) const
{
    const auto active = (handle == dragHandle) || (handle == hoverHandle);
    auto labelBounds = juce::Rectangle<float>(center.getX() - 14.0f, center.getY() + 6.0f, 28.0f, 12.0f);

    g.setColour(juce::Colour::fromRGBA(6, 12, 6, active ? 238 : 212));
    g.fillRoundedRectangle(labelBounds, 3.0f);
    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    g.setColour(effectiveAccent.withAlpha(active ? 0.9f : 0.72f));
    g.drawRoundedRectangle(labelBounds, 3.0f, 0.9f);

    g.setColour(juce::Colour::fromRGBA(238, 248, 238, active ? 250 : 236));
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(id,
               labelBounds.toNearestInt(),
               juce::Justification::centred);
}

juce::String EnvelopeComponent::valueTextForHandle(DragHandle handle) const
{
    if (handle == DragHandle::attack)
    {
        const auto sec = attack.get();
        const auto ms = sec * 1000.0f;
        return "ATTACK " + (sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s");
    }

    if (handle == DragHandle::decaySustain)
    {
        const auto sec = decay.get();
        const auto ms = sec * 1000.0f;
        const auto sustainPct = sustain.get() * 100.0f;
        const auto decayText = sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s";
        return "D " + decayText + " | S " + juce::String(sustainPct, 0) + "%";
    }

    if (handle == DragHandle::release)
    {
        const auto sec = release.get();
        const auto ms = sec * 1000.0f;
        return "RELEASE " + (sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s");
    }

    return {};
}

void EnvelopeComponent::setParameterFromActualValue(juce::AudioParameterFloat& parameter,
                                                            float value)
{
    const auto range = parameter.getNormalisableRange();
    const auto clamped = juce::jlimit(range.start, range.end, value);
    parameter.setValueNotifyingHost(parameter.convertTo0to1(clamped));
}

void EnvelopeComponent::applyDragPosition(juce::Point<float> mousePos,
                                                 const Geometry& geom)
{
    if (!currentEnabled)
    {
        return;
    }

    if (dragHandle == DragHandle::none)
    {
        return;
    }

    const auto x = mousePos.getX();
    const auto y = mousePos.getY();

    if (dragHandle == DragHandle::attack)
    {
        const auto maxX = juce::jmin(geom.left + geom.attackRangeWidth,
                                     geom.releasePoint.getX() - geom.minDecayGap - geom.minSustainWidth);
        const auto clampedX = juce::jlimit(geom.left, maxX, x);
        const auto norm = clamp01((clampedX - geom.left) / juce::jmax(1.0f, geom.attackRangeWidth));
        const auto range = attack.getNormalisableRange();
        setParameterFromActualValue(attack, visualNormToTime(norm, range.start, range.end));
    }
    else if (dragHandle == DragHandle::decaySustain)
    {
        float decayNorm = 0.0f;

        if (draggingSustainSegment)
        {
            const auto segmentWidth = sustainDragStartReleaseX - sustainDragStartDecayX;
            const auto xMin = geom.attackPoint.getX() + geom.minDecayGap;
            const auto xMax = geom.right - segmentWidth;
            const auto targetDecayX = sustainDragStartDecayX + (x - sustainDragStartX);
            const auto clampedDecayX = juce::jlimit(xMin, xMax, targetDecayX);
            const auto clampedReleaseX = clampedDecayX + segmentWidth;

            const auto decayRangeMin = geom.attackPoint.getX() + geom.minDecayGap;
            const auto decayRangeMax = juce::jmax(decayRangeMin + 1.0f, clampedReleaseX - geom.minSustainWidth);
            decayNorm = clamp01((clampedDecayX - decayRangeMin) / juce::jmax(1.0f, decayRangeMax - decayRangeMin));

            const auto releaseNorm = clamp01((geom.right - clampedReleaseX) / juce::jmax(1.0f, geom.releaseRangeWidth));
            const auto releaseRange = release.getNormalisableRange();
            setParameterFromActualValue(release, visualNormToTime(releaseNorm, releaseRange.start, releaseRange.end));
        }
        else
        {
            const auto xMin = geom.attackPoint.getX() + geom.minDecayGap;
            const auto xMax = geom.releasePoint.getX() - geom.minSustainWidth;
            const auto clampedX = juce::jlimit(xMin, xMax, x);
            const auto available = juce::jmax(1.0f, xMax - xMin);
            decayNorm = clamp01((clampedX - xMin) / available);
        }

        const auto yClamped = juce::jlimit(geom.top, geom.bottom, y);
        const auto sustainValue = juce::jmap(yClamped, geom.bottom, geom.top, 0.0f, 1.0f);

        const auto decayRange = decay.getNormalisableRange();
        setParameterFromActualValue(decay, visualNormToTime(decayNorm, decayRange.start, decayRange.end));
        setParameterFromActualValue(sustain, sustainValue);
    }
    else if (dragHandle == DragHandle::release)
    {
        const auto minX = geom.decaySustainPoint.getX() + geom.minSustainWidth;
        const auto clampedX = juce::jlimit(minX, geom.right, x);
        const auto norm = clamp01((geom.right - clampedX) / juce::jmax(1.0f, geom.releaseRangeWidth));
        const auto range = release.getNormalisableRange();
        setParameterFromActualValue(release, visualNormToTime(norm, range.start, range.end));
    }

    refreshFromParameters();
}

void EnvelopeComponent::resized()
{
    auto cardArea = getLocalBounds().reduced(6, 6);
    constexpr int targetCardWidth = 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    cardArea = cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());
    auto area = cardArea.reduced(10, 10);
    auto enabledRow = area.removeFromTop(24);
    const auto labelWidth = uiConfig != nullptr
                                ? uiConfig->getInt("mod.env.visual.onLabel.width",
                                                   uiConfig->getInt("mod.env.visual.onLabel.bounds.width", 52))
                                : 52;
    enabledLabel.setBounds(enabledRow.removeFromLeft(labelWidth));
    enabledButton.setBounds(enabledRow.removeFromLeft(40).reduced(2, 2));

    area.removeFromTop(6);
    auto assignRow = area.removeFromTop(24);
    assignLabel.setBounds(assignRow.removeFromLeft(52));
    assignBox.setBounds(assignRow.reduced(2, 1));
}
