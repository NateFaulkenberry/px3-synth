#include "VuMeterComponent.h"

#include "Theme.h"
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
VuMeterComponent::NeedleAim VuMeterComponent::aimAt(const px3::ui::VuArc& arc,
                                                    double position,
                                                    float pivotOffsetY,
                                                    float lengthScale)
{
    // The point on the scale this reading corresponds to - the mark the needle
    // has to indicate.
    const auto target = arc.pointForPosition(static_cast<float>(juce::jlimit(0.0, 1.0, position)), 1.0f);
    const auto pivot = arc.pivot.translated(0.0f, pivotOffsetY);

    const auto delta = target - pivot;
    const auto distance = delta.getDistanceFromOrigin();

    if (distance < 1.0e-4f)
    {
        return { 0.0f, 0.0f };
    }

    // The blade is built pointing up, and the arc's own convention is
    // direction = (sin a, -cos a), so the angle that points along `delta` is
    // atan2(dx, -dy).
    return { std::atan2(delta.getX(), -delta.getY()), distance * lengthScale };
}

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

// The display inside the bezel. The whole of it: the old face left a quarter
// of the bezel empty under the glass.
juce::Rectangle<float> VuMeterComponent::glassIn(juce::Rectangle<float> bounds) const
{
    return bounds.reduced(cfgF(uiConfig, "busInserts.comp.meterBezelWidth", 3.0f));
}

void VuMeterComponent::paintFace(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    // A display, in the instrument's own terms: a dark inset face with a thin
    // square bezel, light ink and a red hot zone - the same family as every
    // graph and wave display, rather than a cream light-box in a navy frame.
    namespace th = theme;
    const auto bezelColour = cfg(uiConfig, "busInserts.comp.meterBezelColor", th::colour::panelEdge);
    const auto faceColour = cfg(uiConfig, "busInserts.comp.meterFaceColor", th::colour::inset);
    const auto inkColour = cfg(uiConfig, "busInserts.comp.meterInkColor", th::colour::textLabel);
    const auto hotColour = cfg(uiConfig, "busInserts.comp.meterHotColor", juce::Colour::fromRGB(232, 87, 78));
    const auto glowColour = cfg(uiConfig, "busInserts.comp.meterGlowColor", juce::Colour::fromRGBA(120, 186, 255, 22));

    g.setColour(bezelColour);
    g.fillRect(bounds);

    const auto glass = glassIn(bounds);

    g.setColour(faceColour);
    g.fillRect(glass);

    // A faint backlight rising from below, so the face reads as lit glass
    // rather than as a flat fill.
    for (const auto x : { glass.getX() + glass.getWidth() * 0.28f,
                          glass.getX() + glass.getWidth() * 0.72f })
    {
        juce::ColourGradient lamp(glowColour, x, glass.getBottom(),
                                  glowColour.withAlpha(0.0f), x, glass.getY() - glass.getHeight() * 0.35f,
                                  true);
        g.setGradientFill(lamp);
        g.fillRect(glass);
    }

    g.setColour(th::colour::insetLight);
    g.fillRect(glass.withHeight(1.0f));

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

    g.setFont(th::font(th::Type::display).withHeight(cfgF(uiConfig, "busInserts.comp.meterScaleFontSize", 8.5f)));

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

    th::drawLabel(g, meterMode == Mode::gainReduction ? "GAIN REDUCTION" : "VU",
                  juce::Rectangle<float>(glass.getX(), glass.getBottom() - 18.0f, glass.getWidth(), 12.0f),
                  th::Type::secondary, th::colour::textSecondary);
}

void VuMeterComponent::paint(juce::Graphics& g)
{
    if (face.isValid())
    {
        g.drawImage(face, getLocalBounds().toFloat());
    }

    const auto bounds = getLocalBounds().toFloat();
    const auto glass = glassIn(bounds);
    if (glass.isEmpty())
    {
        return;
    }

    const auto arc = vuArcFor(glass);
    const auto live = isLive == nullptr || isLive();

    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(glass.toNearestInt());

    // Fractional throughout: nothing is rounded to a pixel or a degree before
    // it reaches the transform.
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

    // Aimed at the mark, from wherever the pivot has been moved to. Both the
    // angle and the length come out of that, so the tip stays on the scale at
    // any offset - see aimAt.
    //
    // lengthScale is a FRACTION of the distance to the mark, not a pixel count:
    // the meter is sized from its panel, so an absolute length would be right
    // at one size and wrong at every other. 1.0 reaches the mark exactly.
    const auto lengthScale = juce::jlimit(0.05f, 2.0f,
                                          cfgF(uiConfig, "busInserts.comp.meterNeedle.lengthScale", 0.97f));
    const auto aim = aimAt(arc, juce::jlimit(0.0, 1.0, movement.position()), needleOffsetY, lengthScale);
    const auto pivot = arc.pivot.translated(0.0f, needleOffsetY);

    // Rotated about the PIVOT, not about a bounding box: the path is built with
    // its pivot at the origin and then moved there.
    const auto transform = juce::AffineTransform::rotation(aim.angleRadians)
                               .translated(pivot.getX(), pivot.getY());

    auto blade = needlePath(aim.length, width);

    // Bypassed, the movement fades as ONE object. Fading each part on its own
    // let the blade, its shadow and the cap show through one another, so the
    // overlap at the pivot read darker than either - a glass needle.
    const auto layered = ! live;
    if (layered) { g.beginTransparencyLayer(0.35f); }

    // A soft shadow just off the pivot axis, which is what stops the needle
    // reading as a sticker on the glass.
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 46));
    g.fillPath(blade, transform.translated(1.0f, 1.5f));

    g.setColour(needleColour.withAlpha(needleOpacity));
    g.fillPath(blade, transform);

    // A highlight down one side of the blade.
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.strokePath(blade, juce::PathStrokeType(0.6f), transform);

    // The cap, on its own centre. A radius of 0 removes it entirely, for a face
    // whose movement disappears behind the scale rather than sitting on it.
    if (baseRadius > 0.0f)
    {
        const auto baseCentre = arc.pivot.translated(0.0f, baseOffsetY);
        const auto capBounds = juce::Rectangle<float>(baseRadius * 2.0f, baseRadius * 2.0f)
                                   .withCentre(baseCentre);

        g.setColour(baseColour.withAlpha(baseOpacity));
        g.fillEllipse(capBounds);
        g.setColour(juce::Colours::white.withAlpha(0.22f));
        g.drawEllipse(capBounds, 0.8f);
    }

    if (layered) { g.endTransparencyLayer(); }
}

} // namespace px3::ui
