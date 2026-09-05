// How every PX3 knob is drawn.
//
// Was PluginEditorLook.cpp inside the Synth, holding this and the editor's own
// paint. The knob is the ecosystem's visual language rather than the Synth's -
// an effect product drawing JUCE's default rotary would be a PX3 panel with
// somebody else's controls in it - so it moved here and the editor's paint
// stayed behind with the editor.

#include "KnobLookAndFeel.h"

#include "MacroLook.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "ChipLabel.h"
#include "RoundedRect.h"
#include "UIConfig.h"

#include <algorithm>
#include <cmath>

namespace px3::ui
{

void KnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
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

    const auto psychedelicGrayscale = static_cast<bool>(slider.getProperties().getWithDefault("psychedelicBypassGray", false));
    const auto knobBypassed = static_cast<bool>(slider.getProperties().getWithDefault("knobBypassed", false));
    const auto renderGrayscale = psychedelicGrayscale || knobBypassed;
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


} // namespace px3::ui
