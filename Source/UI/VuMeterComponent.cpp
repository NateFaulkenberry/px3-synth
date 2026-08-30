#include "VuMeterComponent.h"

#include "UIConfig.h"

namespace px3::ui
{

namespace
{
juce::Colour cfg(const std::shared_ptr<const UIConfig>& c, const juce::String& path, juce::Colour fallback)
{
    return c != nullptr ? c->getColour(path, fallback) : fallback;
}

float cfgF(const std::shared_ptr<const UIConfig>& c, const juce::String& path, float fallback)
{
    return c != nullptr ? c->getFloat(path, fallback) : fallback;
}

// The face is drawn into an image at the display's scale, so the ticks and
// numbers stay sharp on a Retina panel instead of being upscaled.
float displayScale()
{
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        return static_cast<float>(display->scale);
    }
    return 1.0f;
}
} // namespace

VuMeterComponent::VuMeterComponent()
{
    setInterceptsMouseClicks(false, false);
    // 60 Hz for the animation. The METER's response is slow - 300 ms - but that
    // is a property of the movement, not of how often it is drawn, and at 24 a
    // needle crossing the scale arrives in visible steps.
    startTimerHz(60);
    lastFrameSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

VuMeterComponent::~VuMeterComponent()
{
    stopTimer();
}

void VuMeterComponent::setMode(Mode mode)
{
    if (meterMode == mode)
    {
        return;
    }

    meterMode = mode;
    rebuildFace();
    repaint();
}

void VuMeterComponent::setUIConfig(std::shared_ptr<const UIConfig> config)
{
    uiConfig = std::move(config);
    rebuildFace();
    repaint();
}

void VuMeterComponent::setBadgeText(juce::String text)
{
    badgeText = std::move(text);
    rebuildFace();
    repaint();
}

//==============================================================================
// calibration
//==============================================================================
double VuMeterComponent::positionForLevelDb(double dbfs)
{
    // The scale is linear in AMPLITUDE, as a moving-coil movement is, which is
    // why -20 crowds against the left stop while 0 to +3 spreads over the last
    // third. 0 VU sits at kZeroVuDbfs and +3 VU at the right stop.
    constexpr double kTopVu = 3.0;
    const auto vu = dbfs - kZeroVuDbfs;
    const auto amplitude = std::pow(10.0, juce::jlimit(-60.0, kTopVu, vu) / 20.0);
    const auto fullScale = std::pow(10.0, kTopVu / 20.0);
    return juce::jlimit(0.0, 1.0, amplitude / fullScale);
}

double VuMeterComponent::positionForReductionDb(double db)
{
    // No reduction rests at the right stop and the needle falls left as the
    // unit works - the same amplitude-linear movement, mirrored.
    return juce::jlimit(0.0, 1.0, std::pow(10.0, -juce::jmax(0.0, db) / 20.0));
}

//==============================================================================
// animation
//==============================================================================
void VuMeterComponent::timerCallback()
{
    // REAL elapsed time. JUCE timers are not guaranteed to fire on schedule,
    // and a physical simulation stepped with an assumed 1/60 would run at a
    // different speed whenever the host is busy.
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto dt = now - lastFrameSeconds;
    lastFrameSeconds = now;

    const auto live = isLive == nullptr || isLive();
    const auto target = (live && getTargetPosition != nullptr) ? getTargetPosition() : 0.0;

    movement.step(juce::jlimit(0.0, 1.0, target), dt);

    // Only redraw when the needle has actually moved far enough to change a
    // pixel. A still meter costs nothing.
    if (std::abs(movement.position() - lastDrawnPosition) > 0.0004)
    {
        lastDrawnPosition = movement.position();
        repaint(needleRegion());
    }
}

//==============================================================================
// geometry
//==============================================================================
juce::Path VuMeterComponent::needlePath(float length, float width)
{
    // A tapered blade, widest at the pivot and coming to a point, with a small
    // counterweight behind the pivot as a real movement carries. Built as a
    // path rather than a line so it has thickness that varies along it.
    juce::Path path;
    const auto half = width * 0.5f;

    path.startNewSubPath(-half, 0.0f);
    path.lineTo(-half * 0.28f, -length);
    path.lineTo(half * 0.28f, -length);
    path.lineTo(half, 0.0f);
    path.lineTo(half * 0.55f, length * 0.12f);
    path.lineTo(-half * 0.55f, length * 0.12f);
    path.closeSubPath();
    return path;
}

juce::Rectangle<int> VuMeterComponent::needleRegion() const
{
    // The whole sweep, expanded for the blade's width. Cheaper to invalidate
    // one generous rectangle than to compute the exact swept wedge each frame.
    return getLocalBounds();
}

void VuMeterComponent::resized()
{
    rebuildFace();
}

//==============================================================================
// painting
//==============================================================================
void VuMeterComponent::rebuildFace()
{
    const auto bounds = getLocalBounds();
    if (bounds.isEmpty())
    {
        face = {};
        return;
    }

    const auto scale = juce::jlimit(1.0f, 4.0f, displayScale());
    face = juce::Image(juce::Image::ARGB,
                       juce::roundToInt(static_cast<float>(bounds.getWidth()) * scale),
                       juce::roundToInt(static_cast<float>(bounds.getHeight()) * scale),
                       true);

    juce::Graphics g(face);
    g.addTransform(juce::AffineTransform::scale(scale));
    paintFace(g, bounds.toFloat().withPosition(0.0f, 0.0f));
}

void VuMeterComponent::paintFace(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    const auto bezelColour = cfg(uiConfig, "busInserts.comp.meterBezelColor", juce::Colour::fromRGB(26, 52, 96));
    const auto faceColour = cfg(uiConfig, "busInserts.comp.meterFaceColor", juce::Colour::fromRGB(238, 231, 210));
    const auto inkColour = cfg(uiConfig, "busInserts.comp.meterInkColor", juce::Colour::fromRGB(38, 36, 32));
    const auto hotColour = cfg(uiConfig, "busInserts.comp.meterHotColor", juce::Colour::fromRGB(178, 44, 38));
    const auto glowColour = cfg(uiConfig, "busInserts.comp.meterGlowColor", juce::Colour::fromRGBA(255, 244, 206, 210));

    g.setColour(bezelColour);
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 90));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    auto glass = bounds.reduced(cfgF(uiConfig, "busInserts.comp.meterBezelWidth", 7.0f));
    glass = glass.withTrimmedBottom(glass.getHeight() * 0.24f);

    g.setColour(faceColour);
    g.fillRect(glass);

    // The lamps behind the face. Revision H's movement is a light-box type.
    for (const auto x : { glass.getX() + glass.getWidth() * 0.28f,
                          glass.getX() + glass.getWidth() * 0.72f })
    {
        juce::ColourGradient lamp(glowColour, x, glass.getBottom(),
                                  glowColour.withAlpha(0.0f), x, glass.getY() - glass.getHeight() * 0.35f,
                                  true);
        g.setGradientFill(lamp);
        g.fillRect(glass);
    }

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 70));
    g.drawRect(glass, 1.0f);

    const auto arc = vuArcFor(glass);

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass.toNearestInt());

    struct Mark { double position; juce::String label; bool major; bool hot; };
    std::vector<Mark> marks;

    if (meterMode == Mode::gainReduction)
    {
        for (const auto db : { 0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 7.0, 10.0, 15.0, 20.0 })
        {
            const auto major = db == 0.0 || db == 5.0 || db == 10.0 || db == 20.0;
            marks.push_back({ positionForReductionDb(db),
                              major ? juce::String(juce::roundToInt(db)) : juce::String(),
                              major, db >= 10.0 });
        }
    }
    else
    {
        for (const auto vu : { -20.0, -10.0, -7.0, -5.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0 })
        {
            marks.push_back({ positionForLevelDb(kZeroVuDbfs + vu),
                              juce::String(juce::roundToInt(vu)), true, vu >= 0.0 });
        }
    }

    // The arc itself, in two passes so the hot end is red.
    for (const auto hot : { false, true })
    {
        juce::Path line;
        auto started = false;
        for (int i = 0; i <= 160; ++i)
        {
            const auto position = static_cast<double>(i) / 160.0;
            const auto isHot = meterMode == Mode::gainReduction
                                   ? position <= positionForReductionDb(10.0)
                                   : position >= positionForLevelDb(kZeroVuDbfs);
            if (isHot != hot)
            {
                started = false;
                continue;
            }

            const auto point = arc.pointForPosition(static_cast<float>(position), 0.86f);
            if (! started) { line.startNewSubPath(point); started = true; }
            else           { line.lineTo(point); }
        }

        g.setColour(hot ? hotColour : inkColour);
        g.strokePath(line, juce::PathStrokeType(1.4f));
    }

    g.setFont(juce::FontOptions(cfgF(uiConfig, "busInserts.comp.meterScaleFontSize", 7.5f), juce::Font::bold));

    for (const auto& mark : marks)
    {
        const auto colour = mark.hot ? hotColour : inkColour;
        const auto p = static_cast<float>(mark.position);

        g.setColour(colour.withAlpha(mark.major ? 0.95f : 0.6f));
        g.drawLine({ arc.pointForPosition(p, 0.86f),
                     arc.pointForPosition(p, mark.major ? 0.78f : 0.82f) },
                   mark.major ? 1.3f : 0.8f);

        if (mark.label.isNotEmpty())
        {
            g.setColour(colour);
            g.drawText(mark.label,
                       juce::Rectangle<float>(18.0f, 10.0f).withCentre(arc.pointForPosition(p, 0.70f)),
                       juce::Justification::centred, false);
        }
    }

    panel::drawLegend(g,
                      juce::Rectangle<float>(glass.getX(), glass.getBottom() - 14.0f,
                                             glass.getWidth(), 11.0f),
                      meterMode == Mode::gainReduction ? "GAIN REDUCTION" : "VU",
                      inkColour.withAlpha(0.55f), 6.5f);
}

void VuMeterComponent::paint(juce::Graphics& g)
{
    if (face.isValid())
    {
        g.drawImage(face, getLocalBounds().toFloat());
    }

    const auto bounds = getLocalBounds().toFloat();
    auto glass = bounds.reduced(cfgF(uiConfig, "busInserts.comp.meterBezelWidth", 7.0f));
    glass = glass.withTrimmedBottom(glass.getHeight() * 0.24f);
    if (glass.isEmpty())
    {
        return;
    }

    const auto arc = vuArcFor(glass);
    const auto live = isLive == nullptr || isLive();

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass.toNearestInt());

    // Fractional throughout: the angle is a double and nothing is rounded to a
    // pixel or a degree before it reaches the transform.
    const auto angle = static_cast<double>(arc.angleForPosition(
        static_cast<float>(juce::jlimit(0.0, 1.0, movement.position()))));

    // The needle and its pivot cap are drawn separately - one is a rotating
    // blade, the other a fixed disc over its centre - so each has its own
    // colour and its own vertical position. They shared a colour and a point
    // before, which meant neither could be adjusted without moving the other.
    const auto needleColour = cfg(uiConfig, "busInserts.comp.meterNeedle.color",
                                  juce::Colour::fromRGB(24, 24, 26));
    const auto width = cfgF(uiConfig, "busInserts.comp.meterNeedle.width", 1.6f) * 2.2f;
    const auto needleOffsetY = cfgF(uiConfig, "busInserts.comp.meterNeedle.offsetY", 0.0f);
    const auto needleOpacity = juce::jlimit(0.0f, 1.0f,
                                            cfgF(uiConfig, "busInserts.comp.meterNeedle.opacity", 1.0f));

    const auto baseColour = cfg(uiConfig, "busInserts.comp.meterNeedle.base.color", needleColour);
    const auto baseRadius = juce::jmax(0.0f, cfgF(uiConfig, "busInserts.comp.meterNeedle.base.radius",
                                                  juce::jmax(2.5f, width * 1.5f)));
    const auto baseOffsetY = cfgF(uiConfig, "busInserts.comp.meterNeedle.base.offsetY", 0.0f);
    const auto baseOpacity = juce::jlimit(0.0f, 1.0f,
                                          cfgF(uiConfig, "busInserts.comp.meterNeedle.base.opacity",
                                               needleOpacity));

    // As a FRACTION of the arc's radius, not a pixel count: the meter is sized
    // from its panel, so an absolute length would be right at one size and
    // wrong at every other. 1.0 reaches the scale's arc exactly.
    const auto length = arc.radius
                        * juce::jlimit(0.05f, 2.0f,
                                       cfgF(uiConfig, "busInserts.comp.meterNeedle.lengthScale", 0.97f));

    // Moving the needle's Y moves its PIVOT, so the blade swings about the new
    // point rather than being translated off its own arc.
    const auto pivot = arc.pivot.translated(0.0f, needleOffsetY);

    // Rotated about the PIVOT, not about a bounding box: the path is built with
    // its pivot at the origin and then moved there.
    const auto transform = juce::AffineTransform::rotation(static_cast<float>(angle))
                               .translated(pivot.getX(), pivot.getY());

    auto blade = needlePath(length, width);

    // A soft shadow just off the pivot axis, which is what stops the needle
    // reading as a sticker on the glass.
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, live ? 46 : 18));
    g.fillPath(blade, transform.translated(1.0f, 1.5f));

    g.setColour(needleColour.withAlpha(needleOpacity * (live ? 1.0f : 0.35f)));
    g.fillPath(blade, transform);

    // A highlight down one side of the blade.
    g.setColour(juce::Colours::white.withAlpha(live ? 0.16f : 0.05f));
    g.strokePath(blade, juce::PathStrokeType(0.6f), transform);

    // The cap, on its own centre. A radius of 0 removes it entirely, for a face
    // whose movement disappears behind the scale rather than sitting on it.
    if (baseRadius > 0.0f)
    {
        const auto baseCentre = arc.pivot.translated(0.0f, baseOffsetY);
        const auto capBounds = juce::Rectangle<float>(baseRadius * 2.0f, baseRadius * 2.0f)
                                   .withCentre(baseCentre);

        g.setColour(baseColour.withAlpha(baseOpacity * (live ? 1.0f : 0.35f)));
        g.fillEllipse(capBounds);
        g.setColour(juce::Colours::white.withAlpha(live ? 0.22f : 0.06f));
        g.drawEllipse(capBounds, 0.8f);
    }
}

} // namespace px3::ui
