// How the editor DRAWS: the shared rotary knob, and the editor's own paint.
//
// Split out of PluginEditor.cpp, which had grown to 4,900 lines. These are
// member functions of the same class, so this needs no change to the header -
// PluginEditorDebug.cpp has worked the same way for some time.
//
// The two things here have no state of their own and touch no other part of the
// editor's behaviour: one is a LookAndFeel method, the other paints the frame
// around whatever the child components draw themselves.

#include "MacroLook.h"
#include "ParameterKnob.h"
#include "PluginEditor.h"
#include "KnobOverlays.h"
#include "../DSP/WavetableLibrary.h"
#include "../DSP/WavetableImporter.h"
#include "../DSP/WavetableFactory.h"
#include "ModalBackdrop.h"
#include "RoundedRect.h"
#include "../DSP/PluginProcessorInternals.h"
#include "Card.h"
#include "BinaryData.h"
#include "PX3Version.h"
#include "UIConfig.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <mach/mach.h>

void PX3SynthAudioProcessorEditor::KnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                                     int x,
                                                                     int y,
                                                                     int width,
                                                                     int height,
                                                                     float sliderPos,
                                                                     float rotaryStartAngle,
                                                                     float rotaryEndAngle,
                                                                     juce::Slider& slider)
{
    const auto fullBounds = juce::Rectangle<float>(static_cast<float>(x),
                                                   static_cast<float>(y),
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));
    const auto diameter = juce::jmin(fullBounds.getWidth(), fullBounds.getHeight()) - 10.0f;
    const auto bounds = juce::Rectangle<float>(diameter, diameter).withCentre(fullBounds.getCentre());

    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto center = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto isMixerPanKnob = static_cast<bool>(slider.getProperties().getWithDefault("isMixerPanKnob", false));
    const auto accent = slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                            ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                            : juce::Colour::fromRGB(234, 166, 76);

    const auto psychedelicEnabled = static_cast<bool>(slider.getProperties().getWithDefault("psychedelicFx", false));
    const auto psychedelicGrayscale = static_cast<bool>(slider.getProperties().getWithDefault("psychedelicBypassGray", false));
    // DOOM's ring runs the complementary half of the wheel at a lower value, so
    // it reads as the same effect inverted rather than as a different one.
    const auto psychedelicInverted = static_cast<bool>(slider.getProperties().getWithDefault("psychedelicInverted", false));
    const auto knobBypassed = static_cast<bool>(slider.getProperties().getWithDefault("knobBypassed", false));
    const auto renderGrayscale = psychedelicGrayscale || knobBypassed;
    const auto psychedelicAmount = juce::jlimit(0.0f, 1.0f, sliderPos);
    const auto accentGrayValue = juce::jlimit(0.0f, 1.0f, accent.getPerceivedBrightness());
    const auto accentForHighlight = renderGrayscale
                                        ? juce::Colour::fromFloatRGBA(accentGrayValue, accentGrayValue, accentGrayValue, 1.0f)
                                        : accent;

    // Drop shadow for more tactile depth.
    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 110));
    g.fillEllipse(bounds.translated(0.0f, 3.0f));

    juce::ColourGradient knobGradient(juce::Colour::fromRGB(72, 72, 72),
                                      bounds.getX(),
                                      bounds.getY(),
                                      juce::Colour::fromRGB(34, 34, 34),
                                      bounds.getRight(),
                                      bounds.getBottom(),
                                      false);
    g.setGradientFill(knobGradient);
    g.fillEllipse(bounds);

    // Deterministic micro-grain adds realism without per-frame flicker.
    g.saveState();
    juce::Path grainMask;
    grainMask.addEllipse(bounds.reduced(2.6f));
    g.reduceClipRegion(grainMask);

    for (int i = 0; i < 48; ++i)
    {
        const auto seedA = static_cast<float>(i) * 12.9898f + center.x * 0.37f + center.y * 0.21f;
        const auto seedB = static_cast<float>(i) * 7.913f + center.x * 0.19f + center.y * 0.42f;
        const auto noiseA = std::sin(seedA) * 43758.5453f;
        const auto noiseB = std::sin(seedB) * 24141.1829f;
        const auto fracA = noiseA - std::floor(noiseA);
        const auto fracB = noiseB - std::floor(noiseB);

        const auto theta = fracA * juce::MathConstants<float>::twoPi;
        const auto radial = (0.22f + 0.70f * fracB) * radius;
        const auto dotX = center.x + std::cos(theta) * radial;
        const auto dotY = center.y + std::sin(theta) * radial;
        const auto dotSize = 0.55f + fracB * 0.9f;

        const auto isBright = fracA > 0.5f;
        const auto alpha = static_cast<juce::uint8>(isBright ? (20 + static_cast<int>(fracB * 22.0f))
                                                              : (16 + static_cast<int>(fracB * 18.0f)));
        g.setColour(isBright ? juce::Colour::fromRGBA(255, 255, 255, alpha)
                             : juce::Colour::fromRGBA(0, 0, 0, alpha));
        g.fillEllipse(dotX, dotY, dotSize, dotSize);
    }

    g.restoreState();

    // Top highlight to reinforce 3D curvature.
    juce::ColourGradient highlight(accentForHighlight.withAlpha(0.42f),
                                   center.x,
                                   bounds.getY(),
                                   accentForHighlight.withAlpha(0.0f),
                                   center.x,
                                   center.y,
                                   false);
    g.setGradientFill(highlight);
    g.fillEllipse(bounds.reduced(3.5f));

    g.setColour(juce::Colour::fromRGB(110, 110, 110));
    g.drawEllipse(bounds, 1.6f);

    g.setColour(juce::Colour::fromRGB(14, 14, 14));
    g.drawEllipse(bounds.expanded(0.6f), 0.9f);

    if (psychedelicEnabled && psychedelicAmount > 0.001f)
    {
        const auto t = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.0012);
        const auto glow = std::pow(psychedelicAmount, 0.8f);
        const auto borderRadius = radius + 1.8f;

        // Compact 3px rainbow border with a faint outer glow that brightens as the knob increases.
        for (int seg = 0; seg < 24; ++seg)
        {
            const auto segNorm = static_cast<float>(seg) / 24.0f;
            auto hue = std::fmod(segNorm + t * 0.12f, 1.0f);
            if (psychedelicInverted)
            {
                hue = std::fmod(hue + 0.5f, 1.0f);
            }
            const auto ringValue = psychedelicInverted ? 0.62f : 1.0f;
            const auto grayValue = juce::jmap(segNorm, 0.62f, 0.94f);
            const auto start = segNorm * juce::MathConstants<float>::twoPi;
            const auto span = juce::MathConstants<float>::twoPi / 24.0f * 0.88f;

            juce::Path arc;
            arc.addCentredArc(center.x,
                              center.y,
                              borderRadius,
                              borderRadius,
                              0.0f,
                              start,
                              start + span,
                              true);

            const auto glowAlpha = juce::jlimit(0.0f, 0.65f, 0.06f + glow * 0.34f);
            g.setColour(renderGrayscale
                            ? juce::Colour::fromFloatRGBA(grayValue, grayValue, grayValue, glowAlpha)
                            : juce::Colour::fromHSV(hue, 0.90f, ringValue, glowAlpha));
            g.strokePath(arc,
                         juce::PathStrokeType(5.4f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

            const auto borderAlpha = juce::jlimit(0.0f, 0.95f, 0.25f + glow * 0.62f);
            g.setColour(renderGrayscale
                            ? juce::Colour::fromFloatRGBA(grayValue, grayValue, grayValue, borderAlpha)
                            : juce::Colour::fromHSV(hue, 0.98f, ringValue, borderAlpha));
            g.strokePath(arc,
                         juce::PathStrokeType(3.0f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }
    }

    if (isMixerPanKnob)
    {
        // Scale ticks outside the knob, in the same spirit as the fader's:
        // hard left, centre, hard right. Angles use the pointer's convention -
        // 0 is straight up, positive is clockwise.
        const auto knobRadius = bounds.getWidth() * 0.5f;
        const auto tickInner = knobRadius + 3.0f;
        const auto tickOuter = tickInner + 8.0f;

        for (const auto tickAngle : { -juce::MathConstants<float>::halfPi,
                                      0.0f,
                                      juce::MathConstants<float>::halfPi })
        {
            const auto isCentre = std::abs(tickAngle) < 0.001f;
            const auto sn = std::sin(tickAngle);
            const auto cs = std::cos(tickAngle);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, isCentre ? 96 : 52));
            g.drawLine(center.x + sn * tickInner,
                       center.y - cs * tickInner,
                       center.x + sn * tickOuter,
                       center.y - cs * tickOuter,
                       isCentre ? 1.6f : 1.1f);
        }
    }

    float indicatorAngle = angle;
    if (isMixerPanKnob)
    {
        const auto topCenterAngle = -juce::MathConstants<float>::halfPi;
        const auto minValue = static_cast<float>(slider.getMinimum());
        const auto maxValue = static_cast<float>(slider.getMaximum());
        const auto valueSpan = juce::jmax(0.0001f, maxValue - minValue);
        const auto normalized = juce::jlimit(0.0f,
                                             1.0f,
                                             (static_cast<float>(slider.getValue()) - minValue) / valueSpan);
        const auto panValue = juce::jlimit(-1.0f, 1.0f, normalized * 2.0f - 1.0f);
        const auto panArcEndAngle = topCenterAngle + panValue * juce::MathConstants<float>::halfPi;
        indicatorAngle = panValue * juce::MathConstants<float>::halfPi;

        if (std::abs(panValue) > 0.001f)
        {
            juce::Path panRing;
            const auto arcRadius = radius * 0.88f;
            constexpr int steps = 24;
            for (int i = 0; i <= steps; ++i)
            {
                const auto t = static_cast<float>(i) / static_cast<float>(steps);
                const auto a = topCenterAngle + (panArcEndAngle - topCenterAngle) * t;
                const auto px = center.x + std::cos(a) * arcRadius;
                const auto py = center.y + std::sin(a) * arcRadius;
                if (i == 0)
                {
                    panRing.startNewSubPath(px, py);
                }
                else
                {
                    panRing.lineTo(px, py);
                }
            }

            const auto panBlue = renderGrayscale
                                     ? juce::Colour::fromRGB(200, 200, 200)
                                     : juce::Colour::fromRGB(86, 140, 255);
            g.setColour(panBlue);
            g.strokePath(panRing,
                         juce::PathStrokeType(3.2f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }
    }
    else
    {
        juce::Path ring;
        ring.addCentredArc(center.x,
                           center.y,
                           radius * 0.88f,
                           radius * 0.88f,
                           0.0f,
                           rotaryStartAngle,
                           angle,
                           true);
        g.setColour(accentForHighlight);
        g.strokePath(ring, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Where the value actually is once modulation is applied, when something is
    // driving this control.
    //
    // Drawn as an arc from the parameter's own position out to the modulated
    // one, rather than by moving the knob: the knob shows what the user set and
    // what a DAW would automate, and moving it would fight the parameter
    // attachment and write the modulation back into the parameter. The arc
    // reads as a range extending from the knob, which is also what makes the
    // DEPTH of the modulation visible and not just its instantaneous value.
    const auto modulatedPosition = static_cast<double>(
        slider.getProperties().getWithDefault("modulatedPos", -1.0));
    if (modulatedPosition >= 0.0 && ! renderGrayscale)
    {
        const auto modulatedAngle = rotaryStartAngle
                                    + static_cast<float>(modulatedPosition)
                                          * (rotaryEndAngle - rotaryStartAngle);

        if (std::abs(modulatedAngle - angle) > 0.006f)
        {
            juce::Path modulationArc;
            modulationArc.addCentredArc(center.x,
                                        center.y,
                                        radius * 1.02f,
                                        radius * 1.02f,
                                        0.0f,
                                        juce::jmin(angle, modulatedAngle),
                                        juce::jmax(angle, modulatedAngle),
                                        true);
            g.setColour(accentForHighlight.withAlpha(0.5f));
            g.strokePath(modulationArc, juce::PathStrokeType(2.0f,
                                                             juce::PathStrokeType::curved,
                                                             juce::PathStrokeType::rounded));
        }

        // A dot at the live value, so the current position is readable even when
        // the modulation depth is small enough that the arc is a sliver.
        const auto dotRadius = radius * 1.02f;
        g.setColour(accentForHighlight.brighter(0.35f));
        g.fillEllipse(center.x + std::sin(modulatedAngle) * dotRadius - 1.7f,
                      center.y - std::cos(modulatedAngle) * dotRadius - 1.7f,
                      3.4f, 3.4f);
    }

    juce::Path pointer;
    pointer.addRoundedRectangle(-2.1f, -radius * 0.56f, 4.2f, radius * 0.36f, 1.7f);
    g.setColour(renderGrayscale ? juce::Colour::fromRGB(200, 200, 200)
                                : juce::Colour::fromRGB(246, 246, 246));
    g.fillPath(pointer, juce::AffineTransform::rotation(indicatorAngle).translated(center.x, center.y));

    g.setColour(renderGrayscale ? juce::Colour::fromRGB(170, 170, 170)
                                : juce::Colour::fromRGB(210, 210, 210));
    g.fillEllipse(center.x - 3.1f, center.y - 3.1f, 6.2f, 6.2f);

    px3::ui::drawKnobOverlays(g, bounds, slider, renderGrayscale,
                              { macroAccent, macroLabelBackground, macroLabelText });
}


void PX3SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto bg = uiConfig != nullptr
                        ? uiConfig->getColour("editor.background.baseColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                        : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A);
    const auto stripRadius = uiConfig != nullptr ? uiConfig->getFloat("editor.topStrip.cornerRadius", 12.0f) : 12.0f;
    g.fillAll(bg);


    if (backgroundImage.isValid())
    {
        g.drawImageWithin(backgroundImage,
                          0,
                          0,
                          getWidth(),
                          getHeight(),
                          juce::RectanglePlacement::fillDestination,
                          false);

        const auto darkness = uiConfig != nullptr ? uiConfig->getInt("editor.background.imageDarkness", 150) : 150;
        const auto alpha = static_cast<juce::uint8>(juce::jlimit(0, 255, darkness));
        g.setColour(juce::Colour::fromRGBA(26, 26, 26, alpha));
        g.fillAll();
    }

    // The strip is filled AFTER the background image, not before it. Drawn
    // first, the image simply covered it - which is why only the logo panel,
    // the one piece painted after the image, had any background at all.
    //
    // One fill for the whole strip: there used to be an outline and four
    // vertical dividers marking the boundaries between logo, sections, presets
    // and menu, and with the sections butted together there is nothing left to
    // mark.
    g.setColour(uiConfig != nullptr
                    ? uiConfig->getColour("editor.topStrip.fillColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                    : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(topMenuStripArea.toFloat(), stripRadius);

    // The logo and master gain sections get the same hairline the tabs carry,
    // so every block in the bar is framed alike. Rounded here rather than
    // square because these two sit at the strip's rounded ends - a square
    // outline would cut across the corner.
    {
        const auto insetColour = uiConfig != nullptr
                                     ? uiConfig->getColour("topMenu.tabStyle.inset",
                                                           juce::Colour::fromRGBA(226, 232, 240, 40))
                                     : juce::Colour::fromRGBA(226, 232, 240, 40);
        const auto defaultRadius = juce::jmax(0.0f, stripRadius - 1.0f);

        // Per-corner radii for the INSET hairline itself - this is the only
        // border these two sections have. Nothing here fills, so changing a
        // radius moves the outline and leaves the background untouched. A
        // section that butts against the tabs can be square on that side and
        // follow the strip's rounding on the other.
        const auto strokeSection = [&](const juce::Rectangle<int>& section, const juce::String& path)
        {
            if (section.isEmpty())
            {
                return;
            }

            const auto radiusFor = [&](const char* corner)
            {
                return uiConfig != nullptr ? uiConfig->getFloat(path + "." + corner, defaultRadius)
                                           : defaultRadius;
            };

            const auto area = section.toFloat().reduced(1.0f);
            const auto tl = juce::jmax(0.0f, radiusFor("topLeft"));
            const auto tr = juce::jmax(0.0f, radiusFor("topRight"));
            const auto br = juce::jmax(0.0f, radiusFor("bottomRight"));
            const auto bl = juce::jmax(0.0f, radiusFor("bottomLeft"));

            juce::Path outline;
            outline.startNewSubPath(area.getX() + tl, area.getY());
            outline.lineTo(area.getRight() - tr, area.getY());
            if (tr > 0.0f) outline.addArc(area.getRight() - tr * 2.0f, area.getY(), tr * 2.0f, tr * 2.0f,
                                          0.0f, juce::MathConstants<float>::halfPi, false);
            outline.lineTo(area.getRight(), area.getBottom() - br);
            if (br > 0.0f) outline.addArc(area.getRight() - br * 2.0f, area.getBottom() - br * 2.0f, br * 2.0f, br * 2.0f,
                                          juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi, false);
            outline.lineTo(area.getX() + bl, area.getBottom());
            if (bl > 0.0f) outline.addArc(area.getX(), area.getBottom() - bl * 2.0f, bl * 2.0f, bl * 2.0f,
                                          juce::MathConstants<float>::pi, juce::MathConstants<float>::pi * 1.5f, false);
            outline.lineTo(area.getX(), area.getY() + tl);
            if (tl > 0.0f) outline.addArc(area.getX(), area.getY(), tl * 2.0f, tl * 2.0f,
                                          juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi, false);
            outline.closeSubPath();

            g.strokePath(outline, juce::PathStrokeType(1.0f));
        };

        g.setColour(insetColour);
        strokeSection(logoPanelArea, "topMenu.logoSection.inset.cornerRadius");
        strokeSection(topMenuGainArea, "topMenu.gainSection.inset.cornerRadius");
    }

    if (logoFrame.isValid())
    {
                // The version line used to sit under the logo; it is a menu item
                // now, so the logo has the panel to itself.
                // The 46px reserve was room for the version subtitle. With that
                // gone the logo fills the panel, which is what lets a shorter
                // header still show it at a sensible size.
                // How much shorter than its panel the logo sits - its breathing
                // room inside the section.
                const auto logoInset = uiConfig != nullptr ? uiConfig->getInt("editor.logo.heightInset", 20) : 20;
                const auto logoSize = static_cast<float>(juce::jlimit(40, 120, logoPanelArea.getHeight() - logoInset));
                // Centred on the panel outright, rather than derived from a content
                // height and a top offset. One expression, so changing the size
                // cannot move it off centre.
                const auto logoArea = juce::Rectangle<float>(logoSize, logoSize)
                                          .withCentre(logoPanelArea.toFloat().getCentre());
        const auto vibration = juce::jlimit(0.0f, 1.0f, logoVibrationIntensity);
        const auto shakePx = vibration * 3.2f;
        const auto shakeX = std::sin(logoVibrationPhase * 5.7f) * shakePx;
        const auto shakeY = std::cos(logoVibrationPhase * 7.9f + 0.8f) * (shakePx * 0.85f);
        // The logo is thin white strokes on transparency. Scaled down with the
        // default resampler those strokes average toward the transparent pixels
        // beside them and the whole mark reads dimmer - which is why it looked
        // darker once the header shrank.
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);

        auto transform = juce::AffineTransform::scale(logoArea.getWidth() / static_cast<float>(logoFrame.getWidth()),
                                                      logoArea.getHeight() / static_cast<float>(logoFrame.getHeight()))
                             .translated(logoArea.getX(), logoArea.getY());
        transform = transform.translated(shakeX, shakeY);
        g.drawImageTransformed(logoFrame, transform);

        // The GIF decoder hands back frame 0, which is one of the darker frames
        // in the loop - so the logo at rest reads dimmer than it does mid
        // animation, when the glitch masks are adding light on top of it.
        //
        // Redrawing the same frame builds up the alpha of its anti-aliased
        // strokes, which is what brightens them: a thin stroke at 40% alpha
        // becomes 64% after a second pass. Cheaper and truer to the artwork
        // than tinting it.
        const auto logoBoost = uiConfig != nullptr ? uiConfig->getFloat("editor.logo.boost", 0.55f) : 0.55f;
        if (logoBoost > 0.0f)
        {
            g.setOpacity(juce::jlimit(0.0f, 1.0f, logoBoost));
            g.drawImageTransformed(logoFrame, transform);
            g.setOpacity(1.0f);
        }

        if (logoVibrationIntensity > 0.01f
            && logoGlitchMaskR.isValid()
            && logoGlitchMaskG.isValid()
            && logoGlitchMaskB.isValid())
        {
            const auto glitchStrength = juce::jlimit(0.0f, 1.0f, logoVibrationIntensity);
            const auto split = 0.6f + glitchStrength * 3.0f;
            const auto phase = logoVibrationPhase;
            const auto offsetX = std::sin(phase * 8.7f) * split;
            const auto offsetY = std::cos(phase * 6.3f + 0.5f) * (split * 0.32f);
            const auto alpha = 0.17f + glitchStrength * 0.40f;

            g.setOpacity(alpha);
            g.drawImageTransformed(logoGlitchMaskR,
                                   transform.translated(-offsetX * 0.95f, -offsetY * 0.45f));
            g.setOpacity(alpha * 0.72f);
            g.drawImageTransformed(logoGlitchMaskG,
                                   transform.translated(offsetX * 0.22f, offsetY * 0.20f));
            g.setOpacity(alpha);
            g.drawImageTransformed(logoGlitchMaskB,
                                   transform.translated(offsetX, offsetY * 0.45f));
            g.setOpacity(1.0f);
        }
    }

    // The FX section cards are painted by FxPanel itself - see
    // FxPanel::paintSectionCards. They live inside that panel's rectangle, and
    // six panels share that rectangle and are swapped by visibility, so
    // decoration painted from here was not tied to the panel's lifetime and
    // could survive a switch to another panel as stale outlines.


    // The strip BEHIND the performance controls and the keyboard.
    //
    // A third layer, easy to forget: a translucent gradient, a hairline outline
    // and a divider, spanning both components. Its corners were fixed at 12px
    // while the two components on top of it were square, so as soon as either
    // of them was rounded this showed through the gap - which is exactly what
    // "something is peeking past the corners" is.
    //
    // Every part of it is configurable now, including off, since it is the
    // layer most likely to be in the way of a rounded panel above it.
    const auto stripEnabled = uiConfig == nullptr || uiConfig->getBool("performanceStrip.enabled", true);

    if (stripEnabled && performanceControlsArea.getWidth() > 0 && pianoKeyboard.getBounds().getWidth() > 0)
    {
        // The KEYS, not the keyboard component. The component is taller than
        // the instrument it draws - it carries transparent headroom above the
        // keys so the sparks are not clipped - and taking its raw bounds here
        // dragged this gradient and its outline up into that headroom, where it
        // read as a stray panel floating above the keyboard.
        const auto keys = pianoKeyboard.keyboardArea() + pianoKeyboard.getPosition();
        const auto performanceStrip = performanceControlsArea.getUnion(keys).toFloat();

        const auto radii = px3::ui::CornerRadii::fromConfig(uiConfig.get(), "performanceStrip",
                                                            px3::ui::CornerRadii::all(12.0f));

        // A solid fill, not a gradient. The diagonal ramp it used to carry read
        // as a panel laid on top of the bar rather than as part of it, and it
        // could not be matched to anything else in the interface - every other
        // surface here is a flat colour with an opacity.
        const auto fill = uiConfig != nullptr
                              ? uiConfig->getColour("performanceStrip.background.color",
                                                    juce::Colour::fromRGB(20, 20, 20))
                              : juce::Colour::fromRGB(20, 20, 20);
        const auto fillOpacity = uiConfig != nullptr
                                     ? uiConfig->getFloat("performanceStrip.background.opacity", 1.0f)
                                     : 1.0f;

        g.setColour(fill.withMultipliedAlpha(juce::jlimit(0.0f, 1.0f, fillOpacity)));
        px3::ui::fillRounded(g, performanceStrip, radii);

        const auto outlineWidth = uiConfig != nullptr
                                      ? uiConfig->getFloat("performanceStrip.outline.width", 1.0f)
                                      : 1.0f;
        if (outlineWidth > 0.0f)
        {
            g.setColour(uiConfig != nullptr
                            ? uiConfig->getColour("performanceStrip.outline.color",
                                                  juce::Colour::fromRGBA(255, 255, 255, 50))
                            : juce::Colour::fromRGBA(255, 255, 255, 50));
            px3::ui::drawRounded(g, performanceStrip, radii, outlineWidth);
        }

        const auto dividerWidth = uiConfig != nullptr
                                      ? uiConfig->getFloat("performanceStrip.divider.width", 1.0f)
                                      : 1.0f;
        if (dividerWidth > 0.0f)
        {
            const auto inset = uiConfig != nullptr
                                   ? uiConfig->getFloat("performanceStrip.divider.inset", 8.0f)
                                   : 8.0f;
            const auto dividerX = static_cast<float>(performanceControlsArea.getRight() + 3);
            g.setColour(uiConfig != nullptr
                            ? uiConfig->getColour("performanceStrip.divider.color",
                                                  juce::Colour::fromRGBA(255, 255, 255, 38))
                            : juce::Colour::fromRGBA(255, 255, 255, 38));
            g.drawLine(dividerX,
                       performanceStrip.getY() + inset,
                       dividerX,
                       performanceStrip.getBottom() - inset,
                       dividerWidth);
        }
    }

}
