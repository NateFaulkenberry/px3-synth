#include "PerformanceControls.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPanelGap = 14.0f;
constexpr float kTrackWidth = 10.0f;
constexpr float kHandleRadius = 7.0f;

inline float clamp01(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float easeAmount(float value)
{
    const auto t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

void drawWheel(juce::Graphics& g,
               const juce::String& title,
               const juce::Colour& accent,
               juce::Rectangle<float> panel,
               juce::Rectangle<float> track,
               float normalizedValue,
               bool hasCenter,
               float glow)
{
    g.setColour(juce::Colour::fromRGBA(17, 17, 17, 220));
    g.fillRoundedRectangle(panel, 8.0f);

    const auto labelArea = panel.removeFromTop(18.0f);

    g.setColour(juce::Colour::fromRGB(228, 228, 228));
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText(title, labelArea.toNearestInt(), juce::Justification::centred, false);

    g.setColour(accent.withAlpha(0.22f + 0.28f * glow));
    g.fillRoundedRectangle(track, 8.0f);

    g.setColour(accent.withAlpha(0.36f + 0.42f * glow));
    g.drawRoundedRectangle(track, 8.0f, 1.0f);

    const auto topY = track.getY() + kHandleRadius;
    const auto bottomY = track.getBottom() - kHandleRadius;
    float handleY = bottomY;

    if (hasCenter)
    {
        const auto centerY = (topY + bottomY) * 0.5f;
        handleY = centerY - normalizedValue * (bottomY - topY) * 0.5f;

        g.setColour(accent.withAlpha(0.30f));
        g.drawLine(track.getX() - 6.0f, centerY, track.getRight() + 6.0f, centerY, 1.0f);
    }
    else
    {
        handleY = bottomY - normalizedValue * (bottomY - topY);
    }

    const auto handleX = track.getCentreX();
    const auto glowAlpha = 0.14f + 0.56f * glow;

    g.setColour(accent.withAlpha(glowAlpha * 0.50f));
    g.fillEllipse(handleX - 15.0f, handleY - 15.0f, 30.0f, 30.0f);

    g.setColour(accent.withAlpha(glowAlpha));
    g.fillEllipse(handleX - 11.0f, handleY - 11.0f, 22.0f, 22.0f);

    juce::ColourGradient handleGradient(accent.brighter(0.35f + 0.25f * glow),
                                        handleX,
                                        handleY - kHandleRadius,
                                        accent.darker(0.45f),
                                        handleX,
                                        handleY + kHandleRadius,
                                        false);
    g.setGradientFill(handleGradient);
    g.fillEllipse(handleX - kHandleRadius, handleY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<juce::uint8>(120 + 80.0f * glow)));
    g.drawEllipse(handleX - kHandleRadius, handleY - kHandleRadius, kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);

}
}

PerformanceControls::PerformanceControls()
{
    startTimerHz(60);
}

void PerformanceControls::setControllerState(float pitchBendNormalized,
                                             float modWheelNormalized,
                                             float pitchActivity,
                                             float modActivity)
{
    targetPitch = clampPitch(pitchBendNormalized);
    targetMod = clampMod(modWheelNormalized);

    const auto pitchUse = clamp01(std::abs(targetPitch));
    const auto modUse = clamp01(targetMod);

    targetPitchGlow = clamp01(0.20f * pitchUse + 0.80f * clamp01(pitchActivity));
    targetModGlow = clamp01(0.30f * modUse + 0.70f * clamp01(modActivity));
}

void PerformanceControls::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(20, 20, 20));

    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
    g.drawRoundedRectangle(bounds, 10.0f, 1.0f);

    drawWheel(g,
              "PITCH",
              juce::Colour::fromRGB(82, 155, 255),
              getPitchVisual().panel,
              getPitchVisual().track,
              visualPitch,
              true,
              easeAmount(visualPitchGlow));

    drawWheel(g,
              "MOD",
              juce::Colour::fromRGB(232, 84, 78),
              getModVisual().panel,
              getModVisual().track,
              visualMod,
              false,
              easeAmount(visualModGlow));
}

void PerformanceControls::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.position;

    if (getPitchVisual().panel.contains(point))
    {
        activeControl = ActiveControl::pitch;
    }
    else if (getModVisual().panel.contains(point))
    {
        activeControl = ActiveControl::mod;
    }
    else
    {
        activeControl = ActiveControl::none;
    }

    updateFromMousePosition(point);
}

void PerformanceControls::mouseDrag(const juce::MouseEvent& event)
{
    updateFromMousePosition(event.position);
}

void PerformanceControls::mouseUp(const juce::MouseEvent&)
{
    if (activeControl == ActiveControl::pitch)
    {
        if (onPitchBendChanged)
        {
            onPitchBendChanged(0.0f);
        }

        if (onPitchBendGestureEnded)
        {
            onPitchBendGestureEnded();
        }
    }

    activeControl = ActiveControl::none;
}

void PerformanceControls::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (getPitchVisual().panel.contains(event.position))
    {
        if (onPitchBendChanged)
        {
            onPitchBendChanged(0.0f);
        }

        if (onPitchBendGestureEnded)
        {
            onPitchBendGestureEnded();
        }
    }
    else if (getModVisual().panel.contains(event.position))
    {
        if (onModWheelChanged)
        {
            onModWheelChanged(0.0f);
        }
    }
}

void PerformanceControls::timerCallback()
{
    visualPitch += (targetPitch - visualPitch) * 0.28f;
    visualMod += (targetMod - visualMod) * 0.24f;
    visualPitchGlow += (targetPitchGlow - visualPitchGlow) * 0.16f;
    visualModGlow += (targetModGlow - visualModGlow) * 0.14f;

    repaint();
}

void PerformanceControls::updateFromMousePosition(juce::Point<float> position)
{
    if (activeControl == ActiveControl::pitch)
    {
        const auto track = getPitchVisual().track;
        const auto topY = track.getY() + kHandleRadius;
        const auto bottomY = track.getBottom() - kHandleRadius;
        const auto denom = juce::jmax(1.0f, bottomY - topY);
        const auto normalized = juce::jlimit(-1.0f, 1.0f, ((topY + bottomY) * 0.5f - position.y) / (denom * 0.5f));

        if (onPitchBendChanged)
        {
            onPitchBendChanged(normalized);
        }
    }
    else if (activeControl == ActiveControl::mod)
    {
        const auto track = getModVisual().track;
        const auto topY = track.getY() + kHandleRadius;
        const auto bottomY = track.getBottom() - kHandleRadius;
        const auto denom = juce::jmax(1.0f, bottomY - topY);
        const auto normalized = juce::jlimit(0.0f, 1.0f, (bottomY - position.y) / denom);

        if (onModWheelChanged)
        {
            onModWheelChanged(normalized);
        }
    }
}

PerformanceControls::WheelVisual PerformanceControls::getPitchVisual() const
{
    auto area = getLocalBounds().toFloat().reduced(5.0f);
    const auto panelWidth = (area.getWidth() - kPanelGap) * 0.5f;

    WheelVisual visual;
    visual.panel = area.removeFromLeft(panelWidth);
    const auto trackArea = visual.panel.reduced(12.0f, 22.0f);
    visual.track = juce::Rectangle<float>(trackArea.getCentreX() - kTrackWidth * 0.5f,
                                          trackArea.getY(),
                                          kTrackWidth,
                                          trackArea.getHeight());
    return visual;
}

PerformanceControls::WheelVisual PerformanceControls::getModVisual() const
{
    auto area = getLocalBounds().toFloat().reduced(5.0f);
    const auto panelWidth = (area.getWidth() - kPanelGap) * 0.5f;
    area.removeFromLeft(panelWidth + kPanelGap);

    WheelVisual visual;
    visual.panel = area;
    const auto trackArea = visual.panel.reduced(12.0f, 22.0f);
    visual.track = juce::Rectangle<float>(trackArea.getCentreX() - kTrackWidth * 0.5f,
                                          trackArea.getY(),
                                          kTrackWidth,
                                          trackArea.getHeight());
    return visual;
}

float PerformanceControls::clampPitch(float value)
{
    return juce::jlimit(-1.0f, 1.0f, value);
}

float PerformanceControls::clampMod(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}
