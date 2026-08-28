#include "FilterComponent.h"

#include "../DSP/FilterMode.h"

#include "BypassButton.h"

#include "UIConfig.h"

#include <cmath>

FilterComponent::FilterComponent(juce::AudioParameterFloat& cutoffIn,
                                 juce::AudioParameterFloat& resonanceIn,
                                 juce::AudioParameterChoice& modeIn,
                       juce::AudioParameterBool& enabledIn,
                                 juce::String instanceLabelIn,
                                 juce::Colour accentIn)
    : cutoff(cutoffIn),
      resonance(resonanceIn),
      mode(modeIn),
    enabled(enabledIn),
    instanceLabel(instanceLabelIn),
      accent(accentIn)
{
    // The card background toggles this section, so the pointer says it is
    // clickable. Child controls carry their own cursors.
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    refreshFromParameters();
}

void FilterComponent::setAccentColour(juce::Colour accentIn)
{
    accent = accentIn;
    repaint();
}

void FilterComponent::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);
    repaint();
}

void FilterComponent::mouseUp(const juce::MouseEvent& event)
{
    // The response graph is a display, not a switch - same rule as the other
    // cards' wave graphs.
    if (inner.rowContent(2).contains(event.getPosition()))
    {
        return;
    }

    if (px3::ui::isCardBackgroundToggleClick(event) && onBackgroundClick != nullptr)
    {
        onBackgroundClick();
    }
}

void FilterComponent::mouseMove(const juce::MouseEvent& event)
{
    // The wave graph is a display, not a control, so it does not take the
    // pointer that marks the rest of the card as clickable.
    setMouseCursor(inner.rowContent(2).contains(event.getPosition())
                       ? juce::MouseCursor::NormalCursor
                       : juce::MouseCursor::PointingHandCursor);
}

void FilterComponent::layoutCardInner()
{
    card.setStyleKey("filter" + juce::String(instanceIndex));
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());
    // The wave graph is drawn in this card's identity colour, so a card
    // recoloured in UIConfig recolours its graph with it rather than
    // keeping a group accent baked in at construction.
    accent = card.style().border.colour;


    inner.setStylePath("cards.filter.cardInner");
    inner.setConfig(uiConfig);
    inner.setRowCount(3);
    inner.layout(card.contentBelowTitle());
}

juce::Rectangle<int> FilterComponent::rowBounds(int index) const
{
    return inner.rowContent(index);
}

juce::FlexBox FilterComponent::rowFlex(int index) const
{
    return inner.rowFlex(index);
}

void FilterComponent::setCombParameters(juce::AudioParameterFloat& tune,
                                        juce::AudioParameterFloat& decay,
                                        juce::AudioParameterFloat& damping)
{
    combTune = &tune;
    combDecay = &decay;
    combDamping = &damping;
    repaint();
}

juce::Rectangle<int> FilterComponent::powerBounds() const
{
    return inner.powerBounds();
}

juce::Colour FilterComponent::cardAccentColour() const
{
    return card.style().border.colour;
}

const px3::ui::ControlStyle& FilterComponent::rowControl(int index) const
{
    return inner.rowControl(index);
}

juce::FlexItem::Margin FilterComponent::rowGap(int index) const
{
    return inner.rowGap(index);
}

void FilterComponent::refreshFromParameters()
{
    const auto nextMode = mode.getIndex();
    const auto nextCutoff = cutoff.get();
    const auto nextRes = resonance.get();
    const auto nextEnabled = enabled.get();

    // The comb draws its curve from its own parameters, so they belong in the
    // change test alongside cutoff and resonance.
    const auto nextCombTune = combTune != nullptr ? combTune->get() : 0.0f;
    const auto nextCombDecay = combDecay != nullptr ? combDecay->get() : 0.0f;
    const auto nextCombDamping = combDamping != nullptr ? combDamping->get() : 0.0f;

    if (nextMode != lastModeIndex
        || std::abs(nextCutoff - lastCutoff) > 0.0001f
        || std::abs(nextRes - lastResonance) > 0.0001f
        || std::abs(nextCombTune - lastCombTune) > 0.0001f
        || std::abs(nextCombDecay - lastCombDecay) > 0.0001f
        || std::abs(nextCombDamping - lastCombDamping) > 0.0001f
        || nextEnabled != currentEnabled)
    {
        currentEnabled = nextEnabled;
        lastModeIndex = nextMode;
        lastCutoff = nextCutoff;
        lastResonance = nextRes;
        lastCombTune = nextCombTune;
        lastCombDecay = nextCombDecay;
        lastCombDamping = nextCombDamping;
        repaint();
    }
}

void FilterComponent::setInstanceIndex(int oneBasedIndex)
{
    const auto clamped = juce::jlimit(1, 8, oneBasedIndex);
    if (instanceIndex != clamped)
    {
        instanceIndex = clamped;
        card.setStyleKey("filter" + juce::String(instanceIndex));
        repaint();
    }
}

void FilterComponent::setPanelContentBounds(juce::Rectangle<int> panelContent)
{
    card.setPanelContentBounds(panelContent);
    repaint();
}

void FilterComponent::paint(juce::Graphics& g)
{
    // The card and its title belong here. FltPanel used to draw the title from
    // the panel, into this component's bounds - the same parent-owns-child's-
    // pixels mistake that left stale outlines behind elsewhere.
    layoutCardInner();

    const auto title = "FILTER " + juce::String(instanceIndex);
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

    const auto graphRow = rowBounds(2);
    auto graphRect = graphRow.isEmpty() ? getLocalBounds().toFloat().reduced(2.0f) : graphRow.toFloat();
    if (graphRect.getWidth() < 12.0f || graphRect.getHeight() < 12.0f)
    {
        return;
    }

    g.setColour(juce::Colour::fromRGBA(20, 20, 20, 140));
    g.fillRoundedRectangle(graphRect, 4.0f);
    // The frame takes the card's identity colour, like every other card's
    // graph. It was a fixed pale blue, which is what made a red filter card
    // sit inside a blue-edged graph.
    g.setColour(effectiveAccent.withAlpha(currentEnabled ? 0.34f : 0.26f));
    g.drawRoundedRectangle(graphRect, 4.0f, 1.0f);

    auto contentRect = graphRect;

    const auto left = contentRect.getX() + 4.0f;
    const auto right = contentRect.getRight() - 4.0f;
    const auto top = contentRect.getY() + 4.0f;
    const auto bottom = contentRect.getBottom() - 4.0f;
    const auto midY = (top + bottom) * 0.5f;

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 36));
    g.drawLine(left, midY, right, midY, 1.0f);

    const auto idx = juce::jlimit(0, px3::filterModeMaxIndex, mode.getIndex());

    // ---- comb -------------------------------------------------------------
    // The comb's response is a series of peaks at multiples of its tuning, not
    // a single corner, so it gets its own curve rather than being forced
    // through the biquad shapes below. Drawing it from the real parameters is
    // the point: the graph is how you see what Tune and Decay are doing.
    if (px3::isCombMode(idx) && combTune != nullptr && combDecay != nullptr && combDamping != nullptr)
    {
        // The graph's horizontal axis is the same logarithmic sweep the biquad
        // curves use, so a comb tooth and a cutoff corner at the same frequency
        // land in the same place.
        const auto tuneHz = combTune->get();
        const auto decay = combDecay->get();
        const auto damping = juce::jlimit(0.0f, 1.0f, combDamping->get());

        // Feedback gain from decay, the same rule the resonator uses - a longer
        // decay is a higher gain, which is a sharper, taller tooth.
        const auto loopSeconds = 1.0f / juce::jmax(1.0f, tuneHz);
        const auto gain = juce::jlimit(0.0f, 0.995f,
                                       std::pow(10.0f, -3.0f * loopSeconds / juce::jmax(0.02f, decay)));

        juce::Path combPath;
        combPath.startNewSubPath(left, bottom);

        const auto graphWidth = juce::jmax(1.0f, right - left);
        for (int s = 0; s <= 220; ++s)
        {
            const auto t = static_cast<float>(s) / 220.0f;
            // 20 Hz to 20 kHz, logarithmically.
            const auto hz = 20.0f * std::pow(1000.0f, t);

            // |1 / (1 - g e^-jwD)| normalised: peaks where hz is a multiple of
            // the tuning, troughs between. Damping rolls the peaks off with
            // frequency, which is what the damping control does to the tail.
            const auto phase = juce::MathConstants<float>::twoPi * hz / juce::jmax(1.0f, tuneHz);
            const auto rolloff = 1.0f - damping * juce::jlimit(0.0f, 1.0f, hz / 8000.0f);
            const auto effectiveGain = juce::jlimit(0.0f, 0.995f, gain * rolloff);

            const auto denom = 1.0f + effectiveGain * effectiveGain
                               - 2.0f * effectiveGain * std::cos(phase);
            const auto magnitude = 1.0f / std::sqrt(juce::jmax(1.0e-4f, denom));

            // Compressed, so a near-self-oscillating tooth stays on the graph.
            const auto shaped = juce::jlimit(0.0f, 1.0f, std::log10(1.0f + magnitude) * 0.62f);
            combPath.lineTo(left + t * graphWidth, bottom - shaped * (bottom - top));
        }

        g.setColour(effectiveAccent.withAlpha(currentEnabled ? 0.95f : 0.45f));
        g.strokePath(combPath, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        return;
    }

    const auto cutoffPos = juce::jlimit(0.08f, 0.92f, cutoffNorm());
    const auto resBoost = juce::jlimit(0.0f, 1.0f, resonanceNorm());

    juce::Path response;
    response.startNewSubPath(left, bottom);

    const auto width = juce::jmax(1.0f, right - left);
    for (int s = 1; s <= 56; ++s)
    {
        const auto t = static_cast<float>(s) / 56.0f;
        const auto x = left + t * width;

        float yNorm = 0.5f;
        if (idx == 0 || idx == 1)
        {
            const auto slope = idx == 0 ? 1.4f : 2.3f;
            const auto local = clamp01(t / cutoffPos);
            yNorm = 1.0f - std::pow(local, slope);
        }
        else if (idx == 2 || idx == 3)
        {
            const auto slope = idx == 2 ? 1.4f : 2.3f;
            const auto local = clamp01((t - cutoffPos) / juce::jmax(0.06f, 1.0f - cutoffPos));
            yNorm = std::pow(local, slope);
        }
        else if (idx == 4)
        {
            const auto spread = juce::jmax(0.08f, 0.32f - resBoost * 0.16f);
            const auto d = std::abs(t - cutoffPos) / spread;
            yNorm = juce::jmax(0.0f, 1.0f - d * d);
        }
        else if (idx == 5)
        {
            const auto spread = juce::jmax(0.08f, 0.32f - resBoost * 0.12f);
            const auto d = std::abs(t - cutoffPos) / spread;
            yNorm = 0.12f + 0.88f * juce::jlimit(0.0f, 1.0f, d * d);
        }
        else
        {
            yNorm = 0.55f;
        }

        if (idx == 0 || idx == 1 || idx == 2 || idx == 3)
        {
            const auto d = std::abs(t - cutoffPos);
            const auto peak = std::exp(-(d * d) / 0.0036f) * (0.12f + resBoost * 0.34f);
            yNorm += peak;
        }

        const auto y = bottom - juce::jlimit(0.0f, 1.0f, yNorm) * (bottom - top);
        response.lineTo(x, y);
    }

    g.setColour(effectiveAccent.brighter(0.15f).withAlpha(currentEnabled ? 1.0f : 0.6f));
    g.strokePath(response, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

}

float FilterComponent::clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

float FilterComponent::cutoffNorm() const
{
    return cutoff.convertTo0to1(cutoff.get());
}

float FilterComponent::resonanceNorm() const
{
    const auto range = resonance.getNormalisableRange();
    const auto span = juce::jmax(0.0001f, range.end - range.start);
    return clamp01((resonance.get() - range.start) / span);
}
