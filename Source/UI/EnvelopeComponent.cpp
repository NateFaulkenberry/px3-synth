#include "ParameterKnob.h"
#include "EnvelopeComponent.h"

#include "BypassButton.h"
#include "CardInner.h"

#include "UIConfig.h"

#include <cmath>

EnvelopeComponent::EnvelopeComponent(juce::AudioParameterFloat& attackIn,
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
                                                                         const juce::String& configPrefixIn)
        : attack(attackIn),
            decay(decayIn),
            sustain(sustainIn),
            release(releaseIn),
            enabled(enabledIn),
            enabledButton(enabledButtonIn),
            assignLabel(assignLabelIn),
            assignBox(assignBoxIn),
            amountKnob(amountKnobIn),
            amountLabel(amountLabelIn),
            amountValueLabel(amountValueLabelIn),
            accent(accentIn),
            configPrefix(configPrefixIn)
{
    // The graph is a breakpoint editor now. It draws the same curve the DSP
    // evaluates and reports every edit; this component still owns the card, the
    // labels and the assignment box around it.
    addAndMakeVisible(breakpointEditor);
    breakpointEditor.setConfigPrefix(configPrefixIn);

    // The card's accent, handed over here as well as in setAccentColour - that
    // only fires on a change, so a card whose colour never changes left the
    // editor on its own default and the curve came out the wrong blue.
    breakpointEditor.setAccentColour(accentIn);
    breakpointEditor.onEnvelopeChanged = [this](const px3::BreakpointEnvelope& edited)
    {
        if (onEnvelopeEdited != nullptr)
        {
            onEnvelopeEdited(edited);
        }
    };

        // The mod envelopes' card key and title come straight from their
        // config prefix; AMP ENV calls setCardIdentity to override both.
        cardStyleKey = configPrefix.fromLastOccurrenceOf(".", false, false);
        cardTitle = cardStyleKey.toUpperCase().replace("ENV", "ENV ");

        addAndMakeVisible(enabledButton);
        addAndMakeVisible(assignLabel);
        addAndMakeVisible(assignBox);
        if (amountKnob != nullptr)
        {
            addAndMakeVisible(*amountKnob);
        }
        if (amountLabel != nullptr)
        {
            addAndMakeVisible(*amountLabel);
        }
        if (amountValueLabel != nullptr)
        {
            baseAmountValueTextColour = amountValueLabel->findColour(juce::Label::textColourId);
            addAndMakeVisible(*amountValueLabel);
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
    refreshFromParameters();
}

void EnvelopeComponent::setAccentColour(juce::Colour accentIn)
{
    breakpointEditor.setAccentColour(accentIn);
    accent = accentIn;
    repaint();
}

void EnvelopeComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    breakpointEditor.setUIConfig(configIn);
    uiConfig = std::move(configIn);

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
        breakpointEditor.setEnvelopeEnabled(currentEnabled);
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
        // Greyed with the rest of the card when the envelope is bypassed, by
        // the same two properties the AMOUNT knob uses - otherwise a bypassed
        // card would carry four knobs still drawn as if they were live.
        if (adsrKnobsBuilt)
        {
            for (auto& entry : adsrKnobs)
            {
                entry.knob.setEnabled(currentEnabled);
                entry.knob.setInterceptsMouseClicks(currentEnabled, currentEnabled);
                entry.knob.getProperties().set("knobBypassed", ! currentEnabled);
                entry.knob.getProperties().set("psychedelicBypassGray", ! currentEnabled);
                entry.label.setEnabled(currentEnabled);
                entry.readout.setEnabled(currentEnabled);
                entry.readout.setColour(juce::Label::textColourId,
                                        currentEnabled
                                            ? juce::Colour::fromRGB(218, 218, 228)
                                            : juce::Colour::fromRGB(176, 176, 176));
            }
        }

        if (!currentEnabled)
        {
            hoverHandle = DragHandle::none;
            dragHandle = DragHandle::none;
            draggingSustainSegment = false;
        }
        repaint();
    }
    refreshAdsrReadouts();
}

void EnvelopeComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);

    // resized(), not just repaint(). This changes the card's geometry, and
    // paint() recomputes it - so repainting alone left the breakpoint editor
    // sitting where the PREVIOUS geometry put it while the frame was drawn
    // somewhere else, which is how the curve ended up outside the background.
    resized();
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

    // The graph's fill, frame and grid all belong to the breakpoint editor
    // now. Drawing them here as well meant one rectangle computed in two
    // places, which is how the curve ended up outside its own background.
    juce::ignoreUnused(graphFillColour, graphFillAlpha, graphStrokeColour,
                       graphStrokeAlpha, graphCornerRadius, graphStrokeThickness,
                       graphArea);

    // The curve, the breakpoints and the handles belong to the editor sitting on
    // top of this. Drawing the old fixed-handle ADSR here as well would leave
    // two envelopes on screen disagreeing with each other.
    return;

    for (int i = 0; i <= 4; ++i)
    {
        const auto y = juce::jmap(static_cast<float>(i), 0.0f, 4.0f, geom.top, geom.bottom);
        g.drawHorizontalLine(static_cast<int>(std::lround(y)), geom.left, geom.right);
    }

    g.setColour(juce::Colour::fromRGBA(210, 210, 210, 75));
    g.setFont(juce::FontOptions(9.0f));
    // Inside the graph, not beside it. These used to be drawn 34px to the LEFT
    // of the graph box, which put them outside the row entirely and over the
    // card behind it - and it stopped the graph from using its full width.
    const auto marker = [&g, &geom](const char* text, float y)
    {
        g.drawText(text,
                   juce::Rectangle<int>(static_cast<int>(geom.left) + 4,
                                        static_cast<int>(y) - 6,
                                        34, 12),
                   juce::Justification::centredLeft);
    };
    marker("100%", geom.top + 6.0f);
    marker("50%", (geom.top + geom.bottom) * 0.5f);
    marker("0%", geom.bottom - 6.0f);

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


void EnvelopeComponent::mouseMove(const juce::MouseEvent& event)
{
    hoverHandle = currentEnabled ? pickHandle(event.position, computeGeometry())
                                 : DragHandle::none;
    updateCursorFor(event.position);

    if (!currentEnabled)
    {
        return;
    }

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
        updateCursorFor(event.position);
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

void EnvelopeComponent::mouseUp(const juce::MouseEvent& event)
{
    // Clicking the card background toggles power - but only away from the
    // graph, which is draggable, and never on AMP ENV, which is declared
    // alwaysEnabled and has no meaningful off state.
    const auto forceEnabled = uiConfig != nullptr
                                  ? uiConfig->getBool(configPrefix + ".behavior.alwaysEnabled", false)
                                  : false;
    // The GRAPH's row, which is no longer the last one now that the knobs sit
    // below it. Asking for the last row made a click on the graph read as a
    // click on the card background - which toggles the envelope off.
    const auto onGraph = inner.rowContent(graphRowIndex()).contains(event.getPosition());

    if (! forceEnabled && dragHandle == DragHandle::none && ! onGraph
        && px3::ui::isCardBackgroundToggleClick(event))
    {
        enabledButton.setToggleState(! enabledButton.getToggleState(), juce::sendNotification);
    }

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
    updateCursorFor(event.position);
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
    // The graph's own row - the last one, or the one before the knobs.
    // Deriving it here a second time, by replaying the same sequence of
    // removeFromTop calls resized() used, is what let the drawn graph and the
    // draggable graph disagree.
    const auto graphLayout = inner.rowContent(graphRowIndex()).toFloat();

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

juce::Rectangle<int> EnvelopeComponent::debugGraphFrameBounds() const
{
    return graphBounds();
}

juce::Rectangle<int> EnvelopeComponent::graphBounds() const
{
    // One expression, used by resized() to place the editor and by paint() to
    // check it - they were the same arithmetic written twice, which is how a
    // frame and the thing inside it drift apart.
    const auto geom = computeGeometry();
    auto bounds = juce::Rectangle<float>(geom.left - 6.0f,
                                         geom.top - 5.0f,
                                         (geom.right - geom.left) + 12.0f,
                                         (geom.bottom - geom.top) + 10.0f).toNearestInt();

    // With knobs below, the graph stops short of its row so the two are not
    // hard against each other. The row's own gap and padding cannot do this:
    // the editor fills its row, and the knob stack is centred in the next one,
    // so both only move the caption by a fraction of what they are given.
    if (adsrKnobsWanted())
    {
        const auto gap = uiConfig != nullptr
                           ? uiConfig->getInt(configPrefix + ".visual.graph.bottomGap", 12)
                           : 12;
        bounds.setHeight(juce::jmax(1, bounds.getHeight() - gap));
    }

    return bounds;
}

void EnvelopeComponent::setShapedEnvelope(const px3::BreakpointEnvelope& envelope)
{
    breakpointEditor.setEnvelope(envelope);
}

void EnvelopeComponent::resized()
{
    layoutCardInner();

    // AFTER layoutCardInner, not before it. computeGeometry reads
    // inner.rowContent, which only holds the right values once the card has
    // been laid out - positioning the editor first gave it the PREVIOUS
    // layout's graph rectangle, so the curve drew outside the frame.
    breakpointEditor.setBounds(graphBounds());

    buildAdsrKnobs();
    layoutAdsrKnobs();

    // AMP ENV is the deliberate exception: one full-size graph, no rows of
    // controls above it, so there is nothing further to place. `fullHeightGraph`
    // is the switch that already distinguished it, and it still is.
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

        flex.items.add(juce::FlexItem(116.0f, cellHeight).withMargin(gap));
        flex.performLayout(row.toFloat());

        const auto cell = [&flex](int i) { return flex.items.getReference(i).currentBounds.toNearestInt(); };
        px3::ui::layoutLabelledControl(cell(0),
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

        // Same label height, readout height and cap as the LFO knobs, so the
        // two card families read as one family.
        px3::ui::layoutLabelledControl(flex.items.getReference(0).currentBounds.toNearestInt(),
                                       { amountLabel, amountKnob, amountValueLabel,
                                         ControlShape::square, 16, 20, 84 },
                                       inner.rowControl(1));
    }

    // Row 3 is the ADSR graph. computeGeometry() reads its bounds, which is
    // what keeps the drawing and the mouse hit-testing in agreement.
}

void EnvelopeComponent::setAdsrKnobsVisible(bool shouldShow)
{
    // Asked for in code by whoever owns the card, not read from UIConfig.
    // Whether a row of controls EXISTS decides how many rows cardInner has, and
    // a card whose row count depends on a file that may not have loaded yet is
    // a card that lays itself out differently depending on timing. The config
    // still says how tall the row is.
    if (showAdsrKnobs == shouldShow) { return; }

    showAdsrKnobs = shouldShow;
    resized();
    repaint();
}

EnvelopeComponent::~EnvelopeComponent()
{
    for (auto& entry : adsrKnobs)
    {
        entry.knob.setLookAndFeel(nullptr);
    }
}

void EnvelopeComponent::setKnobLookAndFeel(juce::LookAndFeel* lookAndFeel)
{
    knobLookAndFeel = lookAndFeel;

    for (auto& entry : adsrKnobs)
    {
        if (adsrKnobsBuilt) { entry.knob.setLookAndFeel(knobLookAndFeel); }
    }
}

bool EnvelopeComponent::adsrKnobsWanted() const
{
    return showAdsrKnobs;
}

int EnvelopeComponent::graphRowIndex() const
{
    // The knobs take the last row when they are there, so the graph is the one
    // before it. Both cards ask this rather than assuming, because AMP ENV and
    // ENV 1-3 have different numbers of rows above the graph.
    return juce::jmax(0, inner.rowCount() - (adsrKnobsWanted() ? 2 : 1));
}

void EnvelopeComponent::buildAdsrKnobs()
{
    if (adsrKnobsBuilt || ! adsrKnobsWanted()) { return; }

    juce::AudioParameterFloat* params[4] = { &attack, &decay, &sustain, &release };
    const char* names[4] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };

    for (int i = 0; i < 4; ++i)
    {
        auto& entry = adsrKnobs[static_cast<std::size_t>(i)];

        // Styled exactly as the AMOUNT knob beside them, which is the same
        // styling every other knob in the plugin carries: the shared rotary
        // look-and-feel, a chip caption above and a plain readout below.
        entry.knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        entry.knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        if (knobLookAndFeel != nullptr) { entry.knob.setLookAndFeel(knobLookAndFeel); }
        addAndMakeVisible(entry.knob);

        entry.label.setText(names[i], juce::dontSendNotification);
        entry.label.setJustificationType(juce::Justification::centred);
        entry.label.setFont(juce::FontOptions(11.0f));
        entry.label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        entry.label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        entry.label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(entry.label);

        entry.readout.setJustificationType(juce::Justification::centred);
        entry.readout.setFont(juce::FontOptions(11.0f));
        entry.readout.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
        entry.readout.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        entry.readout.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(entry.readout);

        // The attachment carries the knob to the parameter. The parameter
        // reaching the SHAPE is the card's job, in refreshFromParameters,
        // because that is where the curves the knob must not straighten live.
        entry.attachment = px3::ui::makeParameterKnobAttachment(*params[i], entry.knob);
    }

    adsrKnobsBuilt = true;
    refreshAdsrReadouts();
}

void EnvelopeComponent::refreshAdsrReadouts()
{
    if (! adsrKnobsBuilt) { return; }

    // Times in the unit that reads without a decimal point at the value's own
    // size, and the sustain as a percentage, which is what it is.
    const auto asTime = [](float seconds)
    {
        return seconds < 1.0f ? juce::String(juce::roundToInt(seconds * 1000.0f)) + " ms"
                              : juce::String(seconds, 2) + " s";
    };

    adsrKnobs[0].readout.setText(asTime(attack.get()), juce::dontSendNotification);
    adsrKnobs[1].readout.setText(asTime(decay.get()), juce::dontSendNotification);
    adsrKnobs[2].readout.setText(juce::String(juce::roundToInt(sustain.get() * 100.0f)) + "%",
                                 juce::dontSendNotification);
    adsrKnobs[3].readout.setText(asTime(release.get()), juce::dontSendNotification);
}

void EnvelopeComponent::layoutAdsrKnobs()
{
    if (! adsrKnobsBuilt) { return; }

    using px3::ui::ControlShape;

    const auto row = inner.rowCount() - 1;
    auto flex = inner.rowFlex(row);
    const auto gap = inner.rowGap(row);
    const auto content = inner.rowContent(row);
    const auto cellHeight = static_cast<float>(juce::jmax(1, content.getHeight()));
    const auto cellWidth = static_cast<float>(juce::jmax(1, content.getWidth() / 4));

    for (int i = 0; i < 4; ++i)
    {
        flex.items.add(juce::FlexItem(cellWidth, cellHeight).withMargin(gap));
    }
    flex.performLayout(content.toFloat());

    for (int i = 0; i < 4; ++i)
    {
        auto& entry = adsrKnobs[static_cast<std::size_t>(i)];
        px3::ui::layoutLabelledControl(
            flex.items.getReference(i).currentBounds.toNearestInt(),
            { &entry.label, &entry.knob, &entry.readout, ControlShape::square, 12, 12, 64 },
            inner.rowControl(row));
    }
}

bool EnvelopeComponent::isFullHeightGraph() const
{
    return uiConfig != nullptr && uiConfig->getBool(configPrefix + ".behavior.fullHeightGraph", false);
}

void EnvelopeComponent::updateCursorFor(juce::Point<float> position)
{
    const auto graph = inner.rowContent(inner.rowCount() - 1);

    if (graph.contains(position.toInt()))
    {
        // Inside the graph only the segment lines and their points are
        // draggable. The field behind them does nothing, so it takes the plain
        // arrow rather than claiming to be interactive.
        const auto handle = currentEnabled ? pickHandle(position, computeGeometry())
                                           : DragHandle::none;
        setMouseCursor(handle != DragHandle::none ? juce::MouseCursor::PointingHandCursor
                                                  : juce::MouseCursor::NormalCursor);
        return;
    }

    // Outside the graph: a mod envelope's card background toggles its bypass,
    // but AMP ENV is declared alwaysEnabled and has nothing to toggle.
    setMouseCursor(isFullHeightGraph() ? juce::MouseCursor::NormalCursor
                                       : juce::MouseCursor::PointingHandCursor);
}

void EnvelopeComponent::layoutCardInner()
{
    card.setStyleKey(cardStyleKey);
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

    // The ADSR curve and its handles take the card's identity colour, like the
    // wave graphs elsewhere. Without this the graph kept the accent passed at
    // construction and stayed green after the mod envelopes went purple.
    accent = card.style().border.colour;

    // Handed to the editor HERE, where it is derived, rather than from the
    // constructor or setAccentColour. The card takes its identity colour from
    // its own style, so the accent passed in at construction is not the one it
    // draws with - which is exactly why the curve came out the wrong blue while
    // the card frame was purple.
    breakpointEditor.setAccentColour(accent);

    // The power glyph lights in this card's own identity colour.

    if (auto* power = dynamic_cast<px3::ui::BypassButton*>(&enabledButton))

    {

        power->setAccentColour(card.style().border.colour);

    }


    inner.setStylePath("cards." + px3::ui::cardTypeKey(cardStyleKey) + ".cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount((isFullHeightGraph() ? 1 : 3) + (adsrKnobsWanted() ? 1 : 0));
    inner.layout(card.contentBelowTitle());

    // The power toggle is pinned to cardInner's corner, outside the flex flow,
    // so it stays put no matter what the first row contains.
    enabledButton.setBounds(inner.powerBounds());
}
