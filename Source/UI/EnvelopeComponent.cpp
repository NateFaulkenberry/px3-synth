#include "EnvelopeComponent.h"

#include "CardInner.h"

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
                                                                         juce::Slider* amountKnobIn,
                                                                         juce::Label* amountLabelIn,
                                                                         juce::Label* amountValueLabelIn,
                                                                         juce::Colour accentIn,
                                                                         const juce::String& configPrefixIn)
        : attack(attackIn),
            decay(decayIn),
            sustain(sustainIn),
            release(releaseIn),
            enabled(enabledIn),
            enabledButton(enabledButtonIn),
            enabledLabel(enabledLabelIn),
            assignLabel(assignLabelIn),
            assignBox(assignBoxIn),
            amountKnob(amountKnobIn),
            amountLabel(amountLabelIn),
            amountValueLabel(amountValueLabelIn),
            accent(accentIn),
            configPrefix(configPrefixIn)
{
        // The mod envelopes' card key and title come straight from their
        // config prefix; AMP ENV calls setCardIdentity to override both.
        cardStyleKey = configPrefix.fromLastOccurrenceOf(".", false, false);
        cardTitle = cardStyleKey.toUpperCase().replace("ENV", "ENV ");

        addAndMakeVisible(enabledButton);
        addAndMakeVisible(enabledLabel);
        addAndMakeVisible(assignLabel);
        addAndMakeVisible(assignBox);
        if (amountKnob != nullptr)
        {
            addAndMakeVisible(*amountKnob);
        }
        if (amountLabel != nullptr)
        {
            amountLabel->setVisible(false);
        }
        if (amountValueLabel != nullptr)
        {
            baseAmountValueTextColour = amountValueLabel->findColour(juce::Label::textColourId);
            addAndMakeVisible(*amountValueLabel);
        }
        baseEnabledLabelTextColour = enabledLabel.findColour(juce::Label::textColourId);
        setMouseCursor(juce::MouseCursor::NormalCursor);
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

    if (enabledLabel.isVisible())
    {
        const auto pref = configPrefix + ".visual.";

        const auto textColour = uiConfig != nullptr ? uiConfig->getColour(pref + "onLabel.textColour", juce::Colour::fromRGB(232, 232, 232))
                                                    : juce::Colour::fromRGB(232, 232, 232);
        const auto fontSize = uiConfig != nullptr ? uiConfig->getFloat(pref + "onLabel.fontSize", 11.5f) : 11.5f;
        const auto text = uiConfig != nullptr ? uiConfig->getString(pref + "onLabel.text", "ON") : juce::String("ON");
        enabledLabel.setText(text, juce::dontSendNotification);
        enabledLabel.setColour(juce::Label::textColourId, textColour);
        enabledLabel.setFont(juce::FontOptions(fontSize));
        baseEnabledLabelTextColour = textColour;
    }

    // As above: cardInner's rows come from resized(), so a reload has to redo
    // the layout or the graph and the controls stay where the old config put
    // them.
    resized();
    repaint();
}

void EnvelopeComponent::refreshFromParameters()
{
    const auto forceEnabled = uiConfig != nullptr ? uiConfig->getBool(configPrefix + ".behavior.alwaysEnabled", false) : false;
    const auto a = attack.get();
    const auto d = decay.get();
    const auto s = sustain.get();
    const auto r = release.get();
    const auto nextEnabled = forceEnabled ? true : enabled.get();

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
        if (amountKnob != nullptr)
        {
            amountKnob->setEnabled(currentEnabled);
            amountKnob->setInterceptsMouseClicks(currentEnabled, currentEnabled);
            amountKnob->getProperties().set("knobBypassed", !currentEnabled);
            amountKnob->getProperties().set("psychedelicBypassGray", !currentEnabled);
        }
        if (amountValueLabel != nullptr)
        {
            amountValueLabel->setEnabled(currentEnabled);
            amountValueLabel->setColour(juce::Label::textColourId,
                                        currentEnabled ? baseAmountValueTextColour : juce::Colour::fromRGB(176, 176, 176));
            const auto amountValue = amountKnob != nullptr ? static_cast<float>(amountKnob->getValue()) : 0.0f;
            const auto amountPercent = static_cast<int>(std::lround(juce::jlimit(-1.0f, 1.0f, amountValue) * 100.0f));
            const auto amountPrefix = amountPercent > 0 ? juce::String("+") : juce::String();
            amountValueLabel->setText(amountPrefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
        }
        enabledLabel.setColour(juce::Label::textColourId,
                               currentEnabled ? baseEnabledLabelTextColour : juce::Colour::fromRGB(176, 176, 176));
        if (!currentEnabled)
        {
            hoverHandle = DragHandle::none;
            dragHandle = DragHandle::none;
            draggingSustainSegment = false;
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EnvelopeComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    repaint();
}

void EnvelopeComponent::paint(juce::Graphics& g)
{
    // Card and title owned here rather than by ModPanel. This is the same
    // layout call resized() makes, so the drawn graph and the draggable graph
    // are guaranteed to come from one set of bounds.
    layoutCardInner();

    const auto cardBounds = card.bounds();
    if (cardBounds.isEmpty())
    {
        return;
    }

    const auto title = cardTitle;
    if (currentEnabled)
    {
        card.draw(g, title);
    }
    else
    {
        card.drawInactive(g, title);
    }

    const auto effectiveAccent = currentEnabled ? accent : juce::Colour::fromRGBA(150, 150, 150, 180);
    juce::ignoreUnused(effectiveAccent);

    const auto geom = computeGeometry();
    const auto graphArea = juce::Rectangle<float>(geom.left - 6.0f,
                                                  geom.top - 5.0f,
                                                  (geom.right - geom.left) + 12.0f,
                                                  (geom.bottom - geom.top) + 10.0f);
    const auto graphCornerRadius = uiConfig != nullptr ? uiConfig->getFloat(configPrefix + ".visual.graph.cornerRadius", 7.0f) : 7.0f;
    const auto graphFillColour = uiConfig != nullptr ? uiConfig->getColour(configPrefix + ".visual.graph.fillColour", juce::Colour::fromRGB(14, 14, 18))
                                                     : juce::Colour::fromRGB(14, 14, 18);
    const auto graphFillAlpha = uiConfig != nullptr ? uiConfig->getInt(configPrefix + ".visual.graph.fillAlpha", 170) : 170;
    const auto graphStrokeThickness = uiConfig != nullptr ? uiConfig->getFloat(configPrefix + ".visual.graph.strokeThickness", 1.0f) : 1.0f;
    const auto graphStrokeAlphaEnabled = uiConfig != nullptr ? uiConfig->getInt(configPrefix + ".visual.graph.strokeAlphaEnabled", 82) : 82;
    const auto graphStrokeAlphaDisabled = uiConfig != nullptr ? uiConfig->getInt(configPrefix + ".visual.graph.strokeAlphaDisabled", 62) : 62;
    const auto graphStrokeColourEnabled = uiConfig != nullptr ? uiConfig->getColour(configPrefix + ".visual.graph.strokeColourEnabled", effectiveAccent)
                                                              : effectiveAccent;
    const auto graphStrokeColourDisabled = uiConfig != nullptr ? uiConfig->getColour(configPrefix + ".visual.graph.strokeColourDisabled", juce::Colour::fromRGB(136, 136, 136))
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

void EnvelopeComponent::setCardIdentity(juce::String styleKey, juce::String title)
{
    cardStyleKey = std::move(styleKey);
    cardTitle = std::move(title);
    resized();
    repaint();
}

juce::Rectangle<int> EnvelopeComponent::computeCardBounds() const
{
    auto cardArea = getLocalBounds().reduced(6, 6);

    const auto widthPercent = uiConfig != nullptr ? uiConfig->getFloat(configPrefix + ".layout.cardWidthPercent", 0.0f) : 0.0f;
    if (widthPercent > 0.0f)
    {
        const auto clampedPercent = juce::jlimit(0.1f, 1.0f, widthPercent);
        const auto cardWidth = juce::jlimit(120,
                                            cardArea.getWidth(),
                                            static_cast<int>(std::lround(static_cast<float>(cardArea.getWidth()) * clampedPercent)));
        return cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());
    }

    const auto targetCardWidth = uiConfig != nullptr ? uiConfig->getInt(configPrefix + ".layout.cardTargetWidth", 300) : 300;
    const auto cardWidth = juce::jmin(targetCardWidth, cardArea.getWidth());
    return cardArea.withSizeKeepingCentre(cardWidth, cardArea.getHeight());
}

void EnvelopeComponent::mouseMove(const juce::MouseEvent& event)
{
    if (!currentEnabled)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    hoverHandle = pickHandle(event.position, computeGeometry());
    setMouseCursor(hoverHandle != DragHandle::none ? juce::MouseCursor::PointingHandCursor
                                                    : juce::MouseCursor::NormalCursor);
    repaint();
}

void EnvelopeComponent::mouseExit(const juce::MouseEvent&)
{
    if (dragHandle == DragHandle::none)
    {
        hoverHandle = DragHandle::none;
        setMouseCursor(juce::MouseCursor::NormalCursor);
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
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    setMouseCursor(juce::MouseCursor::PointingHandCursor);

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
    setMouseCursor(hoverHandle != DragHandle::none ? juce::MouseCursor::PointingHandCursor
                                                    : juce::MouseCursor::NormalCursor);
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
    // The graph is the last row of cardInner - the only row, for AMP ENV.
    // Deriving it here a second time, by replaying the same sequence of
    // removeFromTop calls resized() used, is what let the drawn graph and the
    // draggable graph disagree.
    const auto graphLayout = inner.rowContent(inner.rowCount() - 1).toFloat();

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
    layoutCardInner();

    // AMP ENV is the deliberate exception: one full-size graph, no rows of
    // controls, so there is nothing to place here. `fullHeightGraph` is the
    // switch that already distinguished it, and it still is.
    if (isFullHeightGraph())
    {
        return;
    }

    using px3::ui::ControlShape;

    // Row 1: bypass and assign.
    {
        auto flex = inner.rowFlex(0);
        const auto gap = inner.rowGap(0);
        const auto row = inner.rowContent(0);
        const auto cellHeight = static_cast<float>(juce::jmax(1, row.getHeight()));

        flex.items.add(juce::FlexItem(44.0f, cellHeight).withMargin(gap));
        flex.items.add(juce::FlexItem(116.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0),
                                       { &enabledLabel, &enabledButton, nullptr,
                                         ControlShape::square, 14, 0, 22 },
                                       inner.rowControl(0));
        px3::ui::layoutLabelledControl(cell(1),
                                       { &assignLabel, &assignBox, nullptr,
                                         ControlShape::stretch, 14, 0, 24 },
                                       inner.rowControl(0));
    }

    // Row 2: the amount knob, which the mod envelopes have and AMP ENV does
    // not. It is knob-plus-readout with no label above, as it renders today.
    if (amountKnob != nullptr && amountLabel != nullptr && amountValueLabel != nullptr)
    {
        auto flex = inner.rowFlex(1);
        const auto gap = inner.rowGap(1);
        const auto row = inner.rowContent(1);

        flex.items.add(juce::FlexItem(84.0f, static_cast<float>(juce::jmax(1, row.getHeight())))
                           .withMargin(gap));
        flex.performLayout(row.toFloat());

        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { nullptr, amountKnob, amountValueLabel,
                                         ControlShape::square, 0, 20, 84 },
                                       inner.rowControl(1));
        amountLabel->setBounds(0, 0, 0, 0);
    }

    // Row 3 is the ADSR graph. computeGeometry() reads its bounds, which is
    // what keeps the drawing and the mouse hit-testing in agreement.
}

bool EnvelopeComponent::isFullHeightGraph() const
{
    return uiConfig != nullptr && uiConfig->getBool(configPrefix + ".behavior.fullHeightGraph", false);
}

void EnvelopeComponent::layoutCardInner()
{
    card.setStyleKey(cardStyleKey);
    card.setConfig(uiConfig);
    card.layout(computeCardBounds());

    inner.setStylePath("cards." + px3::ui::cardTypeKey(cardStyleKey) + ".cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(isFullHeightGraph() ? 1 : 3);
    inner.layout(card.contentBelowTitle());
}
