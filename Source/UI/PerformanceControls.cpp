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

    const auto pitchDelta = targetPitch - previousTargetPitch;
    const auto pitchMove = std::abs(pitchDelta);
    if (pitchMove > 0.02f)
    {
        const auto direction = pitchDelta >= 0.0f ? 1.0f : -1.0f;
        spawnUnicornsFromPitchWheel(pitchMove, direction);
    }
    previousTargetPitch = targetPitch;

    const auto modMove = std::abs(targetMod - previousTargetMod);
    if (modMove > 0.03f)
    {
        spawnCatsFromModWheel(modMove);
    }
    previousTargetMod = targetMod;

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

    if (!unicornSparks.empty())
    {
        for (const auto& unicorn : unicornSparks)
        {
            const auto lifeDen = juce::jmax(0.0001f, unicorn.maxLifetimeSeconds);
            const auto lifeNorm = juce::jlimit(0.0f, 1.0f, unicorn.lifetimeSeconds / lifeDen);
            const auto alpha = lifeNorm;

            auto bodyPath = createUnicornPath(unicorn.scale, unicorn.facing);
            bodyPath.applyTransform(juce::AffineTransform::rotation(unicorn.rotation)
                                        .translated(unicorn.position.x, unicorn.position.y));

            auto hornPath = createUnicornHornPath(unicorn.scale, unicorn.facing);
            hornPath.applyTransform(juce::AffineTransform::rotation(unicorn.rotation)
                                        .translated(unicorn.position.x, unicorn.position.y));

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<juce::uint8>(170.0f * alpha)));
            g.fillPath(bodyPath);

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<juce::uint8>(220.0f * alpha)));
            g.strokePath(bodyPath, juce::PathStrokeType(1.0f));

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<juce::uint8>(240.0f * alpha)));
            g.strokePath(hornPath, juce::PathStrokeType(1.0f));
        }
    }

    if (!catSparks.empty())
    {
        for (const auto& cat : catSparks)
        {
            const auto lifeDen = juce::jmax(0.0001f, cat.maxLifetimeSeconds);
            const auto lifeNorm = juce::jlimit(0.0f, 1.0f, cat.lifetimeSeconds / lifeDen);
            const auto alpha = lifeNorm;

            auto catPath = createCatPath(cat.scale);
            catPath.applyTransform(juce::AffineTransform::rotation(cat.rotation)
                                       .translated(cat.position.x, cat.position.y));

            g.setColour(juce::Colour::fromRGBA(0, 0, 0, static_cast<juce::uint8>(180.0f * alpha)));
            g.fillPath(catPath);

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, static_cast<juce::uint8>(200.0f * alpha)));
            g.strokePath(catPath, juce::PathStrokeType(1.0f));
        }
    }
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

    constexpr float dt = 1.0f / 60.0f;
    for (auto& cat : catSparks)
    {
        cat.position += cat.velocity;
        cat.velocity *= 0.95f;
        cat.velocity.y -= 0.03f;
        cat.lifetimeSeconds -= dt;
        cat.rotation += cat.spin;
    }

    for (auto& unicorn : unicornSparks)
    {
        unicorn.position += unicorn.velocity;
        unicorn.velocity *= 0.94f;
        unicorn.velocity.y -= 0.02f;
        unicorn.lifetimeSeconds -= dt;
        unicorn.rotation += unicorn.spin;
    }

    catSparks.erase(std::remove_if(catSparks.begin(),
                                   catSparks.end(),
                                   [](const CatSpark& c) { return c.lifetimeSeconds <= 0.0f; }),
                   catSparks.end());

    unicornSparks.erase(std::remove_if(unicornSparks.begin(),
                                       unicornSparks.end(),
                                       [](const UnicornSpark& u) { return u.lifetimeSeconds <= 0.0f; }),
                       unicornSparks.end());

    if (catSparks.size() > 80)
    {
        catSparks.erase(catSparks.begin(), catSparks.begin() + static_cast<std::ptrdiff_t>(catSparks.size() - 80));
    }

    if (unicornSparks.size() > 110)
    {
        unicornSparks.erase(unicornSparks.begin(), unicornSparks.begin() + static_cast<std::ptrdiff_t>(unicornSparks.size() - 110));
    }

    repaint();
}

void PerformanceControls::spawnCatsFromModWheel(float movementAmount)
{
    const auto modVisual = getModVisual();
    const auto track = modVisual.track;
    const auto topY = track.getY() + kHandleRadius;
    const auto bottomY = track.getBottom() - kHandleRadius;
    const auto handleY = bottomY - targetMod * (bottomY - topY);
    const auto handleX = track.getCentreX();

    const auto intensity = juce::jlimit(0.0f, 1.0f, movementAmount * 6.0f + targetMod * 0.30f);
    const auto count = juce::jlimit(1, 3, static_cast<int>(std::lround(1.0f + intensity * 2.0f)));

    for (int i = 0; i < count; ++i)
    {
        const auto angle = juce::MathConstants<float>::pi * (0.85f + 0.30f * rng.nextFloat());
        const auto speed = (1.0f + 2.5f * rng.nextFloat()) * (0.65f + 0.55f * intensity);

        CatSpark cat;
        cat.position = {
            handleX + (rng.nextFloat() * 2.0f - 1.0f) * 7.0f,
            handleY + (rng.nextFloat() * 2.0f - 1.0f) * 6.0f
        };
        cat.velocity = {
            std::cos(angle) * speed,
            std::sin(angle) * speed - (0.4f + 1.0f * intensity)
        };
        cat.maxLifetimeSeconds = 0.16f + rng.nextFloat() * 0.24f;
        cat.lifetimeSeconds = cat.maxLifetimeSeconds;
        cat.scale = 1.45f + rng.nextFloat() * 1.35f;
        cat.rotation = (rng.nextFloat() * 2.0f - 1.0f) * 0.7f;
        cat.spin = (rng.nextFloat() * 2.0f - 1.0f) * 0.09f;
        catSparks.push_back(cat);
    }
}

void PerformanceControls::spawnUnicornsFromPitchWheel(float movementAmount, float direction)
{
    const auto pitchVisual = getPitchVisual();
    const auto track = pitchVisual.track;
    const auto topY = track.getY() + kHandleRadius;
    const auto bottomY = track.getBottom() - kHandleRadius;
    const auto centerY = (topY + bottomY) * 0.5f;
    const auto handleY = centerY - targetPitch * (bottomY - topY) * 0.5f;
    const auto handleX = track.getCentreX();

    const auto intensity = juce::jlimit(0.0f, 1.0f, movementAmount * 5.5f + std::abs(targetPitch) * 0.40f);
    const auto count = juce::jlimit(1, 4, static_cast<int>(std::lround(1.0f + intensity * 3.0f)));

    for (int i = 0; i < count; ++i)
    {
        const auto spread = (rng.nextFloat() * 2.0f - 1.0f) * 0.65f;
        const auto launch = direction > 0.0f ? -1.0f : 1.0f;
        const auto angle = juce::MathConstants<float>::halfPi + launch * (0.40f + spread);
        const auto speed = (0.9f + 2.6f * rng.nextFloat()) * (0.75f + 0.65f * intensity);

        UnicornSpark unicorn;
        unicorn.position = {
            handleX + direction * (4.0f + rng.nextFloat() * 6.0f),
            handleY + (rng.nextFloat() * 2.0f - 1.0f) * 6.0f
        };
        unicorn.velocity = {
            std::cos(angle) * speed + direction * (0.4f + intensity * 0.9f),
            std::sin(angle) * speed
        };
        unicorn.maxLifetimeSeconds = 0.24f + rng.nextFloat() * 0.26f;
        unicorn.lifetimeSeconds = unicorn.maxLifetimeSeconds;
        unicorn.scale = 1.30f + rng.nextFloat() * 1.30f;
        unicorn.facing = direction;
        unicorn.rotation = (rng.nextFloat() * 2.0f - 1.0f) * 0.18f;
        unicorn.spin = (rng.nextFloat() * 2.0f - 1.0f) * 0.07f;
        unicornSparks.push_back(unicorn);
    }
}

juce::Path PerformanceControls::createCatPath(float scale)
{
    juce::Path cat;

    cat.addEllipse(-4.6f * scale, -2.8f * scale, 9.2f * scale, 5.6f * scale);
    cat.addEllipse(-2.2f * scale, -6.2f * scale, 4.4f * scale, 3.9f * scale);

    juce::Path ears;
    ears.startNewSubPath(-1.5f * scale, -3.6f * scale);
    ears.lineTo(-3.2f * scale, -6.8f * scale);
    ears.lineTo(-0.2f * scale, -5.2f * scale);
    ears.closeSubPath();
    ears.startNewSubPath(1.5f * scale, -3.6f * scale);
    ears.lineTo(3.2f * scale, -6.8f * scale);
    ears.lineTo(0.2f * scale, -5.2f * scale);
    ears.closeSubPath();
    cat.addPath(ears);

    juce::Path tail;
    tail.startNewSubPath(3.2f * scale, -0.4f * scale);
    tail.quadraticTo(7.2f * scale, -2.1f * scale, 6.6f * scale, 1.8f * scale);
    cat.addPath(tail);

    return cat;
}

juce::Path PerformanceControls::createUnicornPath(float scale, float facing)
{
    juce::Path unicorn;

    // Stylized compact unicorn body for small particle rendering.
    unicorn.addEllipse(-4.5f * scale, -2.5f * scale, 9.0f * scale, 5.0f * scale);
    unicorn.addEllipse((2.0f * facing - 2.6f) * scale, -5.8f * scale, 4.4f * scale, 3.7f * scale);

    juce::Path legs;
    legs.startNewSubPath(-2.6f * scale, 1.5f * scale);
    legs.lineTo(-2.7f * scale, 4.2f * scale);
    legs.startNewSubPath(0.9f * scale, 1.4f * scale);
    legs.lineTo(1.0f * scale, 4.1f * scale);
    unicorn.addPath(legs);

    juce::Path tail;
    const auto tailX = -3.6f * facing * scale;
    tail.startNewSubPath(tailX, -0.1f * scale);
    tail.quadraticTo(tailX - 3.3f * facing * scale,
                     -2.6f * scale,
                     tailX - 2.7f * facing * scale,
                     1.8f * scale);
    unicorn.addPath(tail);

    return unicorn;
}

juce::Path PerformanceControls::createUnicornHornPath(float scale, float facing)
{
    juce::Path horn;

    const auto headX = (2.7f * facing) * scale;
    const auto hornBaseY = -4.9f * scale;
    const auto hornTipX = headX + 2.3f * facing * scale;
    const auto hornTipY = -8.0f * scale;

    horn.startNewSubPath(headX, hornBaseY);
    horn.lineTo(hornTipX, hornTipY);

    // White rainbow-style horn bands represented as short white cross bars.
    for (int i = 1; i <= 3; ++i)
    {
        const auto t = static_cast<float>(i) / 4.0f;
        const auto px = juce::jmap(t, headX, hornTipX);
        const auto py = juce::jmap(t, hornBaseY, hornTipY);
        const auto band = 0.9f * scale;

        horn.startNewSubPath(px - band * facing, py + 0.2f * scale);
        horn.lineTo(px + band * facing * 0.2f, py - 0.2f * scale);
    }

    return horn;
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
