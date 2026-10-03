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
#include "Theme.h"

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
    // The margin left for modulation rings scales with the knob, so a small
    // knob is not mostly margin.
    const auto side = juce::jmin(fullBounds.getWidth(), fullBounds.getHeight());
    const auto diameter = side - juce::jlimit(4.0f, 10.0f, side * 0.14f);
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

    // ---- the PX3 knob family ------------------------------------------------
    // A recessed track ring carrying the value arc, and a turned cap with a
    // pointer line. One drawing at every size; size is the only thing that
    // varies by importance. No per-paint noise or gloss: the depth comes from
    // a quiet gradient, an edge and a lit rim.
    namespace tc = px3::ui::theme::colour;
    const auto hot = slider.isEnabled() && slider.isMouseOverOrDragging();
    const auto trackRadius = radius * 0.90f;
    const auto trackWidth = juce::jlimit(2.0f, 3.6f, radius * 0.13f);
    const auto capRadius = radius * 0.70f;

    {
        juce::Path track;
        track.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(tc::knobTrack);
        g.strokePath(track, juce::PathStrokeType(trackWidth + 1.4f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Cap: soft contact shadow, body, edge, lit upper rim.
    const auto cap = juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f).withCentre(center);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(cap.translated(0.0f, 1.6f).expanded(0.6f));
    g.setGradientFill(juce::ColourGradient(renderGrayscale ? tc::knobCapTop.withSaturation(0.0f) : tc::knobCapTop,
                                           center.x, cap.getY(),
                                           tc::knobCapBottom, center.x, cap.getBottom(), false));
    g.fillEllipse(cap);
    g.setColour(tc::knobEdge);
    g.drawEllipse(cap, 1.0f);
    {
        juce::Path rim;
        rim.addCentredArc(center.x, center.y, capRadius - 1.2f, capRadius - 1.2f, 0.0f,
                          -1.9f, 1.9f, true);
        g.setColour(juce::Colours::white.withAlpha(hot ? 0.20f : 0.11f));
        g.strokePath(rim, juce::PathStrokeType(1.0f));
    }
    if (hot)
    {
        g.setColour(accentForHighlight.withAlpha(0.35f));
        g.drawEllipse(cap.expanded(1.2f), 1.0f);
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
            const auto arcRadius = trackRadius;
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
        // Bipolar ranges fill from their centre, so a detune or a pan-like
        // control at zero shows nothing lit - the value reads at a glance.
        const auto bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto fromAngle = bipolar
                                   ? rotaryStartAngle + static_cast<float>(slider.valueToProportionOfLength(0.0))
                                                            * (rotaryEndAngle - rotaryStartAngle)
                                   : rotaryStartAngle;
        if (std::abs(angle - fromAngle) > 0.01f)
        {
            juce::Path ring;
            ring.addCentredArc(center.x, center.y, trackRadius, trackRadius, 0.0f,
                               juce::jmin(fromAngle, angle), juce::jmax(fromAngle, angle), true);
            g.setColour(accentForHighlight.withAlpha(slider.isEnabled() ? 1.0f : 0.5f));
            g.strokePath(ring, juce::PathStrokeType(trackWidth, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
        else
        {
            const auto dot = juce::Point<float>(center.x + std::sin(angle) * trackRadius,
                                                center.y - std::cos(angle) * trackRadius);
            g.setColour(accentForHighlight.withAlpha(0.8f));
            g.fillEllipse(juce::Rectangle<float>(trackWidth, trackWidth).withCentre(dot));
        }
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

    {
        // Pointer: a line from near the centre to the cap's edge.
        const auto sn = std::sin(indicatorAngle);
        const auto cs = std::cos(indicatorAngle);
        g.setColour(renderGrayscale ? juce::Colour::fromRGB(170, 170, 170)
                                    : (slider.isEnabled() ? tc::knobPointer : tc::textSecondary));
        g.drawLine(center.x + sn * capRadius * 0.22f, center.y - cs * capRadius * 0.22f,
                   center.x + sn * (capRadius - 2.2f), center.y - cs * (capRadius - 2.2f),
                   juce::jlimit(1.6f, 2.6f, radius * 0.09f));
    }

    px3::ui::drawKnobOverlays(g, bounds, slider, renderGrayscale,
                              { macroAccent, macroLabelBackground, macroLabelText });
}


} // namespace px3::ui
