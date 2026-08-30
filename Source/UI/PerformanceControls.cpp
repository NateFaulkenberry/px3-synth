#include "PerformanceControls.h"

#include "UIConfig.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPanelGap = 14.0f;
constexpr float kTrackWidth = 10.0f;

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
               const PerformanceControls::Style& style,
               const juce::String& title,
               const juce::Colour& accent,
               juce::Rectangle<float> panel,
               juce::Rectangle<float> track,
               float normalizedValue,
               bool hasCenter,
               float glow)
{
    g.setColour(style.panelColour);
    px3::ui::fillRounded(g, panel, style.panelRadius);

    const auto labelArea = panel.removeFromTop(style.titleHeight);

    g.setColour(style.titleColour);
    g.setFont(juce::FontOptions(style.titleSize, juce::Font::bold));
    g.drawText(title, labelArea.toNearestInt(), juce::Justification::centred, false);

    g.setColour(accent.withAlpha(style.trackFillAlpha + style.trackFillGlowAlpha * glow));
    px3::ui::fillRounded(g, track, style.trackRadius);

    g.setColour(accent.withAlpha(style.trackBorderAlpha + style.trackBorderGlowAlpha * glow));
    px3::ui::drawRounded(g, track, style.trackRadius, style.trackBorderWidth);

    const auto handleRadius = style.handleRadius;
    const auto topY = track.getY() + handleRadius;
    const auto bottomY = track.getBottom() - handleRadius;
    float handleY = bottomY;

    if (hasCenter)
    {
        const auto centerY = (topY + bottomY) * 0.5f;
        handleY = centerY - normalizedValue * (bottomY - topY) * 0.5f;

        g.setColour(accent.withAlpha(style.centreLineAlpha));
        g.drawLine(track.getX() - 6.0f, centerY, track.getRight() + 6.0f, centerY, 1.0f);
    }
    else
    {
        handleY = bottomY - normalizedValue * (bottomY - topY);
    }

    const auto handleX = track.getCentreX();
    const auto glowAlpha = 0.14f + 0.56f * glow;

    const auto outer = style.handleGlowOuterRadius;
    const auto inner = style.handleGlowInnerRadius;

    g.setColour(accent.withAlpha(glowAlpha * 0.50f));
    g.fillEllipse(handleX - outer, handleY - outer, outer * 2.0f, outer * 2.0f);

    g.setColour(accent.withAlpha(glowAlpha));
    g.fillEllipse(handleX - inner, handleY - inner, inner * 2.0f, inner * 2.0f);

    juce::ColourGradient handleGradient(accent.brighter(0.35f + 0.25f * glow),
                                        handleX,
                                        handleY - handleRadius,
                                        accent.darker(0.45f),
                                        handleX,
                                        handleY + handleRadius,
                                        false);
    g.setGradientFill(handleGradient);
    g.fillEllipse(handleX - handleRadius, handleY - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f);

    g.setColour(style.handleRimColour.withAlpha((120.0f + 80.0f * glow) / 255.0f));
    g.drawEllipse(handleX - handleRadius, handleY - handleRadius, handleRadius * 2.0f, handleRadius * 2.0f, 1.0f);

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

    // Movement gives the burst a kick, but it is not what drives the emission -
    // the DISPLACEMENT is. A wheel held at full bend keeps throwing sparkles;
    // one nudged and released throws a handful. See timerCallback.
    pitchKick = juce::jmax(pitchKick, std::abs(targetPitch - previousTargetPitch) * 4.0f);
    previousTargetPitch = targetPitch;

    modKick = juce::jmax(modKick, std::abs(targetMod - previousTargetMod) * 4.0f);
    previousTargetMod = targetMod;

    const auto pitchUse = clamp01(std::abs(targetPitch));
    const auto modUse = clamp01(targetMod);

    targetPitchGlow = clamp01(0.20f * pitchUse + 0.80f * clamp01(pitchActivity));
    targetModGlow = clamp01(0.30f * modUse + 0.70f * clamp01(modActivity));
}

// Painted by the shared overlay, for the same reason the keyboard's sparks are:
// the two components overlap each other and z-order can only favour one.
// As with the keyboard's sparks, each sparkle contributes its own reach. The
// halo is 2.6x the star's arm across, so it extends 1.3x the arm from the
// centre - and the arm is the sparkle's own size.
juce::Rectangle<float> PerformanceControls::sparkleBounds() const
{
    if (sparkles.empty())
    {
        return {};
    }

    juce::Rectangle<float> bounds;
    auto first = true;

    for (const auto& sparkle : sparkles)
    {
        // size at draw time is scaled by remaining life, never above the
        // sparkle's own size, so this is a true upper bound.
        const auto reach = sparkle.size * 1.3f + 2.0f;
        const auto box = juce::Rectangle<float>(sparkle.position, sparkle.position).expanded(reach);
        bounds = first ? box : bounds.getUnion(box);
        first = false;
    }

    return bounds;
}

void PerformanceControls::paintSparklesInto(juce::Graphics& g, juce::Point<int> offset) const
{
    if (sparkles.empty())
    {
        return;
    }

    juce::Graphics::ScopedSaveState overlayTransform(g);
    g.addTransform(juce::AffineTransform::translation(static_cast<float>(offset.getX()),
                                                      static_cast<float>(offset.getY())));

    for (const auto& sparkle : sparkles)
    {
            const auto divisor = juce::jmax(0.0001f, sparkle.maxLifetimeSeconds);
            const auto life = juce::jlimit(0.0f, 1.0f, sparkle.lifetimeSeconds / divisor);

            // Fades out AND shrinks. Fading alone leaves a ghost the same size
            // as a live spark, which reads as the animation stalling.
            const auto colour = juce::Colour::fromHSV(sparkle.hue, 0.85f, 1.0f, life);
            const auto size = sparkle.size * (0.35f + 0.65f * life);

            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(juce::AffineTransform::rotation(sparkle.rotation)
                               .translated(sparkle.position.getX(), sparkle.position.getY()));

            // A soft halo under the star, so a dense burst glows instead of
            // looking like scattered confetti.
            g.setColour(colour.withMultipliedAlpha(0.30f));
            g.fillEllipse(juce::Rectangle<float>(size * 2.6f, size * 2.6f).withCentre({ 0.0f, 0.0f }));

            g.setColour(colour);
            g.fillPath(createSparklePath(size));

            // A white core keeps the sparkle legible against the rainbow.
            g.setColour(juce::Colours::white.withAlpha(life * 0.85f));
            g.fillEllipse(juce::Rectangle<float>(size * 0.42f, size * 0.42f).withCentre({ 0.0f, 0.0f }));
        }
    }

namespace
{
juce::Colour cfgColour(const UIConfig* c, const juce::String& path, juce::Colour fallback)
{
    return (c == nullptr || c->getValue(path).isVoid()) ? fallback : c->getColour(path, fallback);
}

float cfgFloat(const UIConfig* c, const juce::String& path, float fallback)
{
    return (c == nullptr || c->getValue(path).isVoid()) ? fallback : c->getFloat(path, fallback);
}
} // namespace

PerformanceControls::Style PerformanceControls::Style::fromConfig(const UIConfig* config,
                                                                  const juce::String& prefix)
{
    Style s;
    if (config == nullptr)
    {
        return s;
    }

    s.background = cfgColour(config, prefix + ".background.color", s.background);
    s.backgroundOpacity = cfgFloat(config, prefix + ".background.opacity", s.backgroundOpacity);
    s.borderInset = cfgFloat(config, prefix + ".border.inset", s.borderInset);
    s.borderColour = cfgColour(config, prefix + ".border.color", s.borderColour);
    s.borderWidth = cfgFloat(config, prefix + ".border.width", s.borderWidth);
    s.borderRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".border", s.borderRadius);

    s.panelColour = cfgColour(config, prefix + ".wheelPanel.color", s.panelColour);
    s.panelRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".wheelPanel", s.panelRadius);

    s.titleColour = cfgColour(config, prefix + ".title.color", s.titleColour);
    s.titleSize = cfgFloat(config, prefix + ".title.fontSize", s.titleSize);
    s.titleHeight = cfgFloat(config, prefix + ".title.height", s.titleHeight);

    s.trackRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".track", s.trackRadius);
    s.trackFillAlpha = cfgFloat(config, prefix + ".track.fillOpacity", s.trackFillAlpha);
    s.trackFillGlowAlpha = cfgFloat(config, prefix + ".track.fillGlowOpacity", s.trackFillGlowAlpha);
    s.trackBorderAlpha = cfgFloat(config, prefix + ".track.borderOpacity", s.trackBorderAlpha);
    s.trackBorderGlowAlpha = cfgFloat(config, prefix + ".track.borderGlowOpacity", s.trackBorderGlowAlpha);
    s.trackBorderWidth = cfgFloat(config, prefix + ".track.borderWidth", s.trackBorderWidth);
    s.centreLineAlpha = cfgFloat(config, prefix + ".track.centreLineOpacity", s.centreLineAlpha);

    s.handleRadius = cfgFloat(config, prefix + ".handle.radius", s.handleRadius);
    s.handleGlowOuterRadius = cfgFloat(config, prefix + ".handle.glowOuterRadius", s.handleGlowOuterRadius);
    s.handleGlowInnerRadius = cfgFloat(config, prefix + ".handle.glowInnerRadius", s.handleGlowInnerRadius);
    s.handleRimColour = cfgColour(config, prefix + ".handle.rimColor", s.handleRimColour);

    s.sparkleMaxPerBurst = static_cast<int>(cfgFloat(config, prefix + ".sparkles.maxPerBurst",
                                                     static_cast<float>(s.sparkleMaxPerBurst)));
    s.sparkleRate = cfgFloat(config, prefix + ".sparkles.rate", s.sparkleRate);

    s.pitchAccent = cfgColour(config, prefix + ".pitch.accent", s.pitchAccent);
    s.modAccent = cfgColour(config, prefix + ".mod.accent", s.modAccent);
    return s;
}

void PerformanceControls::setStyle(const Style& newStyle)
{
    style = newStyle;
    repaint();
}

juce::Rectangle<int> PerformanceControls::controlsArea() const
{
    return getLocalBounds();
}

void PerformanceControls::paint(juce::Graphics& g)
{
    // Only the strip's own rectangle. fillAll would paint the spark headroom
    // too, which sits over the panel above.
    // The FILL follows the same corners as the border. It was a plain
    // fillRect, so rounding the border left a square block of background
    // sitting behind it and the corner never appeared to round at all.
    //
    // Both are drawn on the inset rectangle, so the border sits on the fill's
    // own edge rather than inside a larger square of colour.
    const auto bounds = controlsArea().toFloat().reduced(style.borderInset);

    g.setColour(style.background.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, style.backgroundOpacity)));
    px3::ui::fillRounded(g, bounds, style.borderRadius);

    g.setColour(style.borderColour);
    px3::ui::drawRounded(g, bounds, style.borderRadius, style.borderWidth);

    drawWheel(g,
              style,
              "PITCH",
              style.pitchAccent,
              getPitchVisual().panel,
              getPitchVisual().track,
              visualPitch,
              true,
              easeAmount(visualPitchGlow));

    drawWheel(g,
              style,
              "MOD",
              style.modAccent,
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

    constexpr float dt = 1.0f / 60.0f;
    for (auto& sparkle : sparkles)
    {
        sparkle.position += sparkle.velocity;
        sparkle.velocity *= 0.94f;
        // A little lift rather than gravity: these are sparks, and sparks rise.
        sparkle.velocity.y -= 0.025f;
        sparkle.lifetimeSeconds -= dt;
        sparkle.rotation += sparkle.spin;
    }

    // Continuous emission, proportional to how far each wheel is bent. The
    // accumulator is what makes a small bend a trickle and a full one a
    // constant spray, without either being a special case.
    const auto emitFrom = [this](juce::Point<float> handleCentre, float bend, float& kick, float& accumulator)
    {
        const auto intensity = juce::jlimit(0.0f, 1.0f, bend + kick);
        kick *= 0.82f;

        if (intensity <= 0.02f)
        {
            accumulator = 0.0f;
            return;
        }

        // Bursts per frame, scaled so a full bend emits every frame and a
        // quarter bend roughly every fourth.
        accumulator += (0.15f + intensity * 0.85f) * style.sparkleRate;
        while (accumulator >= 1.0f)
        {
            accumulator -= 1.0f;
            emitSparkles(handleCentre, style.handleRadius, intensity);
        }
    };

    {
        const auto track = getPitchVisual().track;
        const auto topY = track.getY() + style.handleRadius;
        const auto bottomY = track.getBottom() - style.handleRadius;
        const auto centreY = (topY + bottomY) * 0.5f;
        emitFrom({ track.getCentreX(), centreY - visualPitch * (bottomY - topY) * 0.5f },
                 juce::jlimit(0.0f, 1.0f, std::abs(visualPitch)), pitchKick, pitchEmitAccumulator);
    }
    {
        const auto track = getModVisual().track;
        const auto topY = track.getY() + style.handleRadius;
        const auto bottomY = track.getBottom() - style.handleRadius;
        emitFrom({ track.getCentreX(), bottomY - visualMod * (bottomY - topY) },
                 juce::jlimit(0.0f, 1.0f, visualMod), modKick, modEmitAccumulator);
    }

    sparkles.erase(std::remove_if(sparkles.begin(),
                                  sparkles.end(),
                                  [](const Sparkle& s) { return s.lifetimeSeconds <= 0.0f; }),
                   sparkles.end());

    // A hard ceiling, dropping the OLDEST first: a held bend emits continuously,
    // and without this the vector grows for as long as the wheel is up.
    constexpr std::size_t kMaxSparkles = 220;
    if (sparkles.size() > kMaxSparkles)
    {
        sparkles.erase(sparkles.begin(),
                       sparkles.begin() + static_cast<std::ptrdiff_t>(sparkles.size() - kMaxSparkles));
    }

    // The wheels themselves still animate - the handles move and the glows
    // breathe - so this component repaints either way; the sparkles are the
    // overlay's job.
    repaint();

    // One frame past empty, so the overlay clears the last sparkle.
    if ((! sparkles.empty() || hadSparklesLastFrame) && onSparklesChanged != nullptr)
    {
        onSparklesChanged();
    }
    hadSparklesLastFrame = ! sparkles.empty();
}

// Emitted in every direction, not along one. The angle is drawn from the whole
// circle, so a burst is a starburst around the handle rather than a stream
// leaving it - which is what the two shape animations did before.
void PerformanceControls::emitSparkles(juce::Point<float> centre, float radius, float intensity)
{
    intensity = juce::jlimit(0.0f, 1.0f, intensity);
    if (intensity <= 0.001f)
    {
        return;
    }

    // Count, speed, size and lifetime all rise together with the bend, so the
    // burst grows as a whole instead of just getting faster or just denser.
    const auto count = juce::jlimit(1, juce::jmax(1, style.sparkleMaxPerBurst),
                                    static_cast<int>(std::lround(
                                        1.0f + intensity * (static_cast<float>(style.sparkleMaxPerBurst) - 1.0f))));

    for (int i = 0; i < count; ++i)
    {
        // A point on the handle's circumference, and the outward normal there.
        const auto bearing = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const auto outward = juce::Point<float>(std::cos(bearing), std::sin(bearing));

        // Spawned just outside the rim so a sparkle never appears on top of the
        // knob it came off.
        const auto spawnRadius = radius + 1.0f + rng.nextFloat() * 2.0f;

        // Travelling out along that same radius, with a little tangential
        // scatter. Without the scatter the burst is a set of perfectly straight
        // spokes, which reads as a diagram rather than as sparks.
        const auto scatter = (rng.nextFloat() * 2.0f - 1.0f) * 0.38f;
        const auto heading = bearing + scatter;
        const auto speed = (0.8f + 2.2f * rng.nextFloat()) * (0.55f + 1.15f * intensity);

        Sparkle sparkle;
        sparkle.position = centre + outward * spawnRadius;
        sparkle.velocity = { std::cos(heading) * speed, std::sin(heading) * speed };
        sparkle.maxLifetimeSeconds = (0.26f + rng.nextFloat() * 0.30f) * (0.7f + 0.5f * intensity);
        sparkle.lifetimeSeconds = sparkle.maxLifetimeSeconds;
        sparkle.size = (1.8f + rng.nextFloat() * 2.0f) * (0.7f + 0.6f * intensity);

        // The hue advances per sparkle, so a single burst spans the spectrum
        // rather than every spark in it sharing one colour.
        hueCycle += 0.071f;
        if (hueCycle >= 1.0f)
        {
            hueCycle -= 1.0f;
        }
        sparkle.hue = hueCycle;

        sparkle.rotation = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        sparkle.spin = (rng.nextFloat() * 2.0f - 1.0f) * 0.10f;
        sparkles.push_back(sparkle);
    }
}

// A four-point star: two crossed spindles, thin at the waist. Built as a path
// rather than drawn as lines so the points taper.
juce::Path PerformanceControls::createSparklePath(float size)
{
    juce::Path path;
    const auto arm = size;
    const auto waist = size * 0.26f;

    path.startNewSubPath(0.0f, -arm);
    path.quadraticTo(waist * 0.4f, -waist * 0.4f, arm, 0.0f);
    path.quadraticTo(waist * 0.4f, waist * 0.4f, 0.0f, arm);
    path.quadraticTo(-waist * 0.4f, waist * 0.4f, -arm, 0.0f);
    path.quadraticTo(-waist * 0.4f, -waist * 0.4f, 0.0f, -arm);
    path.closeSubPath();
    return path;
}


void PerformanceControls::updateFromMousePosition(juce::Point<float> position)
{
    if (activeControl == ActiveControl::pitch)
    {
        const auto track = getPitchVisual().track;
        const auto topY = track.getY() + style.handleRadius;
        const auto bottomY = track.getBottom() - style.handleRadius;
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
        const auto topY = track.getY() + style.handleRadius;
        const auto bottomY = track.getBottom() - style.handleRadius;
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
    auto area = controlsArea().toFloat().reduced(5.0f);
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
    auto area = controlsArea().toFloat().reduced(5.0f);
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
