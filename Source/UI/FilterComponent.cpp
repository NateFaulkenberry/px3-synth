#include "FilterComponent.h"

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

void FilterComponent::layoutCardInner()
{
    card.setStyleKey("filter" + juce::String(instanceIndex));
    card.setConfig(uiConfig);
    card.layout(getLocalBounds());

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

    if (nextMode != lastModeIndex
        || std::abs(nextCutoff - lastCutoff) > 0.0001f
        || std::abs(nextRes - lastResonance) > 0.0001f
        || nextEnabled != currentEnabled)
    {
        currentEnabled = nextEnabled;
        lastModeIndex = nextMode;
        lastCutoff = nextCutoff;
        lastResonance = nextRes;
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
    g.setColour(juce::Colour::fromRGBA(220, 232, 252, currentEnabled ? 88 : 66));
    g.drawRoundedRectangle(graphRect, 4.0f, 1.0f);

    auto contentRect = graphRect;

    const auto left = contentRect.getX() + 4.0f;
    const auto right = contentRect.getRight() - 4.0f;
    const auto top = contentRect.getY() + 4.0f;
    const auto bottom = contentRect.getBottom() - 4.0f;
    const auto midY = (top + bottom) * 0.5f;

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 36));
    g.drawLine(left, midY, right, midY, 1.0f);

    const auto idx = juce::jlimit(0, 6, mode.getIndex());
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
