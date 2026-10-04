#include "MixerControls.h"

#include <cmath>

#include "Theme.h"
#include "UIConfig.h"

FaderStyle FaderStyle::fromUIConfig(const std::shared_ptr<const UIConfig>& uiConfig, const juce::String& pathPrefix)
{
    FaderStyle style;
    if (uiConfig == nullptr)
    {
        return style;
    }

    style.trackWidth = uiConfig->getFloat(pathPrefix + ".trackWidth", style.trackWidth);
    style.thumbWidth = uiConfig->getFloat(pathPrefix + ".thumbWidth", style.thumbWidth);
    style.thumbHeight = uiConfig->getFloat(pathPrefix + ".thumbHeight", style.thumbHeight);
    style.cornerRadius = uiConfig->getFloat(pathPrefix + ".cornerRadius", style.cornerRadius);
    style.trackPadding = uiConfig->getFloat(pathPrefix + ".trackPadding", style.trackPadding);
    style.trackColour = uiConfig->getColour(pathPrefix + ".trackColour", style.trackColour);
    style.trackBackgroundColour = uiConfig->getColour(pathPrefix + ".trackBackgroundColour", style.trackBackgroundColour);
    style.thumbColour = uiConfig->getColour(pathPrefix + ".thumbColour", style.thumbColour);
    style.disabledColour = uiConfig->getColour(pathPrefix + ".disabledColour", style.disabledColour);
    style.hoverColour = uiConfig->getColour(pathPrefix + ".hoverColour", style.hoverColour);
    style.accentColour = uiConfig->getColour(pathPrefix + ".accentColour", style.accentColour);
    style.tickCount = uiConfig->getInt(pathPrefix + ".tickCount", style.tickCount);
    return style;
}

PanKnob::PanKnob()
{
    // The rotary look-and-feel keys its pan rendering - the centre-out arc and
    // the scale ticks - off this property.
    getProperties().set("isMixerPanKnob", true);
}

void PanKnob::setCentreDetent(double range)
{
    snapRange = juce::jmax(0.0, range);
}

void PanKnob::setExtremeDetent(double range)
{
    extremeSnapRange = juce::jmax(0.0, range);
}

double PanKnob::snapValue(double attemptedValue, DragMode dragMode)
{
    if (dragMode == notDragging || snapRange <= 0.0)
    {
        return attemptedValue;
    }

    const auto centre = (getMinimum() + getMaximum()) * 0.5;
    if (std::abs(attemptedValue - centre) <= snapRange)
    {
        return centre;
    }

    // Hard left and hard right get their own, narrower catch: they are edges,
    // so only a deliberate push into them should stick.
    if (extremeSnapRange > 0.0)
    {
        if (attemptedValue - getMinimum() <= extremeSnapRange)
        {
            return getMinimum();
        }
        if (getMaximum() - attemptedValue <= extremeSnapRange)
        {
            return getMaximum();
        }
    }

    return attemptedValue;
}

void FaderStyleLookAndFeel::setStyle(const FaderStyle& styleIn)
{
    style = styleIn;
}

void FaderStyleLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             float sliderPos,
                                             float,
                                             float,
                                             const juce::Slider::SliderStyle,
                                             juce::Slider& slider)
{
    // A console fader: a recessed slot with a scale beside it and a moulded
    // cap. Drawn with paths rather than imported artwork, in the same material
    // language as the knobs - drop shadow, dark vertical body gradient, light
    // rim, dark outer ring - so the two read as one control set.
    const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                               static_cast<float>(y),
                                               static_cast<float>(width),
                                               static_cast<float>(height));
    const auto enabled = slider.isEnabled();
    const auto trackArea = bounds.reduced(style.trackPadding, style.trackPadding);
    if (trackArea.getHeight() <= 0.0f)
    {
        return;
    }

    const auto accent = enabled ? style.accentColour : style.disabledColour;
    const auto trackX = trackArea.getCentreX() - style.trackWidth * 0.5f;
    const auto track = juce::Rectangle<float>(trackX,
                                              trackArea.getY(),
                                              style.trackWidth,
                                              trackArea.getHeight());

    // ---- scale ticks -------------------------------------------------------
    if (style.tickCount > 1)
    {
        const auto tickLong = style.thumbWidth * 0.30f;
        const auto tickShort = tickLong * 0.55f;
        for (int i = 0; i < style.tickCount; ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(style.tickCount - 1);
            const auto ty = juce::jmap(t, trackArea.getY(), trackArea.getBottom());
            const auto major = (i == 0 || i == style.tickCount - 1 || i == style.tickCount / 2);
            const auto len = major ? tickLong : tickShort;
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, major ? 58 : 30));
            g.fillRect(track.getX() - 3.0f - len, ty - 0.5f, len, 1.0f);
            g.fillRect(track.getRight() + 3.0f, ty - 0.5f, len, 1.0f);
        }
    }

    // ---- recessed slot -----------------------------------------------------
    g.setColour(style.trackBackgroundColour);
    g.fillRoundedRectangle(track, style.cornerRadius);
    // A lip along the top edge reads as depth: the slot is cut into the panel.
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 150));
    g.drawRoundedRectangle(track.reduced(0.4f), style.cornerRadius, 1.0f);

    const auto thumbY = juce::jlimit(trackArea.getY(),
                                     trackArea.getBottom() - style.thumbHeight,
                                     sliderPos - style.thumbHeight * 0.5f);
    const auto capCentreY = thumbY + style.thumbHeight * 0.5f;

    // Filled travel below the cap, in the channel's own colour.
    const auto filled = juce::Rectangle<float>(track.getX(),
                                               capCentreY,
                                               track.getWidth(),
                                               track.getBottom() - capCentreY);
    if (filled.getHeight() > 0.0f)
    {
        g.setColour(enabled ? accent.withAlpha(0.92f) : style.disabledColour.withMultipliedAlpha(0.85f));
        g.fillRoundedRectangle(filled, style.cornerRadius);
    }

    // ---- cap ---------------------------------------------------------------
    const auto cap = juce::Rectangle<float>(style.thumbWidth, style.thumbHeight)
                         .withCentre({ trackArea.getCentreX(), capCentreY });
    const auto capRadius = juce::jmin(4.0f, style.thumbHeight * 0.35f);

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 120));
    g.fillRoundedRectangle(cap.translated(0.0f, 2.0f), capRadius);

    juce::ColourGradient body(juce::Colour::fromRGB(78, 78, 78), cap.getX(), cap.getY(),
                              juce::Colour::fromRGB(32, 32, 32), cap.getX(), cap.getBottom(), false);
    body.addColour(0.5, juce::Colour::fromRGB(54, 54, 54));
    g.setGradientFill(body);
    g.fillRoundedRectangle(cap, capRadius);

    paintSurfaceNoise(g, cap, 0.05f);

    // Grip lines, the moulded ridges of a real fader cap.
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 26));
    for (int i = -1; i <= 1; ++i)
    {
        const auto gy = cap.getCentreY() + static_cast<float>(i) * (style.thumbHeight * 0.22f);
        g.fillRect(cap.getX() + 3.0f, gy - 0.5f, cap.getWidth() - 6.0f, 1.0f);
    }

    // The indicator line across the middle, and the rim.
    const auto capColour = (enabled && slider.isMouseOverOrDragging()) ? style.hoverColour : accent;
    g.setColour(enabled ? capColour : style.disabledColour);
    g.fillRect(cap.getX() + 2.0f, cap.getCentreY() - 1.0f, cap.getWidth() - 4.0f, 2.0f);

    g.setColour(juce::Colour::fromRGB(112, 112, 112));
    g.drawRoundedRectangle(cap, capRadius, 1.2f);
    g.setColour(juce::Colour::fromRGB(14, 14, 14));
    g.drawRoundedRectangle(cap.expanded(0.6f), capRadius, 0.9f);
}

FaderSlider::FaderSlider()
{
    setSliderStyle(juce::Slider::LinearVertical);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setScrollWheelEnabled(false);
    setLookAndFeel(&faderLookAndFeel);
}

FaderSlider::~FaderSlider()
{
    setLookAndFeel(nullptr);
}

void FaderSlider::applyStyle(const FaderStyle& style)
{
    faderLookAndFeel.setStyle(style);
    repaint();
}

MixerToggleButton::MixerToggleButton(const juce::String& text)
    : juce::TextButton(text)
{
    setClickingTogglesState(true);
}

void MixerToggleButton::paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    // The theme's switch, the same key every card's toggles use (see
    // ToggleChipButton): a recessed well with the legend inside it. On tints
    // the well, lights a stripe along its foot and turns the legend the
    // switch's own colour. The old console lamp cap - a domed pale button with
    // a round lamp and the legend beneath - was the last skeuomorphic control
    // left on the mixer, so the legend placement in the style no longer moves
    // anything: the name is always on the key.
    namespace th = px3::ui::theme;
    const auto enabled = isEnabled();
    const auto on = isLit() && enabled;
    const auto area = getLocalBounds().toFloat().reduced(1.0f);
    if (area.isEmpty())
    {
        return;
    }

    const auto lit = enabled ? style.activeColour.withAlpha(1.0f)
                             : style.activeColour.withSaturation(0.0f).withAlpha(1.0f);

    th::drawInset(g, area, th::space::insetRadius);
    if (on)
    {
        g.setColour(lit.withAlpha(0.16f));
        g.fillRoundedRectangle(area.reduced(1.0f), th::space::insetRadius);
        g.setColour(lit.withAlpha(0.9f));
        g.fillRoundedRectangle(area.reduced(4.0f, 0.0f).removeFromBottom(2.0f).translated(0.0f, -1.0f), 1.0f);
    }
    if (enabled && (shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown))
    {
        g.setColour(juce::Colours::white.withAlpha(shouldDrawButtonAsDown ? 0.10f : 0.05f));
        g.fillRoundedRectangle(area.reduced(1.0f), th::space::insetRadius);
    }

    const auto textColour = (on ? lit.interpolatedWith(th::colour::textValue, 0.35f) : th::colour::textLabel)
                                .withAlpha(enabled ? 1.0f : 0.5f);
    g.setColour(textColour);
    g.setFont(th::font(th::Type::label)
                  .withHeight(juce::jmin(th::size(th::Type::label), area.getHeight() - 4.0f)));
    g.drawFittedText(getName(), area.reduced(3.0f, 0.0f).toNearestInt(),
                     juce::Justification::centred, 1, 0.75f);
}

void MixerToggleButton::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

MuteButton::MuteButton()
    : MixerToggleButton("M")
{
    setName("M");
    setTooltip("Mute");
}

SoloButton::SoloButton()
    : MixerToggleButton("S")
{
    setName("S");
    setTooltip("Solo");
}

PhaseButton::PhaseButton()
    : MixerToggleButton(juce::String::fromUTF8("\xc3\x98"))
{
    // U+00D8, the slashed O every console uses for polarity inversion.
    setName(juce::String::fromUTF8("\xc3\x98"));
    setTooltip("Phase");
}

namespace px3::ui
{
MixerToggleButton::Style mixerToggleStyleFromConfig(const UIConfig* config,
                                                    const juce::String& sharedBase,
                                                    const juce::String& overrideBase,
                                                    const MixerToggleButton::Style& fallback)
{
    auto style = fallback;
    if (config == nullptr)
    {
        return style;
    }

    const auto apply = [&](const juce::String& base)
    {
        if (base.isEmpty())
        {
            return;
        }

        // getValue rather than getObject throughout: getObject returns a fresh
        // empty object for any path, so it cannot report that a block is
        // absent, and an override that does not exist would wipe the shared one.
        const auto number = [&](const char* key, auto& field)
        {
            if (const auto value = config->getValue(base + key); ! value.isVoid())
            {
                field = static_cast<std::remove_reference_t<decltype(field)>>(static_cast<double>(value));
            }
        };
        const auto colour = [&](const char* key, juce::Colour& field)
        {
            if (const auto value = config->getValue(base + key); ! value.isVoid())
            {
                field = config->getColour(base + key, field);
            }
        };

        number(".size.width", style.width);
        number(".size.height", style.height);
        number(".cornerRadius", style.cornerRadius);
        number(".textSize", style.textSize);
        colour(".textColour", style.textColour);
        colour(".normalColour", style.normalColour);
        colour(".hoverColour", style.hoverColour);
        colour(".activeColour", style.activeColour);
        colour(".pressedColour", style.pressedColour);
        colour(".disabledColour", style.disabledColour);
        colour(".borderColour", style.borderColour);
        colour(".inset.colour", style.insetColour);
        number(".inset.opacity", style.insetOpacity);
        colour(".outerEdge.colour", style.outerEdgeColour);
        number(".outerEdge.opacity", style.outerEdgeOpacity);

        if (const auto value = config->getValue(base + ".legendPlacement"); ! value.isVoid())
        {
            const auto text = value.toString().trim().toLowerCase();
            if (text == "left")       style.legendPlacement = MixerToggleButton::Style::LegendPlacement::left;
            else if (text == "right") style.legendPlacement = MixerToggleButton::Style::LegendPlacement::right;
            else if (text == "below") style.legendPlacement = MixerToggleButton::Style::LegendPlacement::below;
        }
    };

    apply(sharedBase);
    apply(overrideBase);
    return style;
}
} // namespace px3::ui

InsertButton::InsertButton(const juce::String& legend)
    : MixerToggleButton(legend)
{
    setName(legend);
    setTooltip(legend == "EQ" ? "Bus EQ" : "Bus compressor");
}

void InsertButton::setInsertActive(bool active)
{
    if (insertActive == active)
    {
        return;
    }

    insertActive = active;
    repaint();
}

namespace
{
// Where the red begins, and the level at which the clip lamp latches.
constexpr float kClipThreshold = 0.94f;
}

void paintSurfaceNoise(juce::Graphics& g, juce::Rectangle<float> area, float amount)
{
    if (amount <= 0.0f || area.getWidth() < 2.0f || area.getHeight() < 2.0f)
    {
        return;
    }

    const auto x0 = static_cast<int>(area.getX());
    const auto y0 = static_cast<int>(area.getY());
    const auto x1 = static_cast<int>(area.getRight());
    const auto y1 = static_cast<int>(area.getBottom());

    juce::Graphics::ScopedSaveState clipped(g);
    g.reduceClipRegion(area.toNearestInt());

    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            // A cheap integer hash of the coordinate: stable frame to frame, and
            // uncorrelated enough between neighbours to look like grain.
            auto h = static_cast<juce::uint32>(x * 374761393 + y * 668265263);
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;

            if ((h & 3u) != 0u)
            {
                continue;   // only a quarter of the pixels carry a speck
            }

            const auto bright = (h & 4u) != 0u;
            const auto alpha = static_cast<juce::uint8>(juce::jlimit(0.0f, 255.0f,
                                                                     amount * 255.0f * (bright ? 0.55f : 0.85f)));
            g.setColour(bright ? juce::Colour::fromRGBA(255, 255, 255, alpha)
                               : juce::Colour::fromRGBA(0, 0, 0, alpha));
            g.fillRect(x, y, 1, 1);
        }
    }
}

void MixerLevelMeter::setLevel(float linearLevel)
{
    constexpr float minDb = -60.0f;
    float normalized = 0.0f;

    if (linearLevel > 0.0f)
    {
        const auto db = juce::Decibels::gainToDecibels(linearLevel, minDb);
        normalized = juce::jmap(juce::jlimit(minDb, 0.0f, db), minDb, 0.0f, 0.0f, 1.0f);
    }

    // Ballistics: snap up, ease down. Without this the bar tracks RMS exactly
    // and reads as jitter rather than as level.
    const auto coefficient = normalized > level ? style.riseCoefficient : style.fallCoefficient;
    level += (normalized - level) * juce::jlimit(0.0f, 1.0f, coefficient);
    if (std::abs(normalized - level) < 0.0005f)
    {
        level = normalized;
    }

    // Snap to silence rather than approaching it.
    //
    // An exponential fall never actually reaches zero, and the first segment
    // lights for ANY level above zero - so a note that ended a minute ago would
    // leave one green lamp on forever. Below half a segment there is nothing
    // left to show, so the meter reads empty.
    const auto silenceFloor = 0.5f / static_cast<float>(juce::jmax(1, style.segmentCount));
    if (normalized <= 0.0f && level < silenceFloor)
    {
        level = 0.0f;
    }

    // Peak hold and the clip lamp advance every call, not only when the level
    // moves - they are time based, so an unchanged level still has to age them.
    if (normalized >= peakLevel)
    {
        peakLevel = normalized;
        peakHoldCounter = style.peakHoldFrames;
    }
    else if (peakHoldCounter > 0)
    {
        --peakHoldCounter;
    }
    else
    {
        peakLevel = juce::jmax(normalized, peakLevel - style.peakFallPerFrame);
        if (normalized <= 0.0f && peakLevel < silenceFloor)
        {
            peakLevel = 0.0f;
        }
    }

    if (normalized >= kClipThreshold)
    {
        clipHoldCounter = style.clipHoldFrames;
    }
    else if (clipHoldCounter > 0)
    {
        --clipHoldCounter;
    }

    // Only redraw when the picture would differ. A segment is the smallest
    // visible step, so anything finer than that is invisible anyway.
    const auto segmentStep = 1.0f / static_cast<float>(juce::jmax(1, style.segmentCount));
    const auto moved = std::abs(level - paintedLevel) >= segmentStep * 0.25f
                       || std::abs(peakLevel - paintedPeak) >= segmentStep * 0.25f
                       || (clipHoldCounter > 0) != (paintedClip > 0);

    if (! moved)
    {
        return;
    }

    paintedLevel = level;
    paintedPeak = peakLevel;
    paintedClip = clipHoldCounter;
    repaint();
}

void MixerLevelMeter::applyStyle(const Style& styleIn)
{
    style = styleIn;
    repaint();
}

void MixerLevelMeter::paint(juce::Graphics& g)
{
    // An LED ladder, as a console meter is: a row of discrete lamps in a
    // recessed housing, green through amber to red, with a peak marker and a
    // clip lamp at the end.
    //
    // Unlit segments are drawn as dim versions of their own colour rather than
    // left empty. That is what makes it read as hardware at rest - you can see
    // the whole scale before any signal arrives.
    const auto area = getLocalBounds().toFloat();

    g.setColour(style.backgroundColour);
    g.fillRoundedRectangle(area, style.cornerRadius);
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 150));
    g.drawRoundedRectangle(area.reduced(0.5f), style.cornerRadius, 1.0f);

    paintSurfaceNoise(g, area, 0.04f);

    auto well = area.reduced(2.0f);
    if (well.getWidth() <= 0.0f || well.getHeight() <= 0.0f)
    {
        return;
    }

    // The clip lamp is its own segment at the far right, separated from the
    // ladder by a gap, exactly as it sits on a console.
    const auto clipLampWidth = juce::jmax(3.0f, well.getHeight() * 0.55f);
    const auto clipLamp = well.removeFromRight(clipLampWidth);
    well.removeFromRight(2.0f);

    const auto segments = juce::jmax(1, style.segmentCount);
    constexpr float segmentGap = 1.0f;
    const auto segmentWidth = (well.getWidth() - segmentGap * static_cast<float>(segments - 1))
                              / static_cast<float>(segments);

    if (segmentWidth > 0.0f)
    {
        const auto peakSegment = static_cast<int>(peakLevel * static_cast<float>(segments));
        const auto lampRadius = juce::jmin(1.6f, segmentWidth * 0.4f);

        for (int i = 0; i < segments; ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(segments - 1);
            const auto colour = t < 0.70f ? style.fillColour
                                          : (t < 0.88f ? style.highColour : style.clipColour);

            const auto lit = level * static_cast<float>(segments) > static_cast<float>(i);
            const auto isPeak = (i == peakSegment && peakLevel > 0.001f);

            const auto seg = juce::Rectangle<float>(well.getX() + static_cast<float>(i) * (segmentWidth + segmentGap),
                                                    well.getY(),
                                                    segmentWidth,
                                                    well.getHeight());

            // Every lamp sits in its own dark well, lit or not - the same
            // treatment the mute and solo switches use, so the whole strip
            // reads as one piece of hardware.
            g.setColour(juce::Colour::fromRGBA(0, 0, 0, 170));
            g.fillRoundedRectangle(seg.expanded(0.4f), lampRadius);

            if (lit || isPeak)
            {
                const auto lampColour = lit ? colour : colour.withAlpha(0.75f);

                // Bloom, so a lit lamp emits rather than just fills.
                g.setColour(lampColour.withAlpha(0.30f));
                g.fillRoundedRectangle(seg.expanded(1.2f, 0.8f), lampRadius);

                g.setColour(lampColour);
                g.fillRoundedRectangle(seg, lampRadius);

                // The same specular sheen across the top of each lamp.
                auto sheen = seg.withHeight(seg.getHeight() * 0.45f);
                juce::ColourGradient gloss(juce::Colour::fromRGBA(255, 255, 255, 96),
                                           sheen.getX(), sheen.getY(),
                                           juce::Colour::fromRGBA(255, 255, 255, 0),
                                           sheen.getX(), sheen.getBottom(), false);
                g.setGradientFill(gloss);
                g.fillRoundedRectangle(sheen, lampRadius);
            }
            else
            {
                g.setColour(colour.withMultipliedAlpha(0.15f));
                g.fillRoundedRectangle(seg, lampRadius);
            }
        }
    }

    // ---- clip lamp ---------------------------------------------------------
    const auto clipping = clipHoldCounter > 0;
    const auto clipRadius = juce::jmin(2.0f, clipLamp.getWidth() * 0.35f);

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 190));
    g.fillRoundedRectangle(clipLamp.expanded(0.6f), clipRadius);

    if (clipping)
    {
        g.setColour(style.clipColour.withAlpha(0.38f));
        g.fillRoundedRectangle(clipLamp.expanded(2.4f, 1.4f), clipRadius);
        g.setColour(style.clipColour.brighter(0.45f));
        g.fillRoundedRectangle(clipLamp, clipRadius);

        auto clipSheen = clipLamp.withHeight(clipLamp.getHeight() * 0.45f);
        juce::ColourGradient clipGloss(juce::Colour::fromRGBA(255, 255, 255, 120),
                                       clipSheen.getX(), clipSheen.getY(),
                                       juce::Colour::fromRGBA(255, 255, 255, 0),
                                       clipSheen.getX(), clipSheen.getBottom(), false);
        g.setGradientFill(clipGloss);
        g.fillRoundedRectangle(clipSheen, clipRadius);
    }
    else
    {
        g.setColour(style.clipColour.withMultipliedAlpha(0.16f));
        g.fillRoundedRectangle(clipLamp, clipRadius);
    }

    g.setColour(style.borderColour);
    g.drawRoundedRectangle(area, style.cornerRadius, 1.0f);
}
