#include "PerformanceControls.h"

#include "PianoKeyboard.h"

#include "Theme.h"
#include "UIConfig.h"

#include <algorithm>
#include <cmath>

namespace
{
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
               const juce::Colour& panelColour,
               juce::Rectangle<float> panel,
               juce::Rectangle<float> track,
               float normalizedValue,
               bool hasCenter,
               float glow)
{
    // A hardware wheel seen edge-on through its slot: a ribbed cylinder in a
    // recessed well. The cylinder is shaded by the cosine of the angle each row
    // of it faces away from the viewer, so it reads round; its grip ribs bunch
    // up towards the slot's ends for the same reason, and they ROLL with the
    // value, so moving the wheel looks like turning it. The value is the one
    // accent line on the wheel - the painted stripe a real wheel carries -
    // rather than a ball riding a groove.
    g.setColour(panelColour);
    px3::ui::fillRounded(g, panel, style.panelRadius);

    const auto labelArea = panel.removeFromTop(style.titleHeight);
    g.setColour(style.titleColour);
    g.setFont(juce::FontOptions(style.titleSize, juce::Font::bold));
    g.drawText(title, labelArea.toNearestInt(), juce::Justification::centred, false);

    if (track.getHeight() < 8.0f || track.getWidth() < 4.0f) { return; }

    // The well, then the wheel inside it with a hairline of shadow all round.
    px3::ui::theme::drawInset(g, track);
    const auto wheel = track.reduced(2.0f, 1.0f);

    const auto halfSpan = juce::MathConstants<float>::halfPi * 0.92f; // how much of the cylinder shows
    const auto centreY = wheel.getCentreY();
    const auto radius = wheel.getHeight() * 0.5f / std::sin(halfSpan);
    const auto angleAt = [&](float y) { return std::asin(juce::jlimit(-1.0f, 1.0f, (y - centreY) / radius)); };

    // Body: a vertical ramp sampled from the cylinder's own shading, so the
    // brightest band sits where the wheel faces the viewer.
    {
        const auto lit = juce::Colour::fromRGB(74, 79, 86);
        const auto shade = juce::Colour::fromRGB(14, 15, 17);
        juce::ColourGradient body(shade, 0.0f, wheel.getY(), shade, 0.0f, wheel.getBottom(), false);
        for (int i = 1; i < 12; ++i)
        {
            const auto t = static_cast<float>(i) / 12.0f;
            const auto facing = std::cos(angleAt(wheel.getY() + t * wheel.getHeight()));
            body.addColour(t, shade.interpolatedWith(lit, std::pow(facing, 1.6f)));
        }
        g.setGradientFill(body);
        g.fillRect(wheel);

        // The rubber's edge catches a little light on either side.
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.fillRect(wheel.withWidth(1.0f));
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRect(wheel.withX(wheel.getRight() - 1.0f).withWidth(1.0f));
    }

    // Where the value puts the stripe, and so how far the wheel has turned.
    const auto topY = track.getY() + style.handleRadius;
    const auto bottomY = track.getBottom() - style.handleRadius;
    const auto markY = hasCenter ? (topY + bottomY) * 0.5f - normalizedValue * (bottomY - topY) * 0.5f
                                 : bottomY - normalizedValue * (bottomY - topY);
    const auto turn = angleAt(markY);

    // Ribs, every ribStep of rotation, dimming as they turn away.
    {
        constexpr auto ribStep = 0.17f;
        const auto first = std::ceil((-halfSpan - turn) / ribStep);
        const auto last = std::floor((halfSpan - turn) / ribStep);
        for (auto k = first; k <= last; k += 1.0f)
        {
            const auto angle = turn + k * ribStep;
            const auto y = std::round(centreY + radius * std::sin(angle));
            const auto facing = std::cos(angle);
            g.setColour(juce::Colours::black.withAlpha(0.55f * facing));
            g.fillRect(wheel.getX() + 1.0f, y, wheel.getWidth() - 2.0f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.10f * facing));
            g.fillRect(wheel.getX() + 1.0f, y + 1.0f, wheel.getWidth() - 2.0f, 1.0f);
        }
    }

    // The stripe: thicker facing the viewer, thinner at the ends, with a glow
    // that rises with activity.
    {
        const auto facing = std::cos(turn);
        const auto thickness = juce::jmax(1.0f, 3.0f * facing);
        const auto stripe = juce::Rectangle<float>(wheel.getX(), markY - thickness * 0.5f,
                                                   wheel.getWidth(), thickness);
        g.setColour(accent.withAlpha((0.18f + 0.30f * glow) * facing));
        g.fillRect(stripe.expanded(0.0f, 2.0f));
        g.setColour(accent.brighter(0.2f + 0.3f * glow).withAlpha(0.55f + 0.45f * facing));
        g.fillRect(stripe);
    }

    // Pitch springs back to centre, so its detent is marked beside the slot.
    if (hasCenter)
    {
        g.setColour(accent.withAlpha(style.centreLineAlpha + 0.25f * glow));
        const auto y = std::round(centreY) - 0.5f;
        g.fillRect(track.getX() - 6.0f, y, 4.0f, 1.0f);
        g.fillRect(track.getRight() + 2.0f, y, 4.0f, 1.0f);
    }
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
    s.panelOpacity = cfgFloat(config, prefix + ".wheelPanel.opacity", s.panelOpacity);
    s.panelRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".wheelPanel", s.panelRadius);

    // Each wheel starts from the shared panel and overrides only what it
    // declares, so setting wheelPanel alone still changes both.
    s.pitchPanelColour = cfgColour(config, prefix + ".pitch.background.color", s.panelColour);
    s.pitchPanelOpacity = cfgFloat(config, prefix + ".pitch.background.opacity", s.panelOpacity);
    s.modPanelColour = cfgColour(config, prefix + ".mod.background.color", s.panelColour);
    s.modPanelOpacity = cfgFloat(config, prefix + ".mod.background.opacity", s.panelOpacity);

    s.titleColour = cfgColour(config, prefix + ".title.color", s.titleColour);
    s.titleSize = cfgFloat(config, prefix + ".title.fontSize", s.titleSize);
    s.titleHeight = cfgFloat(config, prefix + ".title.height", s.titleHeight);

    s.trackRadius = px3::ui::CornerRadii::fromConfig(config, prefix + ".track", s.trackRadius);
    s.trackWidth = cfgFloat(config, prefix + ".track.width", s.trackWidth);
    s.trackInsetTop = cfgFloat(config, prefix + ".track.insetTop", s.trackInsetTop);
    s.trackInsetBottom = cfgFloat(config, prefix + ".track.insetBottom", s.trackInsetBottom);
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


    if (const auto v = config->getValue(prefix + ".divider.orientation"); ! v.isVoid())
    {
        s.dividerOrientation = v.toString().trim().equalsIgnoreCase("vertical")
                                   ? Style::DividerOrientation::vertical
                                   : Style::DividerOrientation::horizontal;
    }
    s.dividerColour = cfgColour(config, prefix + ".divider.color", s.dividerColour);
    s.dividerOpacity = cfgFloat(config, prefix + ".divider.opacity", s.dividerOpacity);
    s.dividerInset = cfgFloat(config, prefix + ".divider.inset", s.dividerInset);
    s.dividerWidth = cfgFloat(config, prefix + ".divider.width", s.dividerWidth);
    s.panelInset = cfgFloat(config, prefix + ".layout.panelInset", s.panelInset);
    s.panelGap = cfgFloat(config, prefix + ".layout.panelGap", s.panelGap);
    s.cheekLeft = juce::jmax(0.0f, cfgFloat(config, prefix + ".layout.cheekLeft", s.cheekLeft));

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

juce::Rectangle<int> PerformanceControls::wheelsArea() const
{
    // Whole pixels, so the cheek and the PITCH panel meet on one.
    return controlsArea().withTrimmedLeft(juce::roundToInt(style.cheekLeft));
}

void PerformanceControls::paint(juce::Graphics& g)
{
    // Only the strip's own rectangle.
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
              style.pitchPanelColour.withMultipliedAlpha(
                  juce::jlimit(0.0f, 1.0f, style.pitchPanelOpacity)),
              getPitchVisual().panel,
              getPitchVisual().track,
              visualPitch,
              true,
              easeAmount(visualPitchGlow));

    // The divider.
    //
    // Vertical, it falls exactly midway between the two panels - taken from the
    // panels themselves rather than the strip's centre, so it stays in the
    // middle of the GAP if the two are ever sized unevenly. Horizontal, it runs
    // across the middle of both.
    //
    // Either way it is snapped to a half-pixel: a 1px stroke on a whole
    // coordinate straddles two pixel rows and renders as a 2px smear rather
    // than a hairline.
    if (style.dividerWidth > 0.0f)
    {
        const auto pitchPanel = getPitchVisual().panel;
        const auto modPanel = getModVisual().panel;

        g.setColour(style.dividerColour.withMultipliedAlpha(
            juce::jlimit(0.0f, 1.0f, style.dividerOpacity)));

        if (style.dividerOrientation == Style::DividerOrientation::vertical)
        {
            const auto x = std::floor((pitchPanel.getRight() + modPanel.getX()) * 0.5f) + 0.5f;
            const auto top = pitchPanel.getY() + style.dividerInset;
            const auto bottom = pitchPanel.getBottom() - style.dividerInset;

            if (bottom > top)
            {
                g.drawLine(x, top, x, bottom, style.dividerWidth);
            }
        }
        else
        {
            const auto span = pitchPanel.getUnion(modPanel);
            const auto y = std::floor(span.getCentreY()) + 0.5f;
            const auto left = span.getX() + style.dividerInset;
            const auto right = span.getRight() - style.dividerInset;

            if (right > left)
            {
                g.drawLine(left, y, right, y, style.dividerWidth);
            }
        }
    }

    drawWheel(g,
              style,
              "MOD",
              style.modAccent,
              style.modPanelColour.withMultipliedAlpha(
                  juce::jlimit(0.0f, 1.0f, style.modPanelOpacity)),
              getModVisual().panel,
              getModVisual().track,
              visualMod,
              false,
              easeAmount(visualModGlow));

    // The keyboard case's left end, the mirror of the cheek past the top key.
    if (style.cheekLeft > 0.0f)
    {
        const auto strip = controlsArea().toFloat();
        PianoKeyboard::paintEndCheek(g, strip.withWidth(static_cast<float>(juce::roundToInt(style.cheekLeft))), false);
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

    // The wheels repaint when what they draw has moved - a handle, a glow -
    // and not otherwise: an unconditional 60 Hz repaint of a still pair of
    // wheels was a steady share of the editor's idle CPU.
    constexpr float visibleStep = 0.0015f;
    if (std::abs(visualPitch - drawnPitch) > visibleStep || std::abs(visualMod - drawnMod) > visibleStep
        || std::abs(visualPitchGlow - drawnPitchGlow) > visibleStep || std::abs(visualModGlow - drawnModGlow) > visibleStep)
    {
        drawnPitch = visualPitch;
        drawnMod = visualMod;
        drawnPitchGlow = visualPitchGlow;
        drawnModGlow = visualModGlow;
        repaint();
    }
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

juce::Rectangle<float> PerformanceControls::trackIn(juce::Rectangle<float> panel) const
{
    const auto top = panel.getY() + style.trackInsetTop;
    const auto bottom = juce::jmax(top, panel.getBottom() - style.trackInsetBottom);
    // Whole pixels, or a 6 px slot centred in an odd-width panel lands on a
    // half pixel and both its edges blur.
    const auto left = std::round(panel.getCentreX() - style.trackWidth * 0.5f);
    return { left, std::round(top), style.trackWidth, std::round(bottom) - std::round(top) };
}

PerformanceControls::WheelVisual PerformanceControls::getPitchVisual() const
{
    // Whole pixels, so the seam between the panels is exactly panelGap wide
    // and matches the 1 px seams between every other module.
    auto area = wheelsArea().reduced(juce::roundToInt(style.panelInset));
    const auto gap = juce::roundToInt(style.panelGap);
    const auto panelWidth = (area.getWidth() - gap) / 2;

    WheelVisual visual;
    visual.panel = area.removeFromLeft(panelWidth).toFloat();
    visual.track = trackIn(visual.panel);
    return visual;
}

PerformanceControls::WheelVisual PerformanceControls::getModVisual() const
{
    auto area = wheelsArea().reduced(juce::roundToInt(style.panelInset));
    const auto gap = juce::roundToInt(style.panelGap);
    const auto panelWidth = (area.getWidth() - gap) / 2;
    area.removeFromLeft(panelWidth + gap);

    WheelVisual visual;
    visual.panel = area.toFloat();
    visual.track = trackIn(visual.panel);
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
