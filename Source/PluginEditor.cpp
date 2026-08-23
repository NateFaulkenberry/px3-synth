#include "PluginEditor.h"

#include "BinaryData.h"
#include "PX3Version.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

namespace
{
const std::array<const char*, 5> kGroupNames { "OSC", "FILTER", "AMP ENV", "LFO", "OUTPUT" };
const std::array<juce::Colour, 5> kGroupAccents {
    juce::Colour::fromRGB(74, 153, 255),   // OSC: blue
    juce::Colour::fromRGB(255, 88, 88),    // FILTER: red
    juce::Colour::fromRGB(73, 222, 121),   // AMP ENV: green
    juce::Colour::fromRGB(186, 112, 255),  // LFO: purple
    juce::Colour::fromRGB(255, 216, 74)    // OUTPUT: yellow
};

constexpr int kFxSectionDrive = 0;
constexpr int kFxSectionDelay = 1;
constexpr int kFxSectionReverb = 2;

juce::String moduleIdFromSectionId(int sectionId)
{
    switch (sectionId)
    {
        case kFxSectionDelay:
            return "delay";
        case kFxSectionReverb:
            return "reverb";
        case kFxSectionDrive:
        default:
            return "harmonicDrive";
    }
}

int sectionIdFromModuleId(const juce::String& moduleId)
{
    if (moduleId.equalsIgnoreCase("delay"))
    {
        return kFxSectionDelay;
    }
    if (moduleId.equalsIgnoreCase("reverb"))
    {
        return kFxSectionReverb;
    }
    if (moduleId.equalsIgnoreCase("harmonicDrive"))
    {
        return kFxSectionDrive;
    }
    return -1;
}

class DebugPanelWindow final : public juce::DocumentWindow
{
public:
    DebugPanelWindow()
        : juce::DocumentWindow("P(X3) DEBUG CONSOLE",
                               juce::Colour::fromRGB(18, 18, 18),
                               juce::DocumentWindow::allButtons)
    {
    }

    std::function<void()> onCloseRequested;

    void closeButtonPressed() override
    {
        if (onCloseRequested != nullptr)
        {
            onCloseRequested();
        }
    }
};
}

// Interactive ADSR editor that writes directly to existing processor parameters.
// It intentionally does not own independent envelope state so automation,
// presets, and DAW restore all remain canonical.
class SynthProjectAudioProcessorEditor::EnvelopeGraphComponent final : public juce::Component
{
public:
    EnvelopeGraphComponent(juce::AudioParameterFloat& attackIn,
                           juce::AudioParameterFloat& decayIn,
                           juce::AudioParameterFloat& sustainIn,
                           juce::AudioParameterFloat& releaseIn,
                           juce::Colour accentIn)
        : attack(attackIn), decay(decayIn), sustain(sustainIn), release(releaseIn), accent(accentIn)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        refreshFromParameters();
    }

    void refreshFromParameters()
    {
        const auto a = attack.get();
        const auto d = decay.get();
        const auto s = sustain.get();
        const auto r = release.get();

        if (std::abs(a - lastAttack) > 0.0001f
            || std::abs(d - lastDecay) > 0.0001f
            || std::abs(s - lastSustain) > 0.0001f
            || std::abs(r - lastRelease) > 0.0001f)
        {
            lastAttack = a;
            lastDecay = d;
            lastSustain = s;
            lastRelease = r;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(4.0f);
        if (area.isEmpty())
        {
            return;
        }

        g.setColour(juce::Colour::fromRGBA(10, 18, 10, 155));
        g.fillRoundedRectangle(area, 8.0f);
        g.setColour(accent.withAlpha(0.28f));
        g.drawRoundedRectangle(area, 8.0f, 1.0f);

        const auto geom = computeGeometry();

        // Subtle grid for time/level reference.
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 24));
        for (int i = 1; i < 6; ++i)
        {
            const auto x = juce::jmap(static_cast<float>(i), 0.0f, 6.0f, geom.left, geom.right);
            g.drawVerticalLine(static_cast<int>(std::lround(x)), geom.top, geom.bottom);
        }

        for (int i = 0; i <= 4; ++i)
        {
            const auto y = juce::jmap(static_cast<float>(i), 0.0f, 4.0f, geom.top, geom.bottom);
            g.drawHorizontalLine(static_cast<int>(std::lround(y)), geom.left, geom.right);
        }

        g.setColour(juce::Colour::fromRGBA(210, 210, 210, 75));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText("100%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>(geom.top) - 6, 32, 12), juce::Justification::centredRight);
        g.drawText("50%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>((geom.top + geom.bottom) * 0.5f) - 6, 32, 12), juce::Justification::centredRight);
        g.drawText("0%", juce::Rectangle<int>(static_cast<int>(geom.left) - 34, static_cast<int>(geom.bottom) - 6, 32, 12), juce::Justification::centredRight);

        juce::Path envPath;
        envPath.startNewSubPath(geom.start);
        envPath.lineTo(geom.attackPoint);
        envPath.lineTo(geom.decaySustainPoint);
        envPath.lineTo(geom.releasePoint);
        envPath.lineTo(geom.end);

        g.setColour(accent.withAlpha(0.28f));
        g.strokePath(envPath, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour(accent.brighter(0.25f));
        g.strokePath(envPath, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        drawHandleMarker(g, geom.attackPoint, DragHandle::attack);
        drawHandleMarker(g, geom.decaySustainPoint, DragHandle::decaySustain);
        drawHandleMarker(g, geom.releasePoint, DragHandle::release);

        // Draw labels above curve/markers.
        drawHandleLabel(g, geom.attackPoint, DragHandle::attack, "A");
        drawHandleLabel(g, geom.decaySustainPoint, DragHandle::decaySustain, "D/S");
        drawHandleLabel(g, geom.releasePoint, DragHandle::release, "R");

        const auto active = dragHandle != DragHandle::none ? dragHandle : hoverHandle;
        if (active != DragHandle::none)
        {
            const auto handlePos = handlePositionFor(active, geom);
            const auto text = valueTextForHandle(active);

            auto bubble = juce::Rectangle<float>(0.0f, 0.0f, 122.0f, 30.0f);
            bubble.setCentre(handlePos.translated(0.0f, -24.0f));
            bubble = bubble.withPosition(juce::jlimit(area.getX(), area.getRight() - bubble.getWidth(), bubble.getX()),
                                         juce::jlimit(area.getY(), area.getBottom() - bubble.getHeight(), bubble.getY()));

            g.setColour(juce::Colour::fromRGBA(9, 14, 9, 232));
            g.fillRoundedRectangle(bubble, 6.0f);
            g.setColour(accent.withAlpha(0.78f));
            g.drawRoundedRectangle(bubble, 6.0f, 1.0f);
            g.setColour(juce::Colour::fromRGB(232, 242, 232));
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
            g.drawText(text, bubble.toNearestInt(), juce::Justification::centred);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        hoverHandle = pickHandle(event.position, computeGeometry());
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (dragHandle == DragHandle::none)
        {
            hoverHandle = DragHandle::none;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto geom = computeGeometry();
        dragHandle = pickHandle(event.position, geom);
        hoverHandle = dragHandle;
        if (dragHandle == DragHandle::none)
        {
            return;
        }

        if (dragHandle == DragHandle::attack)
        {
            attack.beginChangeGesture();
        }
        else if (dragHandle == DragHandle::decaySustain)
        {
            decay.beginChangeGesture();
            sustain.beginChangeGesture();
        }
        else if (dragHandle == DragHandle::release)
        {
            release.beginChangeGesture();
        }

        applyDragPosition(event.position, geom);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragHandle == DragHandle::none)
        {
            return;
        }

        applyDragPosition(event.position, computeGeometry());
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragHandle == DragHandle::attack)
        {
            attack.endChangeGesture();
        }
        else if (dragHandle == DragHandle::decaySustain)
        {
            decay.endChangeGesture();
            sustain.endChangeGesture();
        }
        else if (dragHandle == DragHandle::release)
        {
            release.endChangeGesture();
        }

        dragHandle = DragHandle::none;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto handle = pickHandle(event.position, computeGeometry());
        if (handle == DragHandle::none)
        {
            return;
        }

        if (handle == DragHandle::attack)
        {
            attack.beginChangeGesture();
            attack.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(attack).getDefaultValue());
            attack.endChangeGesture();
        }
        else if (handle == DragHandle::decaySustain)
        {
            decay.beginChangeGesture();
            sustain.beginChangeGesture();
            decay.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(decay).getDefaultValue());
            sustain.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(sustain).getDefaultValue());
            decay.endChangeGesture();
            sustain.endChangeGesture();
        }
        else if (handle == DragHandle::release)
        {
            release.beginChangeGesture();
            release.setValueNotifyingHost(static_cast<juce::RangedAudioParameter&>(release).getDefaultValue());
            release.endChangeGesture();
        }

        refreshFromParameters();
    }

private:
    enum class DragHandle
    {
        none,
        attack,
        decaySustain,
        release
    };

    struct Geometry
    {
        float left { 0.0f };
        float right { 0.0f };
        float top { 0.0f };
        float bottom { 0.0f };
        float attackRangeWidth { 1.0f };
        float releaseRangeWidth { 1.0f };
        float minDecayGap { 1.0f };
        float minSustainWidth { 1.0f };
        juce::Point<float> start;
        juce::Point<float> attackPoint;
        juce::Point<float> decaySustainPoint;
        juce::Point<float> releasePoint;
        juce::Point<float> end;
    };

    static float clamp01(float v)
    {
        return juce::jlimit(0.0f, 1.0f, v);
    }

    static float timeToVisualNorm(float seconds, float minValue, float maxValue)
    {
        const auto clamped = juce::jlimit(minValue, maxValue, seconds);
        const auto denom = std::log(juce::jmax(minValue * 1.001f, maxValue) / minValue);
        if (denom <= 0.0f)
        {
            return 0.0f;
        }
        return clamp01(std::log(clamped / minValue) / denom);
    }

    static float visualNormToTime(float norm, float minValue, float maxValue)
    {
        const auto clampedNorm = clamp01(norm);
        return minValue * std::pow(maxValue / minValue, clampedNorm);
    }

    Geometry computeGeometry() const
    {
        Geometry geom;
        const auto area = getLocalBounds().toFloat().reduced(10.0f, 8.0f);

        geom.left = area.getX() + 26.0f;
        geom.right = area.getRight() - 10.0f;
        geom.top = area.getY() + 8.0f;
        geom.bottom = area.getBottom() - 14.0f;

        const auto totalWidth = juce::jmax(20.0f, geom.right - geom.left);
        geom.attackRangeWidth = totalWidth * 0.30f;
        geom.releaseRangeWidth = totalWidth * 0.30f;
        geom.minDecayGap = juce::jmax(10.0f, totalWidth * 0.04f);
        geom.minSustainWidth = juce::jmax(12.0f, totalWidth * 0.10f);

        const auto attackRange = attack.getNormalisableRange();
        const auto decayRange = decay.getNormalisableRange();
        const auto releaseRange = release.getNormalisableRange();

        const auto attackNorm = timeToVisualNorm(attack.get(), attackRange.start, attackRange.end);
        const auto decayNorm = timeToVisualNorm(decay.get(), decayRange.start, decayRange.end);
        const auto releaseNorm = timeToVisualNorm(release.get(), releaseRange.start, releaseRange.end);
        const auto sustainNorm = clamp01(sustain.get());

        const auto xAttack = geom.left + attackNorm * geom.attackRangeWidth;
        auto xRelease = geom.right - releaseNorm * geom.releaseRangeWidth;
        xRelease = juce::jmax(xAttack + geom.minDecayGap + geom.minSustainWidth, xRelease);

        const auto xDecayMin = xAttack + geom.minDecayGap;
        const auto xDecayMax = juce::jmax(xDecayMin + 1.0f, xRelease - geom.minSustainWidth);
        const auto xDecay = xDecayMin + decayNorm * (xDecayMax - xDecayMin);
        const auto ySustain = juce::jmap(sustainNorm, geom.bottom, geom.top);

        geom.start = { geom.left, geom.bottom };
        geom.attackPoint = { xAttack, geom.top };
        geom.decaySustainPoint = { xDecay, ySustain };
        geom.releasePoint = { xRelease, ySustain };
        geom.end = { geom.right, geom.bottom };
        return geom;
    }

    static float distSq(juce::Point<float> a, juce::Point<float> b)
    {
        const auto dx = a.getX() - b.getX();
        const auto dy = a.getY() - b.getY();
        return dx * dx + dy * dy;
    }

    DragHandle pickHandle(juce::Point<float> p, const Geometry& geom) const
    {
        constexpr float hitRadius = 13.0f;
        const auto hitSq = hitRadius * hitRadius;

        const auto a = distSq(p, geom.attackPoint);
        const auto ds = distSq(p, geom.decaySustainPoint);
        const auto r = distSq(p, geom.releasePoint);

        auto best = DragHandle::none;
        auto bestSq = hitSq;

        if (a <= bestSq)
        {
            bestSq = a;
            best = DragHandle::attack;
        }
        if (ds <= bestSq)
        {
            bestSq = ds;
            best = DragHandle::decaySustain;
        }
        if (r <= bestSq)
        {
            best = DragHandle::release;
        }

        return best;
    }

    juce::Point<float> handlePositionFor(DragHandle handle, const Geometry& geom) const
    {
        if (handle == DragHandle::attack)
        {
            return geom.attackPoint;
        }
        if (handle == DragHandle::decaySustain)
        {
            return geom.decaySustainPoint;
        }
        if (handle == DragHandle::release)
        {
            return geom.releasePoint;
        }
        return geom.start;
    }

    void drawHandleMarker(juce::Graphics& g, juce::Point<float> center, DragHandle handle) const
    {
        const auto active = (handle == dragHandle) || (handle == hoverHandle);
        const auto radius = active ? 6.0f : 5.0f;
        g.setColour(accent.withAlpha(active ? 1.0f : 0.86f));
        g.fillEllipse(center.getX() - radius, center.getY() - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colour::fromRGBA(12, 12, 12, 220));
        g.drawEllipse(center.getX() - radius, center.getY() - radius, radius * 2.0f, radius * 2.0f, 1.2f);
    }

    void drawHandleLabel(juce::Graphics& g,
                         juce::Point<float> center,
                         DragHandle handle,
                         const juce::String& id) const
    {
        const auto active = (handle == dragHandle) || (handle == hoverHandle);
        auto labelBounds = juce::Rectangle<float>(center.getX() - 14.0f, center.getY() + 6.0f, 28.0f, 12.0f);

        g.setColour(juce::Colour::fromRGBA(6, 12, 6, active ? 238 : 212));
        g.fillRoundedRectangle(labelBounds, 3.0f);
        g.setColour(accent.withAlpha(active ? 0.9f : 0.72f));
        g.drawRoundedRectangle(labelBounds, 3.0f, 0.9f);

        g.setColour(juce::Colour::fromRGBA(238, 248, 238, active ? 250 : 236));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(id,
                   labelBounds.toNearestInt(),
                   juce::Justification::centred);
    }

    juce::String valueTextForHandle(DragHandle handle) const
    {
        if (handle == DragHandle::attack)
        {
            const auto sec = attack.get();
            const auto ms = sec * 1000.0f;
            return "ATTACK " + (sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s");
        }

        if (handle == DragHandle::decaySustain)
        {
            const auto sec = decay.get();
            const auto ms = sec * 1000.0f;
            const auto sustainPct = sustain.get() * 100.0f;
            const auto decayText = sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s";
            return "D " + decayText + " | S " + juce::String(sustainPct, 0) + "%";
        }

        if (handle == DragHandle::release)
        {
            const auto sec = release.get();
            const auto ms = sec * 1000.0f;
            return "RELEASE " + (sec < 1.0f ? juce::String(ms, 0) + " ms" : juce::String(sec, 2) + " s");
        }

        return {};
    }

    void setParameterFromActualValue(juce::AudioParameterFloat& parameter, float value)
    {
        const auto range = parameter.getNormalisableRange();
        const auto clamped = juce::jlimit(range.start, range.end, value);
        parameter.setValueNotifyingHost(parameter.convertTo0to1(clamped));
    }

    void applyDragPosition(juce::Point<float> mousePos, const Geometry& geom)
    {
        if (dragHandle == DragHandle::none)
        {
            return;
        }

        const auto x = mousePos.getX();
        const auto y = mousePos.getY();

        if (dragHandle == DragHandle::attack)
        {
            const auto maxX = juce::jmin(geom.left + geom.attackRangeWidth,
                                         geom.releasePoint.getX() - geom.minDecayGap - geom.minSustainWidth);
            const auto clampedX = juce::jlimit(geom.left, maxX, x);
            const auto norm = clamp01((clampedX - geom.left) / juce::jmax(1.0f, geom.attackRangeWidth));
            const auto range = attack.getNormalisableRange();
            setParameterFromActualValue(attack, visualNormToTime(norm, range.start, range.end));
        }
        else if (dragHandle == DragHandle::decaySustain)
        {
            const auto xMin = geom.attackPoint.getX() + geom.minDecayGap;
            const auto xMax = geom.releasePoint.getX() - geom.minSustainWidth;
            const auto clampedX = juce::jlimit(xMin, xMax, x);
            const auto available = juce::jmax(1.0f, xMax - xMin);
            const auto decayNorm = clamp01((clampedX - xMin) / available);

            const auto yClamped = juce::jlimit(geom.top, geom.bottom, y);
            const auto sustainValue = juce::jmap(yClamped, geom.bottom, geom.top, 0.0f, 1.0f);

            const auto decayRange = decay.getNormalisableRange();
            setParameterFromActualValue(decay, visualNormToTime(decayNorm, decayRange.start, decayRange.end));
            setParameterFromActualValue(sustain, sustainValue);
        }
        else if (dragHandle == DragHandle::release)
        {
            const auto minX = geom.decaySustainPoint.getX() + geom.minSustainWidth;
            const auto clampedX = juce::jlimit(minX, geom.right, x);
            const auto norm = clamp01((geom.right - clampedX) / juce::jmax(1.0f, geom.releaseRangeWidth));
            const auto range = release.getNormalisableRange();
            setParameterFromActualValue(release, visualNormToTime(norm, range.start, range.end));
        }

        refreshFromParameters();
    }

    juce::AudioParameterFloat& attack;
    juce::AudioParameterFloat& decay;
    juce::AudioParameterFloat& sustain;
    juce::AudioParameterFloat& release;
    juce::Colour accent;
    DragHandle hoverHandle { DragHandle::none };
    DragHandle dragHandle { DragHandle::none };
    float lastAttack { -1.0f };
    float lastDecay { -1.0f };
    float lastSustain { -1.0f };
    float lastRelease { -1.0f };
};

void SynthProjectAudioProcessorEditor::KnobLookAndFeel::drawRotarySlider(juce::Graphics& g,
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
    const auto accent = slider.isColourSpecified(juce::Slider::rotarySliderFillColourId)
                            ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                            : juce::Colour::fromRGB(234, 166, 76);

    const auto psychedelicEnabled = static_cast<bool>(slider.getProperties()["psychedelicFx"]);
    const auto psychedelicGrayscale = static_cast<bool>(slider.getProperties()["psychedelicBypassGray"]);
    const auto psychedelicAmount = juce::jlimit(0.0f, 1.0f, sliderPos);
    const auto accentGrayValue = juce::jlimit(0.0f, 1.0f, accent.getPerceivedBrightness());
    const auto accentForHighlight = psychedelicGrayscale
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
            const auto hue = std::fmod(segNorm + t * 0.12f, 1.0f);
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
            g.setColour(psychedelicGrayscale
                            ? juce::Colour::fromFloatRGBA(grayValue, grayValue, grayValue, glowAlpha)
                            : juce::Colour::fromHSV(hue, 0.90f, 1.0f, glowAlpha));
            g.strokePath(arc,
                         juce::PathStrokeType(5.4f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

            const auto borderAlpha = juce::jlimit(0.0f, 0.95f, 0.25f + glow * 0.62f);
            g.setColour(psychedelicGrayscale
                            ? juce::Colour::fromFloatRGBA(grayValue, grayValue, grayValue, borderAlpha)
                            : juce::Colour::fromHSV(hue, 0.98f, 1.0f, borderAlpha));
            g.strokePath(arc,
                         juce::PathStrokeType(3.0f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }
    }

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

    juce::Path pointer;
    pointer.addRoundedRectangle(-2.1f, -radius * 0.56f, 4.2f, radius * 0.36f, 1.7f);
    g.setColour(juce::Colour::fromRGB(246, 246, 246));
    g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(center.x, center.y));

    g.setColour(juce::Colour::fromRGB(210, 210, 210));
    g.fillEllipse(center.x - 3.1f, center.y - 3.1f, 6.2f, 6.2f);
}

void SynthProjectAudioProcessorEditor::KnobLabel::paint(juce::Graphics& g)
{
    if (getText().isEmpty())
    {
        return;
    }

    auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 54));
    g.fillRoundedRectangle(area, 7.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 96));
    g.drawRoundedRectangle(area, 7.0f, 1.0f);

    g.setColour(findColour(juce::Label::textColourId));
    g.setFont(getFont());
    g.drawText(getText(),
               area.reduced(8.0f, 0.0f).toNearestInt(),
               juce::Justification::centred,
               true);
}

juce::String SynthProjectAudioProcessorEditor::noteNameForMidi(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (midiNote < 0 || midiNote > 127)
    {
        return "-";
    }

    const auto octave = (midiNote / 12) - 1;
    return juce::String(names[midiNote % 12]) + juce::String(octave);
}

juce::String SynthProjectAudioProcessorEditor::fxModuleIdFromSection(int sectionId)
{
    return moduleIdFromSectionId(sectionId);
}

int SynthProjectAudioProcessorEditor::fxSectionFromModuleId(const juce::String& moduleId)
{
    return sectionIdFromModuleId(moduleId);
}

void SynthProjectAudioProcessorEditor::configureKnob(KnobBinding& binding,
                                                      const juce::String& labelText,
                                                      juce::AudioParameterFloat& parameter)
{
    binding.parameter = &parameter;

    auto& knob = *binding.slider;
    auto& label = *binding.label;

    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& range = parameter.getNormalisableRange();
    knob.setRange(range.start, range.end);
    knob.setLookAndFeel(&knobLookAndFeel);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(225, 225, 225));
    label.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setFont(juce::FontOptions(13.0f));
    label.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(knob);
    addAndMakeVisible(label);
}

void SynthProjectAudioProcessorEditor::configureEffectKnob(juce::Slider& slider,
                                                           KnobLabel& label,
                                                           const juce::String& labelText,
                                                           juce::AudioParameterFloat& parameter)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& range = parameter.getNormalisableRange();
    slider.setRange(range.start, range.end);

    slider.setLookAndFeel(&knobLookAndFeel);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    label.setFont(juce::FontOptions(11.5f));
    label.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

void SynthProjectAudioProcessorEditor::attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

void SynthProjectAudioProcessorEditor::attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox)
{
    comboBoxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(parameter, comboBox, nullptr));
}

void SynthProjectAudioProcessorEditor::attachButton(juce::RangedAudioParameter& parameter, juce::Button& button)
{
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(parameter, button, nullptr));
}

void SynthProjectAudioProcessorEditor::setupDebugPanel()
{
    // The debug console is intentionally detached from the main editor surface
    // so frequent diagnostic refreshes do not clutter or stall normal UI work.
    debugToggleButton.setButtonText("DEBUG");
    debugToggleButton.onClick = [this]() { toggleDebugWindow(); };
    addAndMakeVisible(debugToggleButton);

    const auto setupLabel = [](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 236, 236));
        label.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        label.setInterceptsMouseClicks(false, false);
    };

    const auto setupEditor = [](juce::TextEditor& editor)
    {
        editor.setMultiLine(true);
        editor.setReturnKeyStartsNewLine(true);
        editor.setReadOnly(true);
        editor.setScrollbarsShown(true);
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGBA(8, 8, 8, 230));
        editor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(210, 236, 210));
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGBA(170, 170, 170, 90));
        editor.setFont(juce::FontOptions(11.0f));
    };

    const auto setupButton = [](juce::TextButton& button, const juce::String& text)
    {
        button.setButtonText(text);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(46, 46, 46, 230));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 232, 232));
    };

    setupLabel(debugPanelTitle, "P(X3 DEBUG CONSOLE");
    setupButton(debugPanelCloseButton, "CLOSE");
    setupButton(debugClearLogButton, "CLEAR LOG");
    setupButton(debugCopyLogButton, "COPY LOG");
    setupButton(debugSerializeButton, "REFRESH XML + SERIALIZED STATE");
    setupButton(debugRoundTripButton, "TEST STATE ROUND TRIP");
    setupButton(debugForceSerializeTestButton, "FORCE STATE SERIALIZATION TEST");
    setupButton(debugRestoreLastSerializedButton, "RESTORE LAST SERIALIZED STATE");
    setupButton(debugSnapshotButton, "SNAPSHOT CURRENT STATE");
    setupButton(debugCompareSnapshotButton, "COMPARE WITH SNAPSHOT");
    setupButton(debugResetOrderButton, "RESET ORDER");
    setupButton(debugOrderAButton, "DELAY -> REVERB -> HARMONIC DRIVE");
    setupButton(debugOrderBButton, "REVERB -> HARMONIC DRIVE -> DELAY");
    setupButton(debugOrderCButton, "HARMONIC DRIVE -> DELAY -> REVERB");
    setupButton(debugInvalidOrderButton, "TEST INVALID MODULE ORDER");
    setupButton(debugRandomizeParamsButton, "RANDOMIZE PARAMETERS");
    setupButton(debugResetParamsButton, "RESET PARAMETERS");
    setupButton(debugWriteTestValuesButton, "WRITE TEST VALUES");
    setupButton(debugDumpPresetButton, "DUMP PRESET");

    setupLabel(debugInstanceLabel, "A. PLUGIN INSTANCE INFO");
    setupLabel(debugModuleOrderLabel, "B. MODULE ORDER STATE");
    setupLabel(debugValueTreeLabel, "C. VALUETREE STATE");
    setupLabel(debugBackendControlLabel, "D. BACKEND PARAMETER CONTROLS");
    setupLabel(debugParameterLabel, "E. PARAMETER STATE");
    setupLabel(debugSerializedLabel, "F. SERIALIZATION EVENTS");
    setupLabel(debugLfoLabel, "G. LFO DEBUG");
    setupLabel(debugEnvelopeLabel, "H. AMP ENVELOPE DEBUG");
    setupLabel(debugPresetToolsLabel, "I. PRESET / STATE TOOLS");
    setupLabel(debugSnapshotLabel, "J. STATE TESTING");
    setupLabel(debugEventLogLabel, "K. EVENT LOG");

    setupEditor(debugInstanceText);
    setupEditor(debugModuleOrderText);
    setupEditor(debugValueTreeText);
    setupEditor(debugSerializedText);
    setupEditor(debugParameterInspectorText);
    setupEditor(debugEventLogText);
    setupEditor(debugSnapshotText);
    setupEditor(debugLfoText);
    setupEditor(debugEnvelopeText);

    debugDumpPresetNameLabel.setText("Preset Name", juce::dontSendNotification);
    debugDumpPresetNameLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 236, 236));
    debugDumpPresetNameLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    debugDumpPresetNameLabel.setJustificationType(juce::Justification::centredLeft);

    debugDumpPresetNameEditor.setMultiLine(false);
    debugDumpPresetNameEditor.setReturnKeyStartsNewLine(false);
    debugDumpPresetNameEditor.setReadOnly(false);
    debugDumpPresetNameEditor.setTextToShowWhenEmpty("Optional: suggested file/preset name", juce::Colour::fromRGBA(220, 220, 220, 130));
    debugDumpPresetNameEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGBA(14, 14, 14, 220));
    debugDumpPresetNameEditor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(232, 232, 232));
    debugDumpPresetNameEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 100));
    debugDumpPresetNameEditor.setFont(juce::FontOptions(11.0f));

    debugLfoAssignLabel.setText("LFO Assignment", juce::dontSendNotification);
    debugLfoAssignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 236, 236));
    debugLfoAssignLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    debugLfoAssignLabel.setJustificationType(juce::Justification::centredLeft);

    debugLfoAssignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 220));
    debugLfoAssignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    debugLfoAssignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 110));

    const auto& lfoAssignments = audioProcessor.getLfoAssignmentDisplayNames();
    for (int i = 0; i < lfoAssignments.size(); ++i)
    {
        debugLfoAssignBox.addItem(lfoAssignments[i], i + 1);
    }
    debugLfoAssignBox.onChange = [this]()
    {
        if (debugLfoAssignSuppressCallbacks)
        {
            return;
        }

        const auto selected = juce::jmax(0, debugLfoAssignBox.getSelectedId() - 1);
        audioProcessor.setLfoAssignmentIndex(selected);
        refreshDebugLfoState();
    };

    debugParamViewport.setViewedComponent(&debugParamContent, false);
    debugParamViewport.setScrollBarsShown(true, false);

    const auto addToPanel = [this](juce::Component& c)
    {
        debugPanel.addAndMakeVisible(c);
    };

    addToPanel(debugPanelTitle);
    addToPanel(debugPanelCloseButton);
    addToPanel(debugClearLogButton);
    addToPanel(debugCopyLogButton);
    addToPanel(debugSerializeButton);
    addToPanel(debugRoundTripButton);
    addToPanel(debugForceSerializeTestButton);
    addToPanel(debugRestoreLastSerializedButton);
    addToPanel(debugSnapshotButton);
    addToPanel(debugCompareSnapshotButton);
    addToPanel(debugResetOrderButton);
    addToPanel(debugOrderAButton);
    addToPanel(debugOrderBButton);
    addToPanel(debugOrderCButton);
    addToPanel(debugInvalidOrderButton);
    addToPanel(debugRandomizeParamsButton);
    addToPanel(debugResetParamsButton);
    addToPanel(debugWriteTestValuesButton);
    addToPanel(debugInstanceLabel);
    addToPanel(debugModuleOrderLabel);
    addToPanel(debugValueTreeLabel);
    addToPanel(debugSerializedLabel);
    addToPanel(debugParameterLabel);
    addToPanel(debugBackendControlLabel);
    addToPanel(debugLfoLabel);
    addToPanel(debugEnvelopeLabel);
    addToPanel(debugPresetToolsLabel);
    addToPanel(debugDumpPresetNameLabel);
    addToPanel(debugSnapshotLabel);
    addToPanel(debugEventLogLabel);
    addToPanel(debugInstanceText);
    addToPanel(debugModuleOrderText);
    addToPanel(debugValueTreeText);
    addToPanel(debugSerializedText);
    addToPanel(debugParameterInspectorText);
    addToPanel(debugEventLogText);
    addToPanel(debugSnapshotText);
    addToPanel(debugLfoText);
    addToPanel(debugEnvelopeText);
    addToPanel(debugDumpPresetNameEditor);
    addToPanel(debugLfoAssignLabel);
    addToPanel(debugLfoAssignBox);
    addToPanel(debugDumpPresetButton);
    addToPanel(debugParamViewport);

    debugValueTreeText.setText("Manual refresh disabled for live mode. Click 'REFRESH XML + SERIALIZED STATE'.", juce::dontSendNotification);
    debugSerializedText.setText("Manual refresh disabled for live mode. Click 'REFRESH XML + SERIALIZED STATE'.", juce::dontSendNotification);

    debugPanelCloseButton.onClick = [this]()
    {
        closeDebugWindow();
    };

    debugClearLogButton.onClick = [this]()
    {
        audioProcessor.debugClearEventLog();
        refreshDebugEventLog();
    };

    debugCopyLogButton.onClick = [this]()
    {
        juce::SystemClipboard::copyTextToClipboard(audioProcessor.debugGetEventLogText());
    };

    debugSerializeButton.onClick = [this]()
    {
        juce::MemoryBlock block;
        audioProcessor.getStateInformation(block);
        refreshDebugValueTree();
        refreshDebugSerializedState();
    };

    debugRoundTripButton.onClick = [this]()
    {
        juce::String report;
        audioProcessor.debugRoundTripCurrentState(report);
        debugSnapshotText.setText(report, juce::dontSendNotification);
        refreshDebugPanel(true);
    };

    debugForceSerializeTestButton.onClick = [this]() { debugForceSerializationTest(); };
    debugDumpPresetButton.onClick = [this]() { debugDumpPresetToFile(); };
    debugRestoreLastSerializedButton.onClick = [this]()
    {
        juce::String report;
        audioProcessor.debugRestoreLastSerializedState(report);
        debugSnapshotText.setText(report, juce::dontSendNotification);
        fxSectionOrder = audioProcessor.getFxProcessingOrder();
        resized();
        refreshDebugPanel(true);
    };

    debugSnapshotButton.onClick = [this]() { debugCaptureSnapshot("SNAPSHOT_BUTTON"); };
    debugCompareSnapshotButton.onClick = [this]() { debugCompareWithSnapshot(); };

    debugResetOrderButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDrive, kFxSectionDelay, kFxSectionReverb } }, "DEBUG_RESET_ORDER");
    };
    debugOrderAButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDelay, kFxSectionReverb, kFxSectionDrive } }, "DEBUG_ORDER_DELAY_REVERB_DRIVE");
    };
    debugOrderBButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionReverb, kFxSectionDrive, kFxSectionDelay } }, "DEBUG_ORDER_REVERB_DRIVE_DELAY");
    };
    debugOrderCButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDrive, kFxSectionDelay, kFxSectionReverb } }, "DEBUG_ORDER_DRIVE_DELAY_REVERB");
    };
    debugInvalidOrderButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDelay, kFxSectionDelay, kFxSectionReverb } }, "DEBUG_INVALID_ORDER_TEST");
    };

    debugRandomizeParamsButton.onClick = [this]() { debugRandomizeParameters(); };
    debugResetParamsButton.onClick = [this]() { debugResetParameters(); };
    debugWriteTestValuesButton.onClick = [this]() { debugWriteDeterministicTestValues(); };

    const auto& params = audioProcessor.getParameters();
    for (auto* parameter : params)
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged == nullptr)
        {
            continue;
        }

        auto control = std::make_unique<DebugParamControl>();
        control->parameter = ranged;
        control->label.setText(ranged->getName(128) + " [" + ranged->getParameterID() + "]", juce::dontSendNotification);
        control->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
        control->label.setFont(juce::FontOptions(11.0f));

        control->slider.setRange(0.0, 1.0, 0.0001);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(ranged->getValue(), juce::dontSendNotification);
        control->slider.onDragStart = [ranged]() { ranged->beginChangeGesture(); };
        control->slider.onDragEnd = [ranged]() { ranged->endChangeGesture(); };
        control->slider.onValueChange = [ptr = control.get()]
        {
            if (ptr->suppressCallbacks || ptr->parameter == nullptr)
            {
                return;
            }

            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;
            ptr->parameter->setValueNotifyingHost(requested);
            const auto actualNorm = ptr->parameter->getValue();
            const auto actualValue = ptr->parameter->convertFrom0to1(actualNorm);
            ptr->readback.setText("Requested: " + juce::String(requested, 5)
                                  + " | ActualNorm: " + juce::String(actualNorm, 5)
                                  + " | Actual: " + juce::String(actualValue, 5),
                                  juce::dontSendNotification);
        };

        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugParamContent.addAndMakeVisible(control->label);
        debugParamContent.addAndMakeVisible(control->slider);
        debugParamContent.addAndMakeVisible(control->readback);
        debugParamControls.push_back(std::move(control));
    }

    debugParamControlsInitialized = true;
}

void SynthProjectAudioProcessorEditor::openDebugWindow()
{
    if (debugWindow == nullptr)
    {
        auto window = std::make_unique<DebugPanelWindow>();
        window->setUsingNativeTitleBar(true);
        window->setResizable(true, true);
        window->setResizeLimits(920, 620, 2800, 2000);
        window->setAlwaysOnTop(true);
        window->onCloseRequested = [this]() { closeDebugWindow(); };
        window->setContentNonOwned(&debugPanel, false);
        debugWindow = std::move(window);
    }

    if (debugWindowBounds.getWidth() < 300 || debugWindowBounds.getHeight() < 200)
    {
        debugWindowBounds = { 100, 80, 1240, 780 };
    }

    debugWindow->setBounds(debugWindowBounds);
    debugWindow->setVisible(true);
    debugWindow->toFront(true);

    debugPanelVisible = true;
    debugRefreshTickCounter = 0;
    debugLastPanelLayoutBounds = {};
    layoutDebugPanel(debugPanel.getLocalBounds());
    debugLastPanelLayoutBounds = debugPanel.getLocalBounds();
    refreshDebugPanel(false);
}

void SynthProjectAudioProcessorEditor::closeDebugWindow()
{
    if (debugWindow != nullptr)
    {
        debugWindowBounds = debugWindow->getBounds();
        debugWindow->setVisible(false);
        debugWindow->setContentNonOwned(nullptr, false);
        debugWindow.reset();
    }

    debugPanelVisible = false;
    debugRefreshTickCounter = 0;
    debugLastPanelLayoutBounds = {};
    repaint();
}

void SynthProjectAudioProcessorEditor::toggleDebugWindow()
{
    if (debugPanelVisible)
    {
        closeDebugWindow();
        return;
    }

    openDebugWindow();
}

void SynthProjectAudioProcessorEditor::layoutDebugPanel(const juce::Rectangle<int>& bounds)
{
    const auto previousViewPos = debugParamViewport.getViewPosition();

    debugPanel.setBounds(bounds);
    auto area = debugPanel.getLocalBounds().reduced(10);

    auto top = area.removeFromTop(30);
    debugPanelTitle.setBounds(top.removeFromLeft(320));
    debugPanelCloseButton.setBounds(top.removeFromRight(90));

    auto actionRow1 = area.removeFromTop(28);
    debugSerializeButton.setBounds(actionRow1.removeFromLeft(200));
    debugRoundTripButton.setBounds(actionRow1.removeFromLeft(180));
    debugForceSerializeTestButton.setBounds(actionRow1.removeFromLeft(230));
    debugRestoreLastSerializedButton.setBounds(actionRow1.removeFromLeft(230));
    debugSnapshotButton.setBounds(actionRow1.removeFromLeft(170));
    debugCompareSnapshotButton.setBounds(actionRow1.removeFromLeft(190));

    area.removeFromTop(4);
    auto actionRow2 = area.removeFromTop(28);
    debugResetOrderButton.setBounds(actionRow2.removeFromLeft(140));
    debugOrderAButton.setBounds(actionRow2.removeFromLeft(220));
    debugOrderBButton.setBounds(actionRow2.removeFromLeft(220));
    debugOrderCButton.setBounds(actionRow2.removeFromLeft(220));
    debugInvalidOrderButton.setBounds(actionRow2.removeFromLeft(190));
    debugRandomizeParamsButton.setBounds(actionRow2.removeFromLeft(180));
    debugResetParamsButton.setBounds(actionRow2.removeFromLeft(160));
    debugWriteTestValuesButton.setBounds(actionRow2.removeFromLeft(170));

    area.removeFromTop(4);
    auto actionRow3 = area.removeFromTop(28);
    debugClearLogButton.setBounds(actionRow3.removeFromLeft(130));
    debugCopyLogButton.setBounds(actionRow3.removeFromLeft(130));

    area.removeFromTop(6);
    auto left = area.removeFromLeft(area.getWidth() / 2);
    auto right = area;

    auto section = [&left](juce::Label& label, juce::Component& component, int height)
    {
        label.setBounds(left.removeFromTop(18));
        component.setBounds(left.removeFromTop(height));
        left.removeFromTop(4);
    };

    section(debugInstanceLabel, debugInstanceText, 78);
    section(debugModuleOrderLabel, debugModuleOrderText, 120);
    section(debugValueTreeLabel, debugValueTreeText, 80);
    // Keep backend controls in the largest slot so the long parameter list has
    // practical scrolling room during deep debugging sessions.
    section(debugBackendControlLabel, debugParamViewport, juce::jmax(120, left.getHeight() - 24));

    auto sectionRight = [&right](juce::Label& label, juce::Component& component, int height)
    {
        label.setBounds(right.removeFromTop(18));
        component.setBounds(right.removeFromTop(height));
        right.removeFromTop(4);
    };

    sectionRight(debugParameterLabel, debugParameterInspectorText, 120);
    sectionRight(debugSerializedLabel, debugSerializedText, 140);

    debugLfoLabel.setBounds(right.removeFromTop(18));
    debugLfoText.setBounds(right.removeFromTop(66));
    right.removeFromTop(4);
    auto lfoAssignRow = right.removeFromTop(24);
    debugLfoAssignLabel.setBounds(lfoAssignRow.removeFromLeft(130));
    debugLfoAssignBox.setBounds(lfoAssignRow.reduced(1, 0));
    right.removeFromTop(4);

    debugEnvelopeLabel.setBounds(right.removeFromTop(18));
    debugEnvelopeText.setBounds(right.removeFromTop(68));
    right.removeFromTop(4);

    debugPresetToolsLabel.setBounds(right.removeFromTop(18));
    auto presetNameRow = right.removeFromTop(24);
    debugDumpPresetNameLabel.setBounds(presetNameRow.removeFromLeft(96));
    debugDumpPresetNameEditor.setBounds(presetNameRow.reduced(1, 0));
    right.removeFromTop(4);
    debugDumpPresetButton.setBounds(right.removeFromTop(24));
    right.removeFromTop(4);

    sectionRight(debugSnapshotLabel, debugSnapshotText, 80);
    sectionRight(debugEventLogLabel, debugEventLogText, juce::jmax(120, right.getHeight() - 24));

    auto content = debugParamViewport.getLocalBounds().reduced(4);
    
    content.removeFromRight(12);
    int y = 0;
    for (auto& control : debugParamControls)
    {
        control->label.setBounds(0, y, content.getWidth(), 16);
        y += 16;
        control->slider.setBounds(0, y, content.getWidth(), 22);
        y += 22;
        control->readback.setBounds(0, y, content.getWidth(), 14);
        y += 18;
    }
    debugParamContent.setBounds(0, 0, content.getWidth(), juce::jmax(content.getHeight(), y));
    debugParamViewport.setViewPosition(previousViewPos);
}

void SynthProjectAudioProcessorEditor::refreshDebugPanel(bool includeHeavySections)
{
    if (!debugPanelVisible)
    {
        return;
    }

    const auto currentBounds = debugPanel.getLocalBounds();
    if (currentBounds != debugLastPanelLayoutBounds)
    {
        // Re-layout only when the detached debug window actually changes size.
        // This avoids fighting the viewport scroll position during timer updates.
        layoutDebugPanel(currentBounds);
        debugLastPanelLayoutBounds = currentBounds;
    }

    refreshDebugModuleState();
    if (includeHeavySections)
    {
        // XML/serialized panes are intentionally on-demand to avoid high
        // allocation churn during normal timer-driven updates.
        refreshDebugValueTree();
        refreshDebugSerializedState();
    }
    refreshDebugParameterInspector();
    refreshDebugParameterControls();
    refreshDebugLfoState();
    refreshDebugEnvelopeState();
    refreshDebugEventLog();
    debugInstanceText.setText(buildInstanceInfoText(), juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugModuleState()
{
    const auto processor = describeProcessorOrder();
    const auto ui = describeUiOrder();
    const auto tree = describeStateTreeOrder();
    const auto mismatch = (processor != ui) || (processor != tree);

    const auto processorOrder = audioProcessor.getFxProcessingOrder();
    const auto treeOrder = readModuleOrderFromStateTree(audioProcessor.createParameterStateTree());
    std::array<juce::String, 3> uiOrderNames {
        { fxModuleIdFromSection(fxSectionOrder[0]),
          fxModuleIdFromSection(fxSectionOrder[1]),
          fxModuleIdFromSection(fxSectionOrder[2]) }
    };

    const auto findPos = [](const juce::String& id, const std::array<juce::String, 3>& orderNames)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (id.equalsIgnoreCase(orderNames[static_cast<std::size_t>(i)]))
            {
                return i;
            }
        }
        return -1;
    };

    std::array<juce::String, 3> processorOrderNames {
        { fxModuleIdFromSection(processorOrder[0]),
          fxModuleIdFromSection(processorOrder[1]),
          fxModuleIdFromSection(processorOrder[2]) }
    };

    juce::String text;
    text << "Processor: " << processor << "\n";
    text << "UI:        " << ui << "\n";
    text << "ValueTree: " << tree << "\n";
    text << "\nRaw Processor Array:\n";
    text << "[0] " << processorOrderNames[0] << "\n";
    text << "[1] " << processorOrderNames[1] << "\n";
    text << "[2] " << processorOrderNames[2] << "\n";
    text << "Raw UI Array:\n";
    text << "[0] " << uiOrderNames[0] << "\n";
    text << "[1] " << uiOrderNames[1] << "\n";
    text << "[2] " << uiOrderNames[2] << "\n";
    text << "Raw ValueTree MODULE_ORDER:\n";
    text << "module[0] = " << treeOrder[0] << "\n";
    text << "module[1] = " << treeOrder[1] << "\n";
    text << "module[2] = " << treeOrder[2] << "\n";
    text << "Generation: " << juce::String(static_cast<int64_t>(audioProcessor.debugGetModuleOrderGeneration())) << "\n";
    text << "Hash: " << juce::String(static_cast<int64_t>(audioProcessor.debugGetModuleOrderHash())) << "\n";

    static const std::array<juce::String, 3> modules { juce::String("harmonicDrive"), juce::String("delay"), juce::String("reverb") };
    text << "\nPer-Module Positions\n";
    for (const auto& moduleId : modules)
    {
        const auto p = findPos(moduleId, processorOrderNames);
        const auto u = findPos(moduleId, uiOrderNames);
        const auto v = findPos(moduleId, treeOrder);
        text << moduleId << " -> Processor:" << juce::String(p)
             << " UI:" << juce::String(u)
             << " ValueTree:" << juce::String(v)
             << ((p == u && p == v) ? "" : "   !!! MISMATCH !!!")
             << "\n";
    }

    if (mismatch)
    {
        text << "\n!!! STATE MISMATCH !!!\n";
    }
    debugModuleOrderText.setText(text, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugValueTree()
{
    const auto state = audioProcessor.createParameterStateTree();
    if (auto xml = state.createXml())
    {
        debugValueTreeText.setText(xml->toString(), juce::dontSendNotification);
    }
}

void SynthProjectAudioProcessorEditor::refreshDebugSerializedState()
{
    juce::String text;
    const auto size = audioProcessor.debugGetLastSerializedStateSize();
    text << "Size: " << juce::String(size) << " bytes\n";

    const auto block = audioProcessor.debugGetLastSerializedStateCopy();
    text << "Hex (first 256 bytes):\n";
    const auto maxBytes = std::min<size_t>(256u, block.getSize());
    const auto* data = static_cast<const uint8_t*>(block.getData());
    for (size_t i = 0; i < maxBytes; ++i)
    {
        if ((i % 16u) == 0u)
        {
            text << "\n";
        }
        text << juce::String::toHexString(static_cast<int>(data[i])).paddedLeft('0', 2) << " ";
    }
    text << "\n\nXML:\n" << audioProcessor.debugGetLastSerializedStateXml();
    debugSerializedText.setText(text, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugParameterInspector()
{
    debugParameterInspectorText.setText(buildParameterInspectorText(), juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugParameterControls()
{
    if (!debugParamControlsInitialized)
    {
        return;
    }

    for (auto& control : debugParamControls)
    {
        if (control->parameter == nullptr)
        {
            continue;
        }

        const auto norm = control->parameter->getValue();
        const auto actual = control->parameter->convertFrom0to1(norm);
        control->suppressCallbacks = true;
        control->slider.setValue(norm, juce::dontSendNotification);
        control->suppressCallbacks = false;
        control->readback.setText("Requested: " + juce::String(control->lastRequested, 5)
                                  + " | ActualNorm: " + juce::String(norm, 5)
                                  + " | Actual: " + juce::String(actual, 5),
                                  juce::dontSendNotification);
    }
}

void SynthProjectAudioProcessorEditor::refreshDebugEventLog()
{
    debugEventLogText.setText(audioProcessor.debugGetEventLogText(), juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugLfoState()
{
    const auto assignmentIndex = audioProcessor.getLfoAssignmentIndex();
    debugLfoAssignSuppressCallbacks = true;
    debugLfoAssignBox.setSelectedId(assignmentIndex + 1, juce::dontSendNotification);
    debugLfoAssignSuppressCallbacks = false;

    const auto frequencyHz = audioProcessor.getLfoFrequencyParam().get();
    const auto phase = audioProcessor.debugGetLfoPhase();
    const auto lfoValue = audioProcessor.debugGetLfoCurrentValue();
    const auto baseNorm = audioProcessor.debugGetLfoBaseNormalized();
    const auto effectiveNorm = audioProcessor.debugGetLfoEffectiveNormalized();
    const auto assignmentId = audioProcessor.getLfoAssignmentParameterId();
    const auto assignmentName = audioProcessor.debugGetLfoAssignmentName();

    juce::String text;
    text << "Frequency: " << juce::String(frequencyHz, 4) << " Hz\n"
         << "Assignment: " << assignmentName << "\n"
         << "Assignment ID: " << assignmentId << "\n"
         << "Phase: " << juce::String(phase, 5) << "\n"
         << "LFO Value: " << juce::String(lfoValue, 5) << "\n"
         << "Base (norm): " << juce::String(baseNorm, 5) << "\n"
         << "Effective (norm): " << juce::String(effectiveNorm, 5) << "\n"
         << "Delta: " << juce::String(effectiveNorm - baseNorm, 5);

    debugLfoText.setText(text, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshDebugEnvelopeState()
{
    const auto attackSec = audioProcessor.getAttackParam().get();
    const auto decaySec = audioProcessor.getDecayParam().get();
    const auto sustainNorm = audioProcessor.getSustainParam().get();
    const auto releaseSec = audioProcessor.getReleaseParam().get();

    const auto attackText = attackSec < 1.0f ? juce::String(attackSec * 1000.0f, 1) + " ms" : juce::String(attackSec, 4) + " s";
    const auto decayText = decaySec < 1.0f ? juce::String(decaySec * 1000.0f, 1) + " ms" : juce::String(decaySec, 4) + " s";
    const auto releaseText = releaseSec < 1.0f ? juce::String(releaseSec * 1000.0f, 1) + " ms" : juce::String(releaseSec, 4) + " s";

    juce::String text;
    text << "Attack:  " << attackText << "\n"
         << "Decay:   " << decayText << "\n"
         << "Sustain: " << juce::String(sustainNorm, 4) << " (" << juce::String(sustainNorm * 100.0f, 1) << "%)\n"
         << "Release: " << releaseText << "\n\n"
         << "Graph:  /\\__====\\";

    debugEnvelopeText.setText(text, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::debugCaptureSnapshot(const juce::String& reason)
{
    debugLastSnapshot = {};
    debugLastSnapshot.timestamp = audioProcessor.debugNowTimestamp();
    debugLastSnapshot.processorOrder = audioProcessor.getFxProcessingOrder();
    debugLastSnapshot.uiOrder = fxSectionOrder;
    debugLastSnapshot.generation = audioProcessor.debugGetModuleOrderGeneration();
    debugLastSnapshot.hash = audioProcessor.debugGetModuleOrderHash();
    debugLastSnapshot.stateXml = debugValueTreeText.getText();

    juce::MemoryBlock serialized;
    audioProcessor.getStateInformation(serialized);
    debugLastSnapshot.serializedBytes = static_cast<int>(serialized.getSize());
    debugLastSnapshot.serializedXml = audioProcessor.debugGetLastSerializedStateXml();

    for (auto* parameter : audioProcessor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            debugLastSnapshot.normalizedValues[ranged->getParameterID()] = ranged->getValue();
        }
    }

    debugHasSnapshot = true;

    juce::String text;
    text << "SNAPSHOT CAPTURED\n";
    text << "Reason: " << reason << "\n";
    text << "Timestamp: " << debugLastSnapshot.timestamp << "\n";
    text << "Processor Order: " << audioProcessor.debugDescribeOrder(debugLastSnapshot.processorOrder) << "\n";
    text << "UI Order: " << audioProcessor.debugDescribeOrder(debugLastSnapshot.uiOrder) << "\n";
    text << "Generation: " << juce::String(static_cast<int64_t>(debugLastSnapshot.generation)) << "\n";
    text << "Hash: " << juce::String(static_cast<int64_t>(debugLastSnapshot.hash)) << "\n";
    text << "Serialized Bytes: " << juce::String(debugLastSnapshot.serializedBytes) << "\n";
    debugSnapshotText.setText(text, juce::dontSendNotification);

    audioProcessor.debugLogEvent("DEBUG_PANEL", "SNAPSHOT_CAPTURED", "reason=" + reason + " order=" + describeProcessorOrder());
}

void SynthProjectAudioProcessorEditor::debugCompareWithSnapshot()
{
    if (!debugHasSnapshot)
    {
        debugSnapshotText.setText("No snapshot captured yet.", juce::dontSendNotification);
        return;
    }

    juce::String report;
    const auto currentOrder = audioProcessor.getFxProcessingOrder();
    const auto currentOrderText = audioProcessor.debugDescribeOrder(currentOrder);
    const auto snapOrderText = audioProcessor.debugDescribeOrder(debugLastSnapshot.processorOrder);

    report << "COMPARE WITH SNAPSHOT\n";
    report << "Snapshot Time: " << debugLastSnapshot.timestamp << "\n";
    report << "Module Order: " << (currentOrderText == snapOrderText ? "unchanged" : "changed") << "\n";
    report << "Snapshot: " << snapOrderText << "\n";
    report << "Current:  " << currentOrderText << "\n";

    juce::StringArray changed;
    for (auto* parameter : audioProcessor.getParameters())
    {
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
        {
            const auto id = ranged->getParameterID();
            const auto it = debugLastSnapshot.normalizedValues.find(id);
            if (it != debugLastSnapshot.normalizedValues.end())
            {
                if (std::abs(it->second - ranged->getValue()) > 0.0001f)
                {
                    changed.add(id);
                }
            }
        }
    }

    report << "Parameters Changed: " << (changed.isEmpty() ? juce::String("none") : changed.joinIntoString(", ")) << "\n";
    debugSnapshotText.setText(report, juce::dontSendNotification);
    audioProcessor.debugLogEvent("DEBUG_PANEL", "SNAPSHOT_COMPARE", report.replaceCharacters("\n", " | "));
}

void SynthProjectAudioProcessorEditor::debugForceSerializationTest()
{
    juce::MemoryBlock block;
    audioProcessor.getStateInformation(block);
    const auto xmlText = audioProcessor.debugGetLastSerializedStateXml();

    const auto hasModuleOrder = xmlText.containsIgnoreCase("MODULE_ORDER")
                                && xmlText.containsIgnoreCase("harmonicDrive")
                                && xmlText.containsIgnoreCase("delay")
                                && xmlText.containsIgnoreCase("reverb");

    const auto hasDelayTime = xmlText.containsIgnoreCase("delayTime");
    const auto hasReverbAmount = xmlText.containsIgnoreCase("reverbAmount");
    const auto hasRobAmount = xmlText.containsIgnoreCase("robAmount");
    const auto pass = hasModuleOrder && hasDelayTime && hasReverbAmount && hasRobAmount;

    juce::String report;
    report << "FORCE STATE SERIALIZATION TEST\n";
    report << "Size: " << juce::String(static_cast<int>(block.getSize())) << "\n";
    report << "MODULE_ORDER present: " << (hasModuleOrder ? "YES" : "NO") << "\n";
    report << "delayTime present: " << (hasDelayTime ? "YES" : "NO") << "\n";
    report << "reverbAmount present: " << (hasReverbAmount ? "YES" : "NO") << "\n";
    report << "robAmount present: " << (hasRobAmount ? "YES" : "NO") << "\n";
    report << "RESULT: " << (pass ? "PASS" : "FAIL");
    debugSnapshotText.setText(report, juce::dontSendNotification);
    audioProcessor.debugLogEvent("DEBUG_PANEL", "FORCE_SERIALIZATION_TEST", report.replaceCharacters("\n", " | "));
}

void SynthProjectAudioProcessorEditor::debugDumpPresetToFile()
{
    auto suggestedName = debugDumpPresetNameEditor.getText().trim();
    if (suggestedName.isEmpty())
    {
        suggestedName = hasCurrentPreset ? currentPreset.metadata.name.trim() : juce::String();
    }
    if (suggestedName.isEmpty())
    {
        suggestedName = "PX3_Preset_" + juce::Time::getCurrentTime().formatted("%Y-%m-%d");
    }

    auto defaultFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                           .getChildFile(suggestedName)
                           .withFileExtension(PresetManager::presetFileExtension);

    audioProcessor.debugLogEvent("DEBUG_PANEL", "DUMP_PRESET_STARTED", "suggestedName=" + suggestedName);

    auto chooser = std::make_shared<juce::FileChooser>("Dump P(X3) preset",
                                                        defaultFile,
                                                        "*" + juce::String(PresetManager::presetFileExtension));

    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::canSelectFiles
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             auto destination = fc.getResult();
                             if (destination == juce::File())
                             {
                                 audioProcessor.debugLogEvent("DEBUG_PANEL", "DUMP_PRESET_CANCELLED");
                                 return;
                             }

                             if (!destination.hasFileExtension(PresetManager::presetFileExtension))
                             {
                                 destination = destination.withFileExtension(PresetManager::presetFileExtension);
                             }

                             PresetManager::PresetMetadata metadata;
                             metadata.name = debugDumpPresetNameEditor.getText().trim();
                             if (metadata.name.isEmpty())
                             {
                                 metadata.name = destination.getFileNameWithoutExtension();
                             }
                             metadata.category = hasCurrentPreset ? currentPreset.metadata.category : juce::String("USER_DUMP");
                             metadata.author = hasCurrentPreset ? currentPreset.metadata.author : juce::String();
                             metadata.description = hasCurrentPreset ? currentPreset.metadata.description : juce::String();

                             const auto overwrite = destination.existsAsFile();
                             juce::String error;
                             int serializedBytes = 0;
                             if (!presetManager.dumpCurrentStateToPresetFile(destination,
                                                                            metadata,
                                                                            overwrite,
                                                                            true,
                                                                            error,
                                                                            &serializedBytes))
                             {
                                 audioProcessor.debugLogEvent("DEBUG_PANEL", "DUMP_PRESET_FAILED", error);
                                 debugSnapshotText.setText("PRESET DUMP FAILED\n\nCould not write file.\n\n"
                                                               "Error:\n"
                                                               + error,
                                                           juce::dontSendNotification);
                                 refreshDebugEventLog();
                                 return;
                             }

                             audioProcessor.debugLogEvent("DEBUG_PANEL",
                                                         "DUMP_PRESET_SERIALIZED",
                                                         "size=" + juce::String(serializedBytes) + " bytes");
                             audioProcessor.debugLogEvent("DEBUG_PANEL", "DUMP_PRESET_VALIDATED");
                             audioProcessor.debugLogEvent("DEBUG_PANEL",
                                                         "DUMP_PRESET_WRITTEN",
                                                         "path=" + destination.getFullPathName());

                             const auto sizeLabel = juce::File::descriptionOfSizeInBytes(destination.getSize());
                             debugSnapshotText.setText("PRESET DUMP SUCCESSFUL\n\n"
                                                           "File:\n"
                                                           + destination.getFullPathName()
                                                           + "\n\nSize:\n"
                                                           + sizeLabel,
                                                       juce::dontSendNotification);
                             refreshDebugEventLog();
                         });
}

void SynthProjectAudioProcessorEditor::debugWriteDeterministicTestValues()
{
    for (auto& control : debugParamControls)
    {
        if (control->parameter == nullptr)
        {
            continue;
        }

        const auto id = control->parameter->getParameterID();
        float normalized = control->parameter->getDefaultValue();
        if (id == "delayTime") normalized = 0.37f;
        else if (id == "delayFeedback") normalized = 0.61f;
        else if (id == "reverbAmount") normalized = 0.42f;
        else if (id == "robAmount") normalized = 0.77f;
        else if (id == "reverbDecay") normalized = 0.53f;

        control->parameter->beginChangeGesture();
        control->parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalized));
        control->parameter->endChangeGesture();
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "WRITE_TEST_VALUES", "deterministic values applied");
    refreshDebugPanel(false);
}

void SynthProjectAudioProcessorEditor::debugRandomizeParameters()
{
    std::mt19937 rng(static_cast<uint32_t>(juce::Time::getMillisecondCounter()));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (auto& control : debugParamControls)
    {
        if (control->parameter == nullptr)
        {
            continue;
        }

        const auto v = dist(rng);
        control->parameter->beginChangeGesture();
        control->parameter->setValueNotifyingHost(v);
        control->parameter->endChangeGesture();
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "RANDOMIZE_PARAMETERS", "all ranged parameters randomized");
    refreshDebugPanel(false);
}

void SynthProjectAudioProcessorEditor::debugResetParameters()
{
    for (auto& control : debugParamControls)
    {
        if (control->parameter == nullptr)
        {
            continue;
        }

        control->parameter->beginChangeGesture();
        control->parameter->setValueNotifyingHost(control->parameter->getDefaultValue());
        control->parameter->endChangeGesture();
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "RESET_PARAMETERS", "all ranged parameters reset to defaults");
    refreshDebugPanel(false);
}

void SynthProjectAudioProcessorEditor::debugApplyModuleOrder(const std::array<int, 3>& order,
                                                             const juce::String& reason,
                                                             int fromIndex,
                                                             int toIndex)
{
    fxSectionOrder = order;
    for (int stage = 0; stage < 3; ++stage)
    {
        const auto slot = indexForFxSection(stage);
        if (slot >= 0)
        {
            fxSectionTargetAreas[static_cast<std::size_t>(stage)] = fxSectionSlots[static_cast<std::size_t>(slot)].toFloat();
            fxSectionCurrentAreas[static_cast<std::size_t>(stage)] = fxSectionTargetAreas[static_cast<std::size_t>(stage)];
        }
    }
    layoutFxSectionsFromCurrentAreas();
    commitFxOrderToProcessor("DEBUG_PANEL", reason, fromIndex, toIndex);
    refreshDebugPanel(false);
    repaint();
}

std::array<juce::String, 3> SynthProjectAudioProcessorEditor::readModuleOrderFromStateTree(const juce::ValueTree& state) const
{
    std::array<juce::String, 3> result { { "harmonicDrive", "delay", "reverb" } };
    const auto moduleOrder = state.getChildWithName("MODULE_ORDER");
    if (!moduleOrder.isValid())
    {
        return result;
    }

    int write = 0;
    for (int i = 0; i < moduleOrder.getNumChildren() && write < 3; ++i)
    {
        const auto module = moduleOrder.getChild(i);
        if (!module.isValid() || !module.hasProperty("id"))
        {
            continue;
        }
        result[static_cast<std::size_t>(write++)] = module.getProperty("id").toString();
    }
    return result;
}

juce::String SynthProjectAudioProcessorEditor::describeUiOrder() const
{
    juce::StringArray items;
    for (const auto sectionId : fxSectionOrder)
    {
        items.add(fxModuleIdFromSection(sectionId));
    }
    return items.joinIntoString(",");
}

juce::String SynthProjectAudioProcessorEditor::describeProcessorOrder() const
{
    return audioProcessor.debugDescribeOrder(audioProcessor.getFxProcessingOrder());
}

juce::String SynthProjectAudioProcessorEditor::describeStateTreeOrder() const
{
    const auto state = audioProcessor.createParameterStateTree();
    const auto order = readModuleOrderFromStateTree(state);
    return order[0] + "," + order[1] + "," + order[2];
}

juce::String SynthProjectAudioProcessorEditor::buildParameterInspectorText() const
{
    juce::String text;
    const auto& params = audioProcessor.getParameters();
    int paramIndex = 0;
    for (auto* parameter : params)
    {
        const auto currentIndex = paramIndex++;
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter);
        if (ranged == nullptr)
        {
            continue;
        }

        juce::String type = "Ranged";
        const auto normalRange = ranged->getNormalisableRange();
        juce::String rangeText = juce::String(normalRange.start, 6)
                                + " - " + juce::String(normalRange.end, 6)
                                + " step=" + juce::String(normalRange.interval, 6);
        juce::String defaultText = juce::String(ranged->convertFrom0to1(ranged->getDefaultValue()), 6);

        if (const auto* pFloat = dynamic_cast<juce::AudioParameterFloat*>(ranged))
        {
            juce::ignoreUnused(pFloat);
            type = "AudioParameterFloat";
        }
        else if (const auto* pInt = dynamic_cast<juce::AudioParameterInt*>(ranged))
        {
            juce::ignoreUnused(pInt);
            type = "AudioParameterInt";
        }
        else if (const auto* pBool = dynamic_cast<juce::AudioParameterBool*>(ranged))
        {
            juce::ignoreUnused(pBool);
            type = "AudioParameterBool";
            rangeText = "0 - 1";
        }
        else if (const auto* pChoice = dynamic_cast<juce::AudioParameterChoice*>(ranged))
        {
            type = "AudioParameterChoice";
            rangeText = "0 - " + juce::String(juce::jmax(0, pChoice->choices.size() - 1));
        }

        const auto norm = ranged->getValue();
        const auto real = ranged->convertFrom0to1(norm);

        text << "Index: " << juce::String(currentIndex)
             << " | ID: " << ranged->getParameterID()
             << " | Name: " << ranged->getName(128)
             << "\n"
             << "Type: " << type
             << " | Value: " << juce::String(real, 6)
             << " | Normalized: " << juce::String(norm, 6)
             << " | Default: " << defaultText
             << "\n"
             << "Range: " << rangeText
             << " | Automatable: " << (parameter->isAutomatable() ? "YES" : "NO")
             << " | Discrete: " << (parameter->isDiscrete() ? "YES" : "NO")
             << "\n\n";
    }
    return text;
}

juce::String SynthProjectAudioProcessorEditor::buildInstanceInfoText() const
{
    juce::String wrapperName = "Unknown";
#if JucePlugin_Build_AU
    wrapperName = "AU";
#elif JucePlugin_Build_AUv3
    wrapperName = "AUv3";
#elif JucePlugin_Build_VST3
    wrapperName = "VST3";
#elif JucePlugin_Build_VST
    wrapperName = "VST";
#elif JucePlugin_Build_AAX
    wrapperName = "AAX";
#elif JucePlugin_Build_Standalone
    wrapperName = "Standalone";
#endif

    juce::String text;
    text << "P(X3)\n";
    text << "Version: " << px3::version::string() << "\n";
    text << "ID: " << audioProcessor.debugGetInstanceId() << "\n";
    text << "Processor Created: " << audioProcessor.debugGetProcessorCreatedTime() << "\n";
    text << "Editor Created: " << debugEditorCreatedTime << "\n";
    text << "Processor Ptr: 0x" << juce::String::toHexString(reinterpret_cast<juce::int64>(&audioProcessor)) << "\n";
    text << "Editor Ptr: 0x" << juce::String::toHexString(reinterpret_cast<juce::int64>(this)) << "\n";
    text << "JUCE: " << juce::String(JUCE_MAJOR_VERSION) << "." << juce::String(JUCE_MINOR_VERSION) << "." << juce::String(JUCE_BUILDNUMBER) << "\n";
    text << "Format: " << wrapperName << "\n";
    text << "Sample Rate: " << juce::String(audioProcessor.getSampleRate(), 2) << "\n";
    text << "Block Size: " << juce::String(audioProcessor.getBlockSize()) << "\n";
    text << "Num Params: " << juce::String(audioProcessor.getParameters().size()) << "\n";
    text << "Num Modules: 3\n";
    return text;
}

SynthProjectAudioProcessorEditor::SynthProjectAudioProcessorEditor(SynthProjectAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    tooltipWindow(this, 450),
    sourceEnginePanel(p),
    presetManager(p)
{
    backgroundImage = juce::ImageFileFormat::loadFrom(BinaryData::pp_png, BinaryData::pp_pngSize);
    logoFrame = juce::ImageFileFormat::loadFrom(BinaryData::px3_gif, BinaryData::px3_gifSize);

    if (logoFrame.isValid())
    {
        const auto w = logoFrame.getWidth();
        const auto h = logoFrame.getHeight();
        logoGlitchMaskR = juce::Image(juce::Image::ARGB, w, h, true);
        logoGlitchMaskG = juce::Image(juce::Image::ARGB, w, h, true);
        logoGlitchMaskB = juce::Image(juce::Image::ARGB, w, h, true);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto px = logoFrame.getPixelAt(x, y);
                const auto brightness = px.getPerceivedBrightness();
                const auto whiteWeight = juce::jlimit(0.0f, 1.0f, (brightness - 0.72f) / 0.28f) * px.getFloatAlpha();
                if (whiteWeight <= 0.001f)
                {
                    continue;
                }

                const auto a = static_cast<juce::uint8>(std::round(whiteWeight * 255.0f));
                logoGlitchMaskR.setPixelAt(x, y, juce::Colour::fromRGBA(255, 76, 76, a));
                logoGlitchMaskG.setPixelAt(x, y, juce::Colour::fromRGBA(92, 255, 120, a));
                logoGlitchMaskB.setPixelAt(x, y, juce::Colour::fromRGBA(86, 140, 255, a));
            }
        }
    }

    setResizable(true, true);
    setResizeLimits(980, 600, 1900, 980);

    addAndMakeVisible(sourceEnginePanel);
    addAndMakeVisible(performanceControls);
    addAndMakeVisible(pianoKeyboard);

    performanceControls.onPitchBendChanged = [this](float normalized)
    {
        audioProcessor.setPitchBendNormalizedFromUI(normalized);
    };
    performanceControls.onPitchBendGestureEnded = [this]()
    {
        audioProcessor.setPitchBendNormalizedFromUI(0.0f);
    };
    performanceControls.onModWheelChanged = [this](float normalized)
    {
        audioProcessor.setModWheelNormalizedFromUI(normalized);
    };

    pianoKeyboard.onNoteOn = [this](int midiNote, float velocityNorm)
    {
        audioProcessor.queueVirtualKeyboardNoteOn(midiNote, velocityNorm);
    };
    pianoKeyboard.onNoteOff = [this](int midiNote)
    {
        audioProcessor.queueVirtualKeyboardNoteOff(midiNote);
    };

    knobBindings = {
        KnobBinding { &oscSineKnob, &oscSineLabel, nullptr },
        KnobBinding { &oscSawKnob, &oscSawLabel, nullptr },
        KnobBinding { &oscSquareKnob, &oscSquareLabel, nullptr },
        KnobBinding { &cutoffKnob, &cutoffLabel, nullptr },
        KnobBinding { &resonanceKnob, &resonanceLabel, nullptr },
        KnobBinding { &attackKnob, &attackLabel, nullptr },
        KnobBinding { &decayKnob, &decayLabel, nullptr },
        KnobBinding { &sustainKnob, &sustainLabel, nullptr },
        KnobBinding { &releaseKnob, &releaseLabel, nullptr },
        KnobBinding { &lfoFrequencyKnob, &lfoFrequencyLabel, nullptr },
        KnobBinding { &gainKnob, &gainLabel, nullptr }
    };

    configureKnob(knobBindings[0], "PARAM A", audioProcessor.getOscMacroAParam());
    configureKnob(knobBindings[1], "PARAM B", audioProcessor.getOscMacroBParam());
    configureKnob(knobBindings[2], "PARAM C", audioProcessor.getOscMacroCParam());
    configureKnob(knobBindings[3], "Cutoff", audioProcessor.getFilterCutoffParam());
    configureKnob(knobBindings[4], "Reso", audioProcessor.getFilterResonanceParam());
    configureKnob(knobBindings[5], "Attack", audioProcessor.getAttackParam());
    configureKnob(knobBindings[6], "Decay", audioProcessor.getDecayParam());
    configureKnob(knobBindings[7], "Sustain", audioProcessor.getSustainParam());
    configureKnob(knobBindings[8], "Release", audioProcessor.getReleaseParam());
    configureKnob(knobBindings[9], "Freq", audioProcessor.getLfoFrequencyParam());
    configureKnob(knobBindings[10], "Gain", audioProcessor.getMasterGainParam());

    // ADSR graph replaces visible envelope knobs; parameter attachments remain unchanged.
    attackKnob.setVisible(false);
    decayKnob.setVisible(false);
    sustainKnob.setVisible(false);
    releaseKnob.setVisible(false);
    attackLabel.setVisible(false);
    decayLabel.setVisible(false);
    sustainLabel.setVisible(false);
    releaseLabel.setVisible(false);

    envelopeGraph = std::make_unique<EnvelopeGraphComponent>(audioProcessor.getAttackParam(),
                                                             audioProcessor.getDecayParam(),
                                                             audioProcessor.getSustainParam(),
                                                             audioProcessor.getReleaseParam(),
                                                             kGroupAccents[2]);
    addAndMakeVisible(*envelopeGraph);

    lfoFrequencyValueLabel.setJustificationType(juce::Justification::centred);
    lfoFrequencyValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    lfoFrequencyValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lfoFrequencyValueLabel.setFont(juce::FontOptions(11.0f));
    lfoFrequencyValueLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(lfoFrequencyValueLabel);

    lfoFrequencyKnob.onValueChange = [this]()
    {
        refreshLfoFrequencyLabel();
    };
    refreshLfoFrequencyLabel();

    lfoAssignLabel.setText("Assign", juce::dontSendNotification);
    lfoAssignLabel.setJustificationType(juce::Justification::centred);
    lfoAssignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoAssignLabel.setFont(juce::FontOptions(11.5f));
    lfoAssignLabel.setInterceptsMouseClicks(false, false);
    lfoAssignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    lfoAssignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoAssignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

    const auto& lfoAssignments = audioProcessor.getLfoAssignmentDisplayNames();
    for (int i = 0; i < lfoAssignments.size(); ++i)
    {
        lfoAssignBox.addItem(lfoAssignments[i], i + 1);
    }

    lfoAssignBox.onChange = [this]()
    {
        const auto selected = juce::jmax(0, lfoAssignBox.getSelectedId() - 1);
        audioProcessor.setLfoAssignmentIndex(selected);
    };

    addAndMakeVisible(lfoAssignLabel);
    addAndMakeVisible(lfoAssignBox);

    // OSC macro labels can become long in some modes; use a slightly smaller font.
    oscSineLabel.setFont(juce::FontOptions(11.0f));
    oscSawLabel.setFont(juce::FontOptions(11.0f));
    oscSquareLabel.setFont(juce::FontOptions(11.0f));

    configureEffectKnob(robWarmthKnob, robWarmthLabel, "WARMTH", audioProcessor.getRobAmountParam());
    configureEffectKnob(isaacTextureKnob, isaacTextureLabel, "", audioProcessor.getIsaacAmountParam());
    configureEffectKnob(delayTimeKnob, delayTimeLabel, "TIME", audioProcessor.getDelayTimeParam());
    configureEffectKnob(delayFeedbackKnob, delayFeedbackLabel, "FEEDBACK", audioProcessor.getDelayFeedbackParam());
    configureEffectKnob(reverbKnob, reverbLabel, "INTENSITY", audioProcessor.getReverbAmountParam());

    // Keep the large delay knob unlabeled per UX request.
    isaacTextureLabel.setVisible(false);

    robWarmthKnob.getProperties().set("psychedelicFx", true);
    isaacTextureKnob.getProperties().set("psychedelicFx", true);
    reverbKnob.getProperties().set("psychedelicFx", true);

    auto& filterTypeParam = audioProcessor.getFilterTypeParam();
    const auto filterChoiceCount = filterTypeParam.choices.size();
    for (int i = 0; i < filterChoiceCount; ++i)
    {
        filterTypeBox.addItem(filterTypeParam.choices[i], i + 1);
    }
    filterTypeBox.setSelectedItemIndex(filterTypeParam.getIndex(), juce::dontSendNotification);
    filterTypeBox.onChange = [this]()
    {
        repaint();
    };
    filterTypeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    filterTypeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    filterTypeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    addAndMakeVisible(filterTypeBox);

    auto& oscModeParam = audioProcessor.getOscillatorModeParam();
    const auto oscModeCount = oscModeParam.choices.size();
    for (int i = 0; i < oscModeCount; ++i)
    {
        oscModeBox.addItem(oscModeParam.choices[i], i + 1);
    }
    oscModeBox.setSelectedItemIndex(oscModeParam.getIndex(), juce::dontSendNotification);
    oscModeBox.onChange = [this]()
    {
        refreshOscillatorModeUI();
    };
    oscModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    oscModeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    oscModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    oscModeLabel.setText("MODE", juce::dontSendNotification);
    oscModeLabel.setJustificationType(juce::Justification::centred);
    oscModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    oscModeLabel.setFont(juce::FontOptions(11.5f));
    oscModeLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(oscModeBox);
    addAndMakeVisible(oscModeLabel);

    auto& oscVowelParam = audioProcessor.getOscVowelParam();
    const auto oscVowelCount = oscVowelParam.choices.size();
    for (int i = 0; i < oscVowelCount; ++i)
    {
        oscVowelBox.addItem(oscVowelParam.choices[i], i + 1);
    }
    oscVowelBox.setSelectedItemIndex(oscVowelParam.getIndex(), juce::dontSendNotification);
    oscVowelBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    oscVowelBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    oscVowelBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    oscVowelLabel.setText("VOWEL", juce::dontSendNotification);
    oscVowelLabel.setJustificationType(juce::Justification::centred);
    oscVowelLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    oscVowelLabel.setFont(juce::FontOptions(11.5f));
    oscVowelLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(oscVowelBox);
    addAndMakeVisible(oscVowelLabel);

    auto& imagePositionParam = audioProcessor.getImagePositionParam();
    oscSineKnob.onValueChange = [this, &imagePositionParam]()
    {
        if (!oscSineKnob.isMouseButtonDown())
        {
            return;
        }

        const auto macroA = static_cast<float>(oscSineKnob.getValue());

        // In WAVETABLE mode, the POSITION knob should visibly drive Image Engine position.
        if (audioProcessor.getOscillatorModeParam().getIndex() == 8)
        {
            imagePositionParam.setValueNotifyingHost(imagePositionParam.convertTo0to1(macroA));
        }
    };

    auto& delayAlgoParam = audioProcessor.getDelayAlgorithmParam();
    const auto delayAlgoChoiceCount = delayAlgoParam.choices.size();
    for (int i = 0; i < delayAlgoChoiceCount; ++i)
    {
        delayAlgoBox.addItem(delayAlgoParam.choices[i], i + 1);
    }
    delayAlgoBox.setSelectedItemIndex(delayAlgoParam.getIndex(), juce::dontSendNotification);
    delayAlgoBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    delayAlgoBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    delayAlgoBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    delayAlgoLabel.setText("ALGO", juce::dontSendNotification);
    delayAlgoLabel.setJustificationType(juce::Justification::centred);
    delayAlgoLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    delayAlgoLabel.setFont(juce::FontOptions(11.5f));
    delayAlgoLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(delayAlgoBox);
    addAndMakeVisible(delayAlgoLabel);

    auto& granularSyncParam = audioProcessor.getGranularSyncDivisionParam();
    const auto choiceCount = granularSyncParam.choices.size();
    for (int i = 0; i < choiceCount; ++i)
    {
        granularSyncBox.addItem(granularSyncParam.choices[i], i + 1);
    }
    granularSyncBox.setSelectedItemIndex(granularSyncParam.getIndex(), juce::dontSendNotification);
    granularSyncBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    granularSyncBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    granularSyncBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    granularSyncLabel.setText("SYNC", juce::dontSendNotification);
    granularSyncLabel.setJustificationType(juce::Justification::centred);
    granularSyncLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    granularSyncLabel.setFont(juce::FontOptions(11.5f));
    granularSyncLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(granularSyncBox);
    addAndMakeVisible(granularSyncLabel);

    auto& granularModeParam = audioProcessor.getGranularModeParam();
    const auto modeChoiceCount = granularModeParam.choices.size();
    for (int i = 0; i < modeChoiceCount; ++i)
    {
        granularModeBox.addItem(granularModeParam.choices[i], i + 1);
    }
    granularModeBox.setSelectedItemIndex(granularModeParam.getIndex(), juce::dontSendNotification);
    granularModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    granularModeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    granularModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    granularModeLabel.setText("MODE", juce::dontSendNotification);
    granularModeLabel.setJustificationType(juce::Justification::centred);
    granularModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    granularModeLabel.setFont(juce::FontOptions(11.5f));
    granularModeLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(granularModeBox);
    addAndMakeVisible(granularModeLabel);

    auto& reverbAlgoParam = audioProcessor.getReverbAlgorithmParam();
    const auto reverbChoiceCount = reverbAlgoParam.choices.size();
    for (int i = 0; i < reverbChoiceCount; ++i)
    {
        reverbTypeBox.addItem(reverbAlgoParam.choices[i], i + 1);
    }
    reverbTypeBox.setSelectedItemIndex(reverbAlgoParam.getIndex(), juce::dontSendNotification);
    reverbTypeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    reverbTypeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    reverbTypeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    reverbTypeLabel.setText("ALGO", juce::dontSendNotification);
    reverbTypeLabel.setJustificationType(juce::Justification::centred);
    reverbTypeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    reverbTypeLabel.setFont(juce::FontOptions(11.5f));
    reverbTypeLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(reverbTypeBox);
    addAndMakeVisible(reverbTypeLabel);

    const auto configureBypassButton = [](juce::ToggleButton& button)
    {
        button.setButtonText("");
        button.setTooltip("Bypass");
        button.setClickingTogglesState(true);
        button.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
        button.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));
    };

    configureBypassButton(robBypassButton);
    configureBypassButton(delayBypassButton);
    configureBypassButton(reverbBypassButton);

    addAndMakeVisible(robBypassButton);
    addAndMakeVisible(delayBypassButton);
    addAndMakeVisible(reverbBypassButton);

    for (auto& binding : knobBindings)
    {
        attachSlider(*binding.parameter, *binding.slider);
    }

    attachSlider(audioProcessor.getRobAmountParam(), robWarmthKnob);
    attachSlider(audioProcessor.getIsaacAmountParam(), isaacTextureKnob);
    attachSlider(audioProcessor.getDelayTimeParam(), delayTimeKnob);
    attachSlider(audioProcessor.getDelayFeedbackParam(), delayFeedbackKnob);
    attachSlider(audioProcessor.getReverbAmountParam(), reverbKnob);

    attachComboBox(audioProcessor.getFilterTypeParam(), filterTypeBox);
    attachComboBox(audioProcessor.getOscillatorModeParam(), oscModeBox);
    attachComboBox(audioProcessor.getOscVowelParam(), oscVowelBox);
    attachComboBox(audioProcessor.getDelayAlgorithmParam(), delayAlgoBox);
    attachComboBox(audioProcessor.getGranularSyncDivisionParam(), granularSyncBox);
    attachComboBox(audioProcessor.getGranularModeParam(), granularModeBox);
    attachComboBox(audioProcessor.getReverbAlgorithmParam(), reverbTypeBox);

    attachButton(audioProcessor.getRobEnabledParam(), robBypassButton);
    attachButton(audioProcessor.getDelayEnabledParam(), delayBypassButton);
    attachButton(audioProcessor.getReverbEnabledParam(), reverbBypassButton);

    // MIDI status bar is temporarily disabled.
    // midiStatusLabel.setText("MIDI In: waiting for note...", juce::dontSendNotification);
    // midiStatusLabel.setJustificationType(juce::Justification::centred);
    // midiStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 172, 88));
    // midiStatusLabel.setFont(juce::FontOptions(14.0f));
    // midiStatusLabel.setInterceptsMouseClicks(false, false);
    // addAndMakeVisible(midiStatusLabel);

    const auto setupPresetButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(40, 40, 40, 210));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 232, 232));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(68, 124, 180, 220));
    };

    setupPresetButton(presetPrevButton);
    setupPresetButton(presetNameButton);
    setupPresetButton(presetNextButton);
    setupPresetButton(presetSaveButton);
    setupPresetButton(presetSaveAsButton);
    setupPresetButton(presetDeleteButton);
    setupPresetButton(presetImportButton);
    setupPresetButton(presetExportButton);

    presetPrevButton.setButtonText("<");
    presetNameButton.setButtonText("INIT");
    presetNextButton.setButtonText(">");
    presetSaveButton.setButtonText("SAVE");
    presetSaveAsButton.setButtonText("SAVE AS");
    presetDeleteButton.setButtonText("DEL");
    presetImportButton.setButtonText("IMPORT");
    presetExportButton.setButtonText("EXPORT");

    presetFavoriteButton.setButtonText("★");
    presetFavoriteButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(230, 230, 230));
    presetFavoriteButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(250, 210, 70));
    presetFavoriteButton.setTooltip("Add to Favorites");

    presetPrevButton.onClick = [this]()
    {
        if (presetFiltered.empty())
        {
            return;
        }

        if (!hasCurrentPreset)
        {
            applyPresetRecord(presetFiltered.front());
            return;
        }

        int currentIndex = 0;
        for (int i = 0; i < static_cast<int>(presetFiltered.size()); ++i)
        {
            if (presetFiltered[static_cast<std::size_t>(i)].file == currentPreset.file)
            {
                currentIndex = i;
                break;
            }
        }

        const auto next = (currentIndex - 1 + static_cast<int>(presetFiltered.size())) % static_cast<int>(presetFiltered.size());
        applyPresetRecord(presetFiltered[static_cast<std::size_t>(next)]);
    };

    presetNextButton.onClick = [this]()
    {
        if (presetFiltered.empty())
        {
            return;
        }

        if (!hasCurrentPreset)
        {
            applyPresetRecord(presetFiltered.front());
            return;
        }

        int currentIndex = 0;
        for (int i = 0; i < static_cast<int>(presetFiltered.size()); ++i)
        {
            if (presetFiltered[static_cast<std::size_t>(i)].file == currentPreset.file)
            {
                currentIndex = i;
                break;
            }
        }

        const auto next = (currentIndex + 1) % static_cast<int>(presetFiltered.size());
        applyPresetRecord(presetFiltered[static_cast<std::size_t>(next)]);
    };

    presetNameButton.onClick = [this]() { openPresetBrowser(); };
    presetSaveButton.onClick = [this]() { savePreset(false); };
    presetSaveAsButton.onClick = [this]() { savePreset(true); };
    presetDeleteButton.onClick = [this]() { deleteCurrentPreset(); };
    presetImportButton.onClick = [this]() { importPreset(); };
    presetExportButton.onClick = [this]() { exportCurrentPreset(); };
    presetFavoriteButton.onClick = [this]()
    {
        if (!hasCurrentPreset)
        {
            presetFavoriteButton.setToggleState(false, juce::dontSendNotification);
            return;
        }

        juce::String error;
        if (!presetManager.setFavorite(currentPreset, presetFavoriteButton.getToggleState(), error))
        {
            showPresetError("Favorite Failed", error);
            presetFavoriteButton.setToggleState(currentPreset.isFavorite, juce::dontSendNotification);
            return;
        }

        presetManager.refreshIndex();
        rebuildPresetFilteredList();
        if (const auto* found = presetManager.findByFile(currentPreset.file))
        {
            currentPreset = *found;
            refreshPresetNameDisplay();
        }
    };

    addAndMakeVisible(presetPrevButton);
    addAndMakeVisible(presetNameButton);
    addAndMakeVisible(presetNextButton);
    addAndMakeVisible(presetFavoriteButton);
    addAndMakeVisible(presetSaveButton);
    addAndMakeVisible(presetSaveAsButton);
    addAndMakeVisible(presetDeleteButton);
    addAndMakeVisible(presetImportButton);
    addAndMakeVisible(presetExportButton);

    presetBrowserPanel.setInterceptsMouseClicks(false, true);
    addAndMakeVisible(presetBrowserPanel);
    presetBrowserPanel.setVisible(false);

    presetBrowserTitle.setText("P(X3) PRESETS", juce::dontSendNotification);
    presetBrowserTitle.setJustificationType(juce::Justification::centredLeft);
    presetBrowserTitle.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 236, 236));
    presetBrowserTitle.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    presetBrowserTitle.setInterceptsMouseClicks(false, false);

    presetScopeBox.addItem("All", 1);
    presetScopeBox.addItem("Factory", 2);
    presetScopeBox.addItem("User", 3);
    presetScopeBox.addItem("Favorites", 4);
    presetScopeBox.setSelectedId(1, juce::dontSendNotification);

    presetCategoryBox.addItem("All", 1);
    presetCategoryBox.setSelectedId(1, juce::dontSendNotification);

    presetSearchEditor.setTextToShowWhenEmpty("Search name/category/author/description", juce::Colour::fromRGBA(220, 220, 220, 120));
    presetSearchEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGBA(24, 24, 24, 210));
    presetSearchEditor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(235, 235, 235));

    presetListBox.setModel(this);
    presetListBox.setRowHeight(24);
    presetListBox.setColour(juce::ListBox::backgroundColourId, juce::Colour::fromRGBA(20, 20, 20, 200));

    presetBrowserLoadButton.setButtonText("LOAD");
    presetBrowserCloseButton.setButtonText("CLOSE");
    setupPresetButton(presetBrowserLoadButton);
    setupPresetButton(presetBrowserCloseButton);

    presetBrowserDetails.setJustificationType(juce::Justification::topLeft);
    presetBrowserDetails.setColour(juce::Label::textColourId, juce::Colour::fromRGB(208, 208, 208));
    presetBrowserDetails.setFont(juce::FontOptions(12.0f));

    presetBrowserPanel.addAndMakeVisible(presetBrowserTitle);
    presetBrowserPanel.addAndMakeVisible(presetScopeBox);
    presetBrowserPanel.addAndMakeVisible(presetCategoryBox);
    presetBrowserPanel.addAndMakeVisible(presetSearchEditor);
    presetBrowserPanel.addAndMakeVisible(presetListBox);
    presetBrowserPanel.addAndMakeVisible(presetBrowserLoadButton);
    presetBrowserPanel.addAndMakeVisible(presetBrowserCloseButton);
    presetBrowserPanel.addAndMakeVisible(presetBrowserDetails);

    presetScopeBox.onChange = [this]() { rebuildPresetFilteredList(); };
    presetCategoryBox.onChange = [this]() { rebuildPresetFilteredList(); };
    presetSearchEditor.onTextChange = [this]() { rebuildPresetFilteredList(); };
    presetBrowserCloseButton.onClick = [this]() { closePresetBrowser(); };
    presetBrowserLoadButton.onClick = [this]()
    {
        const auto row = presetListBox.getSelectedRow();
        if (row >= 0 && row < static_cast<int>(presetFiltered.size()))
        {
            applyPresetRecord(presetFiltered[static_cast<std::size_t>(row)]);
            closePresetBrowser();
        }
    };

    // Seed visual slot layout with the processor order before the first setSize/resized pass.
    fxSectionOrder = audioProcessor.getFxProcessingOrder();
    setSize(1320, 700);

    juce::String presetInitError;
    if (!presetManager.initialise(presetInitError))
    {
        showPresetError("Preset Init Failed", presetInitError);
    }

    auto categories = presetManager.getAllCategories();
    int catId = 2;
    for (const auto& category : categories)
    {
        if (!category.equalsIgnoreCase("ALL"))
        {
            presetCategoryBox.addItem(category, catId++);
        }
    }

    rebuildPresetFilteredList();
    // Do not auto-load INIT here: the host may have just restored plugin state.
    // Auto-loading a preset in the editor constructor would overwrite that state.
    refreshPresetNameDisplay();

    refreshOscillatorModeUI();
    refreshGranularModeUI();
    refreshLfoAssignmentUI();
    refreshEnvelopeGraphUI();
    refreshFxBypassUI();
    debugEditorCreatedTime = audioProcessor.debugNowTimestamp();
#if PX3_DEBUG_PANEL
    setupDebugPanel();
#endif
    audioProcessor.debugNotifyEditorCreated(this);

    startTimerHz(30);
}

SynthProjectAudioProcessorEditor::~SynthProjectAudioProcessorEditor()
{
    closeDebugWindow();
    audioProcessor.debugNotifyEditorDestroyed(this);

    for (auto& binding : knobBindings)
    {
        binding.slider->setLookAndFeel(nullptr);
    }

    robWarmthKnob.setLookAndFeel(nullptr);
    isaacTextureKnob.setLookAndFeel(nullptr);
    delayTimeKnob.setLookAndFeel(nullptr);
    delayFeedbackKnob.setLookAndFeel(nullptr);
    reverbKnob.setLookAndFeel(nullptr);
}

void SynthProjectAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));

    if (backgroundImage.isValid())
    {
        g.drawImageWithin(backgroundImage,
                          0,
                          0,
                          getWidth(),
                          getHeight(),
                          juce::RectanglePlacement::fillDestination,
                          false);

        g.setColour(juce::Colour::fromRGBA(26, 26, 26, 150));
        g.fillAll();
    }

    g.setColour(juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(logoPanelArea.toFloat(), 12.0f);

    if (logoFrame.isValid())
    {
                const auto subtitleHeight = 18.0f;
                const auto subtitleGap = 6.0f;
                const auto logoSize = static_cast<float>(juce::jlimit(80, 120, logoPanelArea.getHeight() - 46));
                const auto contentHeight = logoSize + subtitleGap + subtitleHeight;
                const auto contentTop = static_cast<float>(logoPanelArea.getY())
                                                                + (static_cast<float>(logoPanelArea.getHeight()) - contentHeight) * 0.5f;

                const auto logoArea = juce::Rectangle<float>(static_cast<float>(logoPanelArea.getX()),
                                                                                                         contentTop,
                                                                                                         static_cast<float>(logoPanelArea.getWidth()),
                                                                                                         logoSize)
                                                                    .withSizeKeepingCentre(logoSize, logoSize);
        const auto vibration = juce::jlimit(0.0f, 1.0f, logoVibrationIntensity);
        const auto shakePx = vibration * 3.2f;
        const auto shakeX = std::sin(logoVibrationPhase * 5.7f) * shakePx;
        const auto shakeY = std::cos(logoVibrationPhase * 7.9f + 0.8f) * (shakePx * 0.85f);
        auto transform = juce::AffineTransform::scale(logoArea.getWidth() / static_cast<float>(logoFrame.getWidth()),
                                                      logoArea.getHeight() / static_cast<float>(logoFrame.getHeight()))
                             .translated(logoArea.getX(), logoArea.getY());
        transform = transform.translated(shakeX, shakeY);
        g.drawImageTransformed(logoFrame, transform);

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

                g.setColour(juce::Colour::fromRGB(232, 232, 232));
                g.setFont(juce::FontOptions(14.0f));
                const auto subtitleArea = juce::Rectangle<int>(logoPanelArea.getX() + 10,
                                                                                                             static_cast<int>(std::round(logoArea.getBottom() + subtitleGap)),
                                                                                                             logoPanelArea.getWidth() - 20,
                                                                                                             static_cast<int>(subtitleHeight));
                g.drawText("Synth v" + px3::version::string(), subtitleArea, juce::Justification::centred);
    }

        const auto robEnabled = audioProcessor.getRobEnabledParam().get();
        const auto delayEnabled = audioProcessor.getDelayEnabledParam().get();
        const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

        g.setColour(robEnabled ? juce::Colour::fromRGBA(104, 194, 255, 35)
                       : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(robSectionArea.toFloat(), 10.0f);
        g.setColour(robEnabled ? juce::Colour::fromRGBA(104, 194, 255, 180)
                       : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(robSectionArea.toFloat(), 10.0f, 1.0f);

        g.setColour(delayEnabled ? juce::Colour::fromRGBA(255, 198, 110, 35)
                     : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(isaacSectionArea.toFloat(), 10.0f);
        g.setColour(delayEnabled ? juce::Colour::fromRGBA(255, 198, 110, 180)
                     : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(isaacSectionArea.toFloat(), 10.0f, 1.0f);

        g.setColour(reverbEnabled ? juce::Colour::fromRGBA(128, 208, 255, 30)
                      : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(reverbSectionArea.toFloat(), 10.0f);
        g.setColour(reverbEnabled ? juce::Colour::fromRGBA(128, 208, 255, 150)
                      : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(reverbSectionArea.toFloat(), 10.0f, 1.0f);

        g.setColour(robEnabled ? juce::Colour::fromRGB(240, 245, 255)
                       : juce::Colour::fromRGB(170, 170, 170));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("HARMONIC DRIVE", robSectionArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);

        g.setColour(delayEnabled ? juce::Colour::fromRGB(250, 244, 224)
                     : juce::Colour::fromRGB(170, 170, 170));
        g.drawText("DELAY", isaacSectionArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);

        g.setColour(reverbEnabled ? juce::Colour::fromRGB(224, 245, 255)
                      : juce::Colour::fromRGB(170, 170, 170));
        g.drawText("REVERB", reverbSectionArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
        g.fillRoundedRectangle(topSpareSectionArea.toFloat(), 10.0f);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 65));
        g.drawRoundedRectangle(topSpareSectionArea.toFloat(), 10.0f, 1.0f);

    if (performanceControlsArea.getWidth() > 0 && pianoKeyboard.getBounds().getWidth() > 0)
    {
        auto performanceStrip = performanceControlsArea.getUnion(pianoKeyboard.getBounds()).toFloat();
        juce::ColourGradient stripGradient(juce::Colour::fromRGBA(56, 88, 118, 72),
                                           performanceStrip.getX(),
                                           performanceStrip.getY(),
                                           juce::Colour::fromRGBA(35, 38, 42, 96),
                                           performanceStrip.getRight(),
                                           performanceStrip.getBottom(),
                                           false);
        g.setGradientFill(stripGradient);
        g.fillRoundedRectangle(performanceStrip, 12.0f);

        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 50));
        g.drawRoundedRectangle(performanceStrip, 12.0f, 1.0f);

        const auto dividerX = static_cast<float>(performanceControlsArea.getRight() + 3);
        g.setColour(juce::Colour::fromRGBA(255, 255, 255, 38));
        g.drawLine(dividerX,
                   performanceStrip.getY() + 8.0f,
                   dividerX,
                   performanceStrip.getBottom() - 8.0f,
                   1.0f);
    }

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 26));
    g.drawLine(static_cast<float>(controlsArea.getX()),
               static_cast<float>(controlsArea.getY()),
               static_cast<float>(controlsArea.getRight()),
               static_cast<float>(controlsArea.getY()),
               1.0f);

    for (std::size_t i = 0; i < knobGroupAreas.size(); ++i)
    {
        const auto headerFontSize = (i == 0) ? 14.0f : 15.0f;
        g.setFont(juce::FontOptions(headerFontSize, juce::Font::bold));

        const auto panel = knobGroupAreas[i].reduced(2);
        const auto accent = kGroupAccents[i];
        const auto panelFloat = panel.toFloat();

        g.setColour(accent.withAlpha(0.16f));
        g.fillRoundedRectangle(panelFloat, 10.0f);

        g.setColour(accent.withAlpha(0.10f));
        g.fillRoundedRectangle(panel.withTrimmedBottom(panel.getHeight() / 2).toFloat(), 10.0f);

        g.setColour(accent.withAlpha(0.78f));
        g.drawRoundedRectangle(panelFloat, 10.0f, 1.0f);

        const auto labelArea = panel.withHeight(24);
        g.setColour(accent.brighter(0.35f));
        g.drawText(kGroupNames[i], labelArea, juce::Justification::centred);

        if (i == 0)
        {
            auto oscSplit = panel.reduced(8, 8);
            oscSplit.removeFromTop(26);
            auto oscVizRect = oscSplit.removeFromLeft(oscSplit.getWidth() / 2).reduced(4, 2);
            oscVizRect.removeFromBottom(28);

            if (oscVizRect.getWidth() > 40 && oscVizRect.getHeight() > 24)
            {
                const auto modeIndex = juce::jmax(0, oscModeBox.getSelectedItemIndex());
                const auto macroA = static_cast<float>(oscSineKnob.getValue());
                const auto macroB = static_cast<float>(oscSawKnob.getValue());
                const auto macroC = static_cast<float>(oscSquareKnob.getValue());

                auto viz = oscVizRect.toFloat();
                g.setColour(juce::Colour::fromRGBA(12, 16, 26, 170));
                g.fillRoundedRectangle(viz, 7.0f);
                g.setColour(juce::Colour::fromRGBA(145, 198, 255, 80));
                g.drawRoundedRectangle(viz, 7.0f, 1.0f);

                const auto left = viz.getX() + 6.0f;
                const auto right = viz.getRight() - 6.0f;
                const auto top = viz.getY() + 5.0f;
                const auto bottom = viz.getBottom() - 5.0f;
                const auto mid = (top + bottom) * 0.5f;
                const auto width = juce::jmax(1.0f, right - left);
                const auto height = juce::jmax(1.0f, bottom - top);

                g.setColour(juce::Colour::fromRGBA(255, 255, 255, 28));
                for (int gx = 1; gx < 6; ++gx)
                {
                    const auto x = left + width * (static_cast<float>(gx) / 6.0f);
                    g.drawLine(x, top, x, bottom, 0.7f);
                }
                g.drawLine(left, mid, right, mid, 0.9f);

                juce::Path wave;
                wave.startNewSubPath(left, mid);

                for (int s = 0; s <= 72; ++s)
                {
                    const auto t = static_cast<float>(s) / 72.0f;
                    const auto phase = t * juce::MathConstants<float>::twoPi + oscVizPhase;
                    float y = 0.0f;

                    switch (modeIndex)
                    {
                        case 0: // SINE
                            y = std::sin(phase);
                            break;
                        case 1: // SAW
                            y = 2.0f * t - 1.0f;
                            break;
                        case 2: // SQUARE
                            y = std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
                            break;
                        case 3: // TRIANGLE
                            y = 1.0f - 4.0f * std::abs(t - 0.5f);
                            break;
                        case 4: // NOISE
                        case 5: // PINK NOISE
                        {
                            const auto n = std::sin(phase * 13.0f + t * 31.0f) * 0.7f
                                           + std::sin(phase * 29.0f + t * 19.0f) * 0.3f;
                            y = modeIndex == 5 ? n * 0.55f : n;
                            break;
                        }
                        case 6: // SUPER SAW
                            y = (std::sin(phase)
                                 + std::sin(phase * (1.0f + 0.04f + macroA * 0.2f))
                                 + std::sin(phase * (1.0f - 0.05f - macroA * 0.18f)))
                                * 0.33f;
                            break;
                        case 7: // PWM
                        {
                            const auto widthNorm = juce::jlimit(0.1f, 0.9f, 0.1f + macroA * 0.8f);
                            y = t < widthNorm ? 1.0f : -1.0f;
                            break;
                        }
                        case 8: // WAVETABLE
                            y = std::sin(phase * (1.0f + macroA * 4.0f)) * 0.6f
                                + std::sin(phase * (3.0f + macroB * 5.0f)) * 0.35f;
                            break;
                        case 9:  // ADDITIVE
                        case 18: // ISAAC
                            y = std::sin(phase) * 0.62f + std::sin(phase * 2.0f) * 0.22f + std::sin(phase * 3.0f) * 0.16f;
                            break;
                        case 10: // FORMANT
                            y = std::sin(phase) * 0.45f + std::sin(phase * (2.0f + macroB * 2.0f)) * 0.33f + std::sin(phase * 4.0f) * 0.22f;
                            break;
                        case 11: // FM
                            y = std::sin(phase + std::sin(phase * (1.0f + macroA * 4.0f)) * (macroB * 3.0f));
                            break;
                        case 12: // HARD SYNC
                            y = std::sin(std::fmod(phase * (1.0f + macroA * 6.0f), juce::MathConstants<float>::twoPi));
                            break;
                        case 13: // KARPLUS
                        case 15: // DIGITAL
                        case 16: // PHYSICAL
                            y = std::sin(phase * (1.0f + macroA * 2.0f)) * (0.7f - t * 0.35f)
                                + std::sin(phase * (5.0f + macroB * 8.0f)) * 0.18f;
                            break;
                        case 14: // ORGAN
                            y = std::sin(phase) * 0.55f + std::sin(phase * 2.0f) * 0.30f + std::sin(phase * 4.0f) * 0.15f;
                            break;
                        case 17: // ROB
                            y = std::sin(phase * (1.0f + macroB * 1.8f)) * 0.6f + std::sin(phase * 7.0f + t * 9.0f) * 0.25f;
                            break;
                        case 19: // PX3
                            y = std::sin(phase * (1.0f + macroA * 2.3f)
                                         + std::sin(phase * (2.0f + macroB * 5.0f)) * (0.7f + macroC * 2.4f));
                            break;
                        default:
                            y = std::sin(phase);
                            break;
                    }

                    y = juce::jlimit(-1.0f, 1.0f, y);
                    const auto px = left + t * width;
                    const auto py = mid - y * (height * 0.40f);
                    if (s == 0)
                    {
                        wave.startNewSubPath(px, py);
                    }
                    else
                    {
                        wave.lineTo(px, py);
                    }
                }

                const auto glowAlpha = juce::jlimit(0.20f, 0.86f, 0.20f + 0.22f * (macroA + macroB + macroC));
                g.setColour(juce::Colour::fromRGBA(118, 190, 255, static_cast<juce::uint8>(std::round(glowAlpha * 120.0f))));
                g.strokePath(wave,
                             juce::PathStrokeType(3.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
                g.setColour(juce::Colour::fromRGB(170, 228, 255));
                g.strokePath(wave,
                             juce::PathStrokeType(1.35f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

                g.setColour(juce::Colour::fromRGBA(255, 255, 255, 120));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText("Mode Visual", oscVizRect.removeFromTop(14), juce::Justification::centredTop);
            }
        }

        if (i == 1)
        {
            // This mini-response graph is intentionally illustrative (not an
            // exact transfer-function plot). It helps developers and testers
            // confirm filter mode selection at a glance without opening debug.
            const auto idx = juce::jlimit(0, 6, filterTypeBox.getSelectedItemIndex());

            const auto cutoffBounds = cutoffKnob.getBounds().toFloat();
            const auto resonanceBounds = resonanceKnob.getBounds().toFloat();
            const auto dropdownBounds = filterTypeBox.getBounds().toFloat();

            const auto knobBottom = juce::jmax(cutoffBounds.getBottom(), resonanceBounds.getBottom());
            const auto knobCenterX = (cutoffBounds.getCentreX() + resonanceBounds.getCentreX()) * 0.5f;
            const auto graphWidth = juce::jmin(56.0f, panelFloat.getWidth() - 20.0f);
            const auto graphHeight = 22.0f;

            const auto minY = knobBottom + 6.0f;
            const auto maxY = dropdownBounds.getY() - graphHeight - 4.0f;
            const auto graphY = (maxY >= minY)
                                    ? juce::jlimit(minY, maxY, minY + (maxY - minY) * 0.4f)
                                    : minY;
            const auto liftedGraphY = juce::jmax(panelFloat.getY() + 30.0f, graphY - 15.0f);
            const auto graphX = juce::jlimit(panelFloat.getX() + 8.0f,
                                             panelFloat.getRight() - graphWidth - 8.0f,
                                             knobCenterX - graphWidth * 0.5f);
            auto graphRect = juce::Rectangle<float>(graphX, liftedGraphY, graphWidth, graphHeight);
            g.setColour(juce::Colour::fromRGBA(20, 20, 20, 140));
            g.fillRoundedRectangle(graphRect, 4.0f);
            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 70));
            g.drawRoundedRectangle(graphRect, 4.0f, 1.0f);

            const auto left = graphRect.getX() + 4.0f;
            const auto right = graphRect.getRight() - 4.0f;
            const auto top = graphRect.getY() + 4.0f;
            const auto bottom = graphRect.getBottom() - 4.0f;
            const auto midY = (top + bottom) * 0.5f;

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 36));
            g.drawLine(left, midY, right, midY, 1.0f);

            juce::Path response;
            response.startNewSubPath(left, bottom);

            const auto width = juce::jmax(1.0f, right - left);
            for (int s = 1; s <= 40; ++s)
            {
                const auto t = static_cast<float>(s) / 40.0f;
                const auto x = left + t * width;
                float yNorm = 0.5f;

                switch (idx)
                {
                    case 0: // LP12
                        yNorm = 1.0f - std::pow(t, 1.35f);
                        break;
                    case 1: // LP24
                        yNorm = 1.0f - std::pow(t, 2.35f);
                        break;
                    case 2: // HP12
                        yNorm = std::pow(t, 1.35f);
                        break;
                    case 3: // HP24
                        yNorm = std::pow(t, 2.35f);
                        break;
                    case 4: // BandPass
                    {
                        const auto d = std::abs(t - 0.5f) * 2.0f;
                        yNorm = juce::jmax(0.0f, 1.0f - d * d);
                        break;
                    }
                    case 5: // Notch
                    {
                        const auto d = std::abs(t - 0.5f) * 2.0f;
                        yNorm = 0.12f + 0.88f * juce::jlimit(0.0f, 1.0f, d * d);
                        break;
                    }
                    case 6: // AllPass
                        yNorm = 0.55f;
                        break;
                    default:
                        break;
                }

                const auto y = bottom - juce::jlimit(0.0f, 1.0f, yNorm) * (bottom - top);
                response.lineTo(x, y);
            }

            g.setColour(juce::Colour::fromRGB(124, 206, 255));
            g.strokePath(response, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            if (idx == 4)
            {
                const auto barW = width / 7.0f;
                const auto centerX = (left + right) * 0.5f;
                g.setColour(juce::Colour::fromRGBA(124, 206, 255, 95));
                g.fillRect(centerX - barW * 1.6f, bottom - (bottom - top) * 0.40f, barW, (bottom - top) * 0.40f);
                g.fillRect(centerX - barW * 0.5f, bottom - (bottom - top) * 0.78f, barW, (bottom - top) * 0.78f);
                g.fillRect(centerX + barW * 0.6f, bottom - (bottom - top) * 0.48f, barW, (bottom - top) * 0.48f);
            }
        }
    }

}

void SynthProjectAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    if (!presetBrowserVisible)
    {
        return;
    }

    // The preset browser is drawn as a modal-like sheet over the main UI.
    // We retain context by blurring/dimming the rest of the editor instead of
    // switching to a separate window.
    const auto panelBounds = presetBrowserPanel.getBounds().toFloat();
    const auto panelHole = panelBounds.expanded(1.0f);

    juce::Path outsidePanelMask;
    outsidePanelMask.setUsingNonZeroWinding(false);
    outsidePanelMask.addRectangle(getLocalBounds().toFloat());
    outsidePanelMask.addRoundedRectangle(panelHole, 10.0f);

    if (presetBrowserBackdropSnapshot.isValid())
    {
        g.saveState();
        g.reduceClipRegion(outsidePanelMask);

        // Multi-offset snapshot blend gives an inexpensive full-UI blur effect.
        g.setOpacity(0.075f);
        for (int dy = -6; dy <= 6; dy += 2)
        {
            for (int dx = -6; dx <= 6; dx += 2)
            {
                if (dx == 0 && dy == 0)
                {
                    continue;
                }
                g.drawImageAt(presetBrowserBackdropSnapshot, dx, dy, false);
            }
        }

        g.setOpacity(0.14f);
        g.drawImageAt(presetBrowserBackdropSnapshot, 0, 0, false);
        g.setOpacity(1.0f);
        g.restoreState();
    }

    g.setColour(juce::Colour::fromRGBA(0, 0, 0, 180));
    g.fillPath(outsidePanelMask);
}

void SynthProjectAudioProcessorEditor::resized()
{
    // Layout policy:
    // - Header prioritizes logo/preset bar/fx cards for quick performance edits.
    // - Mid section hosts core synth controls.
    // - Bottom section reserves reliable space for performance strip + keyboard.
    // This balancing intentionally avoids dramatic jumps while resizing.
    auto bounds = getLocalBounds().reduced(16);

    const auto headerHeight = juce::jlimit(210, 320, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.40)));
    const auto controlsHeight = juce::jlimit(150, 270, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.34)));
    const auto keyboardHeight = juce::jlimit(106, 144, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.15)));
    // const auto statusHeight = 36;
    const auto sectionGap = 10;

    headerArea = bounds.removeFromTop(headerHeight);
    logoPanelArea = { headerArea.getX() + 4, headerArea.getY() + 4, 160, 160 };
#if PX3_DEBUG_PANEL
    debugToggleButton.setBounds(logoPanelArea.getX() + 18,
                                logoPanelArea.getBottom() + 8,
                                logoPanelArea.getWidth() - 36,
                                24);
#endif
    headerPlaceholderArea = { logoPanelArea.getRight() + 12,
                              headerArea.getY(),
                              juce::jmax(0, headerArea.getRight() - (logoPanelArea.getRight() + 12)),
                              headerArea.getHeight() };

    auto topArea = headerPlaceholderArea.reduced(4, 2);
    auto presetRow = topArea.removeFromTop(28);
    topArea.removeFromTop(4);
    const auto topGap = 8;
    updateFxSectionTargets(topArea, topGap);
    layoutFxSectionsFromCurrentAreas();

    presetBarArea = presetRow;

    auto presetLayout = presetRow;
    presetPrevButton.setBounds(presetLayout.removeFromLeft(26));
    presetLayout.removeFromLeft(4);
    presetExportButton.setBounds(presetLayout.removeFromRight(74));
    presetLayout.removeFromRight(4);
    presetImportButton.setBounds(presetLayout.removeFromRight(74));
    presetLayout.removeFromRight(4);
    presetDeleteButton.setBounds(presetLayout.removeFromRight(52));
    presetLayout.removeFromRight(4);
    presetSaveAsButton.setBounds(presetLayout.removeFromRight(84));
    presetLayout.removeFromRight(4);
    presetSaveButton.setBounds(presetLayout.removeFromRight(64));
    presetLayout.removeFromRight(4);
    presetFavoriteButton.setBounds(presetLayout.removeFromRight(28));
    presetLayout.removeFromRight(4);
    presetNextButton.setBounds(presetLayout.removeFromRight(26));
    presetLayout.removeFromRight(8);
    presetNameButton.setBounds(presetLayout);

    sourceEnginePanel.setBounds(topSpareSectionArea.reduced(1));

    bounds.removeFromTop(sectionGap);

    const auto desiredControlsHeight = juce::jmax(controlsHeight, bounds.getHeight() - keyboardHeight);
    controlsArea = bounds.removeFromTop(juce::jlimit(0, bounds.getHeight(), desiredControlsHeight));
    // bounds.removeFromTop(sectionGap);
    // midiStatusArea = bounds.removeFromBottom(statusHeight);
    // bounds.removeFromBottom(sectionGap);

    auto keyboardRow = bounds.reduced(4, 0);
    const auto perfWidth = juce::jlimit(112, 190, keyboardRow.getWidth() / 8);
    performanceControlsArea = keyboardRow.removeFromLeft(perfWidth);

    performanceControls.setBounds(performanceControlsArea);
    pianoKeyboard.setBounds(keyboardRow);
    // midiStatusLabel.setBounds(midiStatusArea.withTrimmedLeft(180).withTrimmedRight(180));

    // Knob-group widths are weighted ratios rather than equal columns because
    // OSC and AMP ENV carry more controls than LFO/OUTPUT.
    const auto groupGap = 20;
    auto groupsSpan = controlsArea.reduced(8, 8);
    groupsSpan.removeFromTop(20);

    const auto usableWidth = groupsSpan.getWidth() - (groupGap * 4);
    const auto usableWidthD = static_cast<double>(usableWidth);
    const auto oscWidth = static_cast<int>(std::lround(usableWidthD * 0.27));
    const auto filterWidth = static_cast<int>(std::lround(usableWidthD * 0.18));
    const auto envWidth = static_cast<int>(std::lround(usableWidthD * 0.28));
    const auto lfoWidth = static_cast<int>(std::lround(usableWidthD * 0.14));
    const auto outWidth = usableWidth - oscWidth - filterWidth - envWidth - lfoWidth;

    auto x = groupsSpan.getX();
    knobGroupAreas[0] = { x, groupsSpan.getY(), oscWidth, groupsSpan.getHeight() };
    x += oscWidth + groupGap;
    knobGroupAreas[1] = { x, groupsSpan.getY(), filterWidth, groupsSpan.getHeight() };
    x += filterWidth + groupGap;
    knobGroupAreas[2] = { x, groupsSpan.getY(), envWidth, groupsSpan.getHeight() };
    x += envWidth + groupGap;
    knobGroupAreas[3] = { x, groupsSpan.getY(), lfoWidth, groupsSpan.getHeight() };
    x += lfoWidth + groupGap;
    knobGroupAreas[4] = { x, groupsSpan.getY(), outWidth, groupsSpan.getHeight() };

    layoutKnobGroup(knobGroupAreas[0], 0, 3, kGroupAccents[0]);
    layoutKnobGroup(knobGroupAreas[1], 3, 2, kGroupAccents[1]);
    layoutKnobGroup(knobGroupAreas[3], 9, 1, kGroupAccents[3]);
    layoutKnobGroup(knobGroupAreas[4], 10, 1, kGroupAccents[4]);

    if (envelopeGraph != nullptr)
    {
        auto envArea = knobGroupAreas[2].reduced(10, 8);
        envArea.removeFromTop(30);
        envelopeGraph->setBounds(envArea);
    }

    {
        auto oscArea = knobGroupAreas[0].reduced(12, 8);
        auto oscLeft = oscArea.removeFromLeft(oscArea.getWidth() / 2).reduced(0, 0);
        auto oscRight = oscArea.reduced(0, 0);
        auto modeRow = juce::Rectangle<int>(oscLeft.getX(), oscLeft.getBottom() - 22, oscLeft.getWidth(), 18);
        auto vowelRow = juce::Rectangle<int>(oscRight.getX(), oscRight.getBottom() - 44, oscRight.getWidth(), 18);

        auto modeLabelArea = modeRow.removeFromLeft(52);
        oscModeLabel.setBounds(modeLabelArea);
        oscModeBox.setBounds(modeRow.reduced(1, 0));

        auto vowelLabelArea = vowelRow.removeFromLeft(52);
        oscVowelLabel.setBounds(vowelLabelArea);
        oscVowelBox.setBounds(vowelRow.reduced(1, 0));
    }

    {
        auto filterArea = knobGroupAreas[1].reduced(12, 8);
        const auto row = juce::Rectangle<int>(filterArea.getX(),
                                              filterArea.getBottom() - 22,
                                              filterArea.getWidth(),
                                              18);
        filterTypeBox.setBounds(row.reduced(1, 0));
    }

    {
        auto lfoArea = knobGroupAreas[3].reduced(10, 8);
        const auto assignRow = juce::Rectangle<int>(lfoArea.getX(),
                                                    lfoArea.getBottom() - 22,
                                                    lfoArea.getWidth(),
                                                    18);
        auto row = assignRow;
        auto labelArea = row.removeFromLeft(52);
        lfoAssignLabel.setBounds(labelArea);
        lfoAssignBox.setBounds(row.reduced(1, 0));

        const auto readoutY = juce::jlimit(lfoArea.getY(),
                                           assignRow.getY() - 16,
                                           lfoFrequencyKnob.getBottom() + 4);
        lfoFrequencyValueLabel.setBounds(lfoArea.getX(), readoutY, lfoArea.getWidth(), 14);
    }

    const auto browserWidth = juce::jlimit(520, 760, getWidth() - 120);
    const auto browserHeight = juce::jlimit(360, 520, getHeight() - 120);
    auto browserX = (getWidth() - browserWidth) / 2;
    auto browserY = (getHeight() - browserHeight) / 2;

    if (presetBrowserPanel.getWidth() > 0 && presetBrowserPanel.getHeight() > 0)
    {
        browserX = presetBrowserPanel.getX();
        browserY = presetBrowserPanel.getY();
    }

    browserX = juce::jlimit(8, juce::jmax(8, getWidth() - browserWidth - 8), browserX);
    browserY = juce::jlimit(8, juce::jmax(8, getHeight() - browserHeight - 8), browserY);
    presetBrowserPanel.setBounds(browserX, browserY, browserWidth, browserHeight);

    auto browserArea = presetBrowserPanel.getLocalBounds().reduced(10);
    presetBrowserTitle.setBounds(browserArea.removeFromTop(24));
    browserArea.removeFromTop(6);

    auto filterRow = browserArea.removeFromTop(26);
    presetScopeBox.setBounds(filterRow.removeFromLeft(120));
    filterRow.removeFromLeft(6);
    presetCategoryBox.setBounds(filterRow.removeFromLeft(170));
    filterRow.removeFromLeft(6);
    presetSearchEditor.setBounds(filterRow);

    browserArea.removeFromTop(6);
    auto footer = browserArea.removeFromBottom(74);
    presetListBox.setBounds(browserArea.removeFromLeft(browserArea.getWidth() * 2 / 3));
    browserArea.removeFromLeft(8);
    presetBrowserDetails.setBounds(browserArea);

    auto footerRight = footer.removeFromRight(190);
    presetBrowserLoadButton.setBounds(footerRight.removeFromLeft(90));
    footerRight.removeFromLeft(10);
    presetBrowserCloseButton.setBounds(footerRight.removeFromLeft(90));

}

void SynthProjectAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    if (presetBrowserVisible)
    {
        const auto mousePos = event.getPosition();
        if (!presetBrowserPanel.getBounds().contains(mousePos))
        {
            closePresetBrowser();
        }

        if (presetBrowserPanel.getBounds().contains(mousePos))
        {
            const auto local = mousePos - presetBrowserPanel.getPosition();
            if (juce::Rectangle<int>(0, 0, presetBrowserPanel.getWidth(), 30).contains(local))
            {
                presetBrowserDragging = true;
                presetBrowserDragOffset = local;
            }
            presetBrowserPanel.toFront(false);
        }
        return;
    }

    const auto point = event.getPosition();
    const auto sectionId = fxSectionAtPoint(point);
    if (sectionId < 0)
    {
        return;
    }

    draggingFxSection = sectionId;
    pressedFxSection = sectionId;
    fxDragStartPoint = point;
    fxDragHasMoved = false;
    draggingSectionOffsetX = static_cast<float>(point.x) - fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)].getX();
}

void SynthProjectAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (presetBrowserVisible)
    {
        if (presetBrowserDragging)
        {
            auto newTopLeft = event.getPosition() - presetBrowserDragOffset;
            const auto margin = 8;
            const auto maxX = getWidth() - presetBrowserPanel.getWidth() - margin;
            const auto maxY = getHeight() - presetBrowserPanel.getHeight() - margin;
            newTopLeft.x = juce::jlimit(margin, juce::jmax(margin, maxX), newTopLeft.x);
            newTopLeft.y = juce::jlimit(margin, juce::jmax(margin, maxY), newTopLeft.y);
            presetBrowserPanel.setTopLeftPosition(newTopLeft);
            repaint();
        }
        return;
    }

    if (draggingFxSection < 0)
    {
        return;
    }

    const auto point = event.getPosition();
    if (!fxDragHasMoved)
    {
        if (point.getDistanceFrom(fxDragStartPoint) < 4)
        {
            return;
        }
        fxDragHasMoved = true;
    }

    auto area = fxSectionCurrentAreas[static_cast<std::size_t>(draggingFxSection)];
    const auto minX = static_cast<float>(fxSectionSlots[0].getX());
    const auto maxX = static_cast<float>(fxSectionSlots[2].getRight() - fxSectionSlots[2].getWidth());
    auto newX = static_cast<float>(point.x) - draggingSectionOffsetX;
    newX = juce::jlimit(minX, maxX, newX);
    area.setX(newX);
    area.setY(fxSectionTargetAreas[static_cast<std::size_t>(draggingFxSection)].getY());
    fxSectionCurrentAreas[static_cast<std::size_t>(draggingFxSection)] = area;

    const auto centerX = area.getCentreX();
    int targetSlot = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (int slot = 0; slot < 3; ++slot)
    {
        const auto slotCenterX = static_cast<float>(fxSectionSlots[static_cast<std::size_t>(slot)].getCentreX());
        const auto distance = std::abs(centerX - slotCenterX);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            targetSlot = slot;
        }
    }

    moveFxSectionToSlot(draggingFxSection, targetSlot);
    layoutFxSectionsFromCurrentAreas();
    repaint(headerPlaceholderArea);
}

void SynthProjectAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    if (presetBrowserVisible)
    {
        juce::ignoreUnused(event);
        presetBrowserDragging = false;
        return;
    }

    if (draggingFxSection < 0)
    {
        return;
    }

    const auto releasePoint = event.getPosition();
    const auto isHeaderClick = !fxDragHasMoved && pressedFxSection >= 0;
    if (isHeaderClick)
    {
        const auto sectionBounds = fxSectionCurrentAreas[static_cast<std::size_t>(pressedFxSection)].toNearestInt();
        const auto headerBounds = sectionBounds.withHeight(24);
        if (headerBounds.contains(releasePoint))
        {
            switch (pressedFxSection)
            {
                case kFxSectionDrive:
                {
                    auto& p = audioProcessor.getRobEnabledParam();
                    p.beginChangeGesture();
                    p.setValueNotifyingHost(p.convertTo0to1(!p.get()));
                    p.endChangeGesture();
                    break;
                }
                case kFxSectionDelay:
                {
                    auto& p = audioProcessor.getDelayEnabledParam();
                    p.beginChangeGesture();
                    p.setValueNotifyingHost(p.convertTo0to1(!p.get()));
                    p.endChangeGesture();
                    break;
                }
                case kFxSectionReverb:
                {
                    auto& p = audioProcessor.getReverbEnabledParam();
                    p.beginChangeGesture();
                    p.setValueNotifyingHost(p.convertTo0to1(!p.get()));
                    p.endChangeGesture();
                    break;
                }
                default:
                    break;
            }
            refreshFxBypassUI();
        }
    }

    fxSectionCurrentAreas[static_cast<std::size_t>(draggingFxSection)] =
        fxSectionTargetAreas[static_cast<std::size_t>(draggingFxSection)];
    draggingFxSection = -1;
    pressedFxSection = -1;
    fxDragHasMoved = false;
    commitFxOrderToProcessor("USER", "USER_DRAG_END", -1, -1);
    layoutFxSectionsFromCurrentAreas();
    repaint(headerPlaceholderArea);
}

void SynthProjectAudioProcessorEditor::updateFxSectionTargets(const juce::Rectangle<int>& topArea, int topGap)
{
    auto layoutArea = topArea;
    const auto sectionWidth = juce::jmax(108, (layoutArea.getWidth() - (topGap * 3)) / 4);

    for (int i = 0; i < 3; ++i)
    {
        fxSectionSlots[static_cast<std::size_t>(i)] = layoutArea.removeFromLeft(sectionWidth);
        layoutArea.removeFromLeft(topGap);
    }
    topSpareSectionArea = layoutArea;

    for (int stage = 0; stage < 3; ++stage)
    {
        const auto slotIndex = indexForFxSection(stage);
        if (slotIndex >= 0)
        {
            fxSectionTargetAreas[static_cast<std::size_t>(stage)] =
                fxSectionSlots[static_cast<std::size_t>(slotIndex)].toFloat();
        }
    }

    if (!fxSectionsInitialized)
    {
        fxSectionCurrentAreas = fxSectionTargetAreas;
        fxSectionsInitialized = true;
    }
}

void SynthProjectAudioProcessorEditor::layoutFxSectionsFromCurrentAreas()
{
    robSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionDrive)].toNearestInt();
    isaacSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionDelay)].toNearestInt();
    reverbSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionReverb)].toNearestInt();

    {
        auto robInner = robSectionArea.reduced(10, 8);
        robBypassButton.setBounds(robSectionArea.getX() + 8, robSectionArea.getY() + 5, 22, 18);
        robInner.removeFromTop(24);
        auto bottomArea = robInner.removeFromBottom(46);
        const auto knobSize = juce::jmin(82, juce::jmin(robInner.getWidth(), robInner.getHeight()));
        robWarmthKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(robInner.getCentre()));
        robWarmthLabel.setBounds(bottomArea.removeFromTop(22));
    }

    {
        auto isaacInner = isaacSectionArea.reduced(10, 8);
        delayBypassButton.setBounds(isaacSectionArea.getX() + 8, isaacSectionArea.getY() + 5, 22, 18);
        isaacInner.removeFromTop(24);
        auto delayControlsArea = isaacInner.removeFromBottom(120);

        auto rowMode = delayControlsArea.removeFromBottom(22);
        delayControlsArea.removeFromBottom(2);
        auto rowAlgo = delayControlsArea.removeFromBottom(22);
        auto rowSync = delayControlsArea.removeFromBottom(22);
        delayControlsArea.removeFromBottom(2);
        auto miniArea = delayControlsArea;

        auto leftMini = miniArea.removeFromLeft(miniArea.getWidth() / 2).reduced(2, 0);
        auto rightMini = miniArea.reduced(2, 0);

        auto leftLabel = leftMini.removeFromBottom(16);
        auto rightLabel = rightMini.removeFromBottom(16);

        const auto miniKnobSize = juce::jlimit(30,
                                               44,
                                               juce::jmin(leftMini.getWidth(), juce::jmin(leftMini.getHeight(), rightMini.getHeight())));
        const auto leftKnobBounds = juce::Rectangle<int>(miniKnobSize, miniKnobSize).withCentre(leftMini.getCentre());
        const auto rightKnobBounds = juce::Rectangle<int>(miniKnobSize, miniKnobSize).withCentre(rightMini.getCentre());
        delayTimeKnob.setBounds(leftKnobBounds);
        delayFeedbackKnob.setBounds(rightKnobBounds);

        constexpr int miniLabelHeight = 16;
        const auto leftLabelWidth = juce::jmin(leftMini.getWidth(), miniKnobSize + 14);
        const auto rightLabelWidth = juce::jmin(rightMini.getWidth(), miniKnobSize + 14);
        delayTimeLabel.setBounds(juce::Rectangle<int>(leftLabelWidth, miniLabelHeight)
                         .withCentre({ leftKnobBounds.getCentreX(), leftLabel.getCentreY() }));
        delayFeedbackLabel.setBounds(juce::Rectangle<int>(rightLabelWidth, miniLabelHeight)
                         .withCentre({ rightKnobBounds.getCentreX(), rightLabel.getCentreY() }));

        auto algoLabelArea = rowAlgo.removeFromLeft(56);
        delayAlgoLabel.setBounds(algoLabelArea);
        delayAlgoBox.setBounds(rowAlgo.reduced(2, 1));

        auto syncLabelArea = rowSync.removeFromLeft(56);
        granularSyncLabel.setBounds(syncLabelArea);
        granularSyncBox.setBounds(rowSync.reduced(2, 1));

        auto modeLabelArea = rowMode.removeFromLeft(56);
        granularModeLabel.setBounds(modeLabelArea);
        granularModeBox.setBounds(rowMode.reduced(2, 1));

        const auto knobSize = juce::jmin(80, juce::jmin(isaacInner.getWidth(), isaacInner.getHeight()));
        isaacTextureKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(isaacInner.getCentre()));
        isaacTextureLabel.setBounds(juce::Rectangle<int>(isaacInner.getX(), isaacInner.getBottom() - 18, isaacInner.getWidth(), 16));
    }

    {
        auto reverbInner = reverbSectionArea.reduced(10, 8);
        reverbBypassButton.setBounds(reverbSectionArea.getX() + 8, reverbSectionArea.getY() + 5, 22, 18);
        reverbInner.removeFromTop(24);
        auto bottomArea = reverbInner.removeFromBottom(46);
        auto labelArea = bottomArea.removeFromTop(22);
        reverbTypeLabel.setBounds(bottomArea.removeFromLeft(56));
        reverbTypeBox.setBounds(bottomArea.reduced(2, 1));

        const auto knobSize = juce::jmin(82, juce::jmin(reverbInner.getWidth(), reverbInner.getHeight()));
        reverbKnob.setBounds(juce::Rectangle<int>(knobSize, knobSize).withCentre(reverbInner.getCentre()));
        reverbLabel.setBounds(labelArea);
    }
}

void SynthProjectAudioProcessorEditor::animateFxSections()
{
    if (!fxSectionsInitialized)
    {
        return;
    }

    bool changed = false;
    for (int sectionId = 0; sectionId < 3; ++sectionId)
    {
        if (sectionId == draggingFxSection)
        {
            continue;
        }

        auto current = fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)];
        const auto target = fxSectionTargetAreas[static_cast<std::size_t>(sectionId)];
        current = current.transformedBy(juce::AffineTransform::translation((target.getX() - current.getX()) * 0.30f,
                                                                            (target.getY() - current.getY()) * 0.30f));
        current.setSize(target.getWidth(), target.getHeight());

        const auto dx = std::abs(current.getX() - target.getX());
        const auto dy = std::abs(current.getY() - target.getY());
        if (dx < 0.45f && dy < 0.45f)
        {
            current = target;
        }

        if (current != fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)])
        {
            fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)] = current;
            changed = true;
        }
    }

    if (changed)
    {
        layoutFxSectionsFromCurrentAreas();
        repaint(headerPlaceholderArea);
    }
}

int SynthProjectAudioProcessorEditor::indexForFxSection(int sectionId) const
{
    for (int i = 0; i < 3; ++i)
    {
        if (fxSectionOrder[static_cast<std::size_t>(i)] == sectionId)
        {
            return i;
        }
    }

    return -1;
}

int SynthProjectAudioProcessorEditor::fxSectionAtPoint(juce::Point<int> point) const
{
    for (int sectionId = 0; sectionId < 3; ++sectionId)
    {
        if (fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)].toNearestInt().contains(point))
        {
            return sectionId;
        }
    }

    return -1;
}

void SynthProjectAudioProcessorEditor::moveFxSectionToSlot(int sectionId, int slotIndex)
{
    const auto fromIndex = indexForFxSection(sectionId);
    const auto toIndex = juce::jlimit(0, 2, slotIndex);
    if (fromIndex < 0 || fromIndex == toIndex)
    {
        return;
    }

    auto reordered = fxSectionOrder;
    const auto section = reordered[static_cast<std::size_t>(fromIndex)];

    if (fromIndex < toIndex)
    {
        for (int i = fromIndex; i < toIndex; ++i)
        {
            reordered[static_cast<std::size_t>(i)] = reordered[static_cast<std::size_t>(i + 1)];
        }
    }
    else
    {
        for (int i = fromIndex; i > toIndex; --i)
        {
            reordered[static_cast<std::size_t>(i)] = reordered[static_cast<std::size_t>(i - 1)];
        }
    }

    reordered[static_cast<std::size_t>(toIndex)] = section;
    fxSectionOrder = reordered;

    // Persist order immediately as slots change so host/project state always tracks UI order.
    commitFxOrderToProcessor("USER", "USER_DRAG", fromIndex, toIndex);

    for (int stage = 0; stage < 3; ++stage)
    {
        const auto slot = indexForFxSection(stage);
        if (slot >= 0)
        {
            fxSectionTargetAreas[static_cast<std::size_t>(stage)] =
                fxSectionSlots[static_cast<std::size_t>(slot)].toFloat();
        }
    }
}

void SynthProjectAudioProcessorEditor::commitFxOrderToProcessor(const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    audioProcessor.setFxProcessingOrderWithReason(fxSectionOrder, source, reason, fromIndex, toIndex);
}

void SynthProjectAudioProcessorEditor::rebuildPresetFilteredList()
{
    PresetManager::Query query;

    switch (presetScopeBox.getSelectedId())
    {
        case 2:
            query.includeFactory = true;
            query.includeUser = false;
            break;
        case 3:
            query.includeFactory = false;
            query.includeUser = true;
            break;
        case 4:
            query.favoritesOnly = true;
            break;
        default:
            break;
    }

    query.category = presetCategoryBox.getText();
    query.searchText = presetSearchEditor.getText();

    presetFiltered = presetManager.queryPresets(query);
    presetListBox.updateContent();
    presetListBox.repaint();

    if (hasCurrentPreset)
    {
        int row = -1;
        for (int i = 0; i < static_cast<int>(presetFiltered.size()); ++i)
        {
            if (presetFiltered[static_cast<std::size_t>(i)].file == currentPreset.file)
            {
                row = i;
                break;
            }
        }

        if (row >= 0)
        {
            presetListBox.selectRow(row);
        }
    }

    if (presetFiltered.empty())
    {
        presetBrowserDetails.setText("No presets match this filter.", juce::dontSendNotification);
    }
}

void SynthProjectAudioProcessorEditor::refreshPresetNameDisplay()
{
    juce::String name = hasCurrentPreset ? currentPreset.metadata.name : juce::String("INIT");
    if (currentPresetDirty)
    {
        name << "*";
    }

    presetNameButton.setButtonText(name);
    presetFavoriteButton.setToggleState(hasCurrentPreset && currentPreset.isFavorite, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::applyPresetRecord(const PresetManager::PresetRecord& record)
{
    juce::String error;
    if (!presetManager.loadPreset(record, error))
    {
        showPresetError("Preset Load Failed", error);
        return;
    }

    hasCurrentPreset = true;
    currentPreset = record;
    loadedStateHash = computeCurrentStateHash();
    currentPresetDirty = false;
    refreshPresetNameDisplay();
    repaint();
}

void SynthProjectAudioProcessorEditor::openPresetBrowser()
{
    presetBrowserBackdropSnapshot = createComponentSnapshot(getLocalBounds());
    presetBrowserVisible = true;
    presetBrowserDragging = false;
    presetBrowserPanel.setVisible(true);
    presetBrowserPanel.setAlwaysOnTop(true);
    presetBrowserPanel.toFront(true);
    rebuildPresetFilteredList();
    repaint();
}

void SynthProjectAudioProcessorEditor::closePresetBrowser()
{
    presetBrowserVisible = false;
    presetBrowserDragging = false;
    presetBrowserPanel.setAlwaysOnTop(false);
    presetBrowserPanel.setVisible(false);
    presetBrowserBackdropSnapshot = {};
    repaint();
}

void SynthProjectAudioProcessorEditor::showPresetError(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           title,
                                           message,
                                           "OK",
                                           this);
}

void SynthProjectAudioProcessorEditor::savePreset(bool saveAs)
{
    if (!saveAs && hasCurrentPreset && !currentPreset.isFactory)
    {
        PresetManager::PresetMetadata metadata = currentPreset.metadata;
        juce::String error;
        juce::File outFile;
        if (!presetManager.saveUserPreset(metadata, true, error, &outFile))
        {
            showPresetError("Save Failed", error);
            return;
        }

        presetManager.refreshIndex();
        rebuildPresetFilteredList();
        if (const auto* record = presetManager.findByFile(outFile))
        {
            currentPreset = *record;
            hasCurrentPreset = true;
            loadedStateHash = computeCurrentStateHash();
            currentPresetDirty = false;
            refreshPresetNameDisplay();
        }
        return;
    }

    auto initialCategory = hasCurrentPreset ? currentPreset.metadata.category : juce::String("LEADS");
    if (initialCategory.trim().isEmpty())
    {
        initialCategory = "LEADS";
    }

    auto defaultDir = presetManager.getUserPresetRootDir().getChildFile(initialCategory.toUpperCase());
    if (!defaultDir.exists())
    {
        defaultDir.createDirectory();
    }

    auto defaultName = hasCurrentPreset ? currentPreset.metadata.name : juce::String("New Preset");
    auto chooser = std::make_shared<juce::FileChooser>("Save P(X3) preset",
                                                        defaultDir.getChildFile(defaultName + ".px3preset"),
                                                        "*.px3preset");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             auto destination = fc.getResult();
                             if (destination == juce::File())
                             {
                                 return;
                             }

                             if (!destination.hasFileExtension(".px3preset"))
                             {
                                 destination = destination.withFileExtension(".px3preset");
                             }

                             PresetManager::PresetMetadata metadata;
                             metadata.name = destination.getFileNameWithoutExtension();
                             metadata.category = destination.getParentDirectory().getFileName();
                             metadata.author = hasCurrentPreset ? currentPreset.metadata.author : juce::String();
                             metadata.description = hasCurrentPreset ? currentPreset.metadata.description : juce::String();

                             bool overwrite = destination.existsAsFile();
                             if (overwrite)
                             {
                                 const auto proceed = juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                                                            "Overwrite Preset?",
                                                                                            "A preset with this name already exists. Overwrite it?",
                                                                                            "Overwrite",
                                                                                            "Cancel",
                                                                                            this,
                                                                                            nullptr);
                                 if (!proceed)
                                 {
                                     return;
                                 }
                             }

                             juce::String error;
                             juce::File outFile;
                             if (!presetManager.saveUserPreset(metadata, overwrite, error, &outFile))
                             {
                                 showPresetError("Save Failed", error);
                                 return;
                             }

                             presetManager.refreshIndex();
                             rebuildPresetFilteredList();
                             if (const auto* record = presetManager.findByFile(outFile))
                             {
                                 currentPreset = *record;
                                 hasCurrentPreset = true;
                                 loadedStateHash = computeCurrentStateHash();
                                 currentPresetDirty = false;
                                 refreshPresetNameDisplay();
                             }
                         });
}

void SynthProjectAudioProcessorEditor::deleteCurrentPreset()
{
    if (!hasCurrentPreset)
    {
        return;
    }

    if (currentPreset.isFactory || currentPreset.metadata.name.equalsIgnoreCase("INIT"))
    {
        showPresetError("Delete Not Allowed", "Factory and INIT presets cannot be deleted.");
        return;
    }

    auto ok = juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                  "Delete Preset",
                                                  "Delete \"" + currentPreset.metadata.name + "\"?",
                                                  "Delete",
                                                  "Cancel",
                                                  this,
                                                  nullptr);
    if (!ok)
    {
        return;
    }

    juce::String error;
    if (!presetManager.deleteUserPreset(currentPreset, error))
    {
        showPresetError("Delete Failed", error);
        return;
    }

    hasCurrentPreset = false;
    currentPresetDirty = false;
    loadedStateHash.clear();
    presetManager.refreshIndex();
    rebuildPresetFilteredList();
    refreshPresetNameDisplay();
}

void SynthProjectAudioProcessorEditor::importPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>("Import P(X3) preset",
                                                        juce::File(),
                                                        "*.px3preset");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             const auto file = fc.getResult();
                             if (!file.existsAsFile())
                             {
                                 return;
                             }

                             PresetManager::PresetRecord imported;
                             juce::String error;
                             if (!presetManager.importPreset(file, error, &imported))
                             {
                                 showPresetError("Import Failed", error);
                                 return;
                             }

                             rebuildPresetFilteredList();
                             applyPresetRecord(imported);
                         });
}

void SynthProjectAudioProcessorEditor::exportCurrentPreset()
{
    if (!hasCurrentPreset)
    {
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>("Export P(X3) preset",
                                                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                            .getChildFile(currentPreset.metadata.name + ".px3preset"),
                                                        "*.px3preset");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             auto destination = fc.getResult();
                             if (destination == juce::File())
                             {
                                 return;
                             }

                             if (!destination.hasFileExtension(".px3preset"))
                             {
                                 destination = destination.withFileExtension(".px3preset");
                             }

                             juce::String error;
                             if (!presetManager.exportPreset(currentPreset, destination, error))
                             {
                                 showPresetError("Export Failed", error);
                             }
                         });
}

juce::String SynthProjectAudioProcessorEditor::computeCurrentStateHash() const
{
    auto state = audioProcessor.createParameterStateTree();
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        return {};
    }

    const auto text = xml->toString();
    return juce::String::toHexString(static_cast<juce::int64>(text.hashCode64()));
}

void SynthProjectAudioProcessorEditor::updatePresetDirtyState()
{
    if (!hasCurrentPreset)
    {
        return;
    }

    const auto currentHash = computeCurrentStateHash();
    const auto dirty = loadedStateHash.isNotEmpty() && currentHash != loadedStateHash;
    if (dirty != currentPresetDirty)
    {
        currentPresetDirty = dirty;
        refreshPresetNameDisplay();
    }
}

int SynthProjectAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(presetFiltered.size());
}

void SynthProjectAudioProcessorEditor::paintListBoxItem(int rowNumber,
                                                        juce::Graphics& g,
                                                        int width,
                                                        int height,
                                                        bool rowIsSelected)
{
    g.fillAll(rowIsSelected ? juce::Colour::fromRGBA(76, 120, 184, 170)
                            : juce::Colour::fromRGBA(0, 0, 0, 0));

    if (rowNumber < 0 || rowNumber >= static_cast<int>(presetFiltered.size()))
    {
        return;
    }

    const auto& item = presetFiltered[static_cast<std::size_t>(rowNumber)];
    const auto favoritePrefix = item.isFavorite ? juce::String("★ ") : juce::String();
    const auto sourcePrefix = item.isFactory ? juce::String("[F] ") : juce::String("[U] ");

    g.setColour(juce::Colour::fromRGB(234, 234, 234));
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(favoritePrefix + sourcePrefix + item.metadata.name,
               6,
               0,
               width - 8,
               height,
               juce::Justification::centredLeft,
               true);
}

void SynthProjectAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(presetFiltered.size()))
    {
        presetBrowserDetails.setText("", juce::dontSendNotification);
        return;
    }

    const auto& preset = presetFiltered[static_cast<std::size_t>(lastRowSelected)];
    juce::String details;
    details << "Name: " << preset.metadata.name << "\n";
    details << "Category: " << preset.metadata.category << "\n";
    details << "Source: " << (preset.isFactory ? "Factory" : "User") << "\n";
    if (preset.metadata.author.isNotEmpty())
    {
        details << "Author: " << preset.metadata.author << "\n";
    }
    if (preset.metadata.description.isNotEmpty())
    {
        details << "\n" << preset.metadata.description;
    }

    presetBrowserDetails.setText(details, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshOscillatorModeUI()
{
    const auto paramModeIndex = audioProcessor.getOscillatorModeParam().getIndex();
    if (oscModeBox.getSelectedItemIndex() != paramModeIndex)
    {
        oscModeBox.setSelectedItemIndex(paramModeIndex, juce::dontSendNotification);
    }

    const auto paramVowelIndex = audioProcessor.getOscVowelParam().getIndex();
    if (oscVowelBox.getSelectedItemIndex() != paramVowelIndex)
    {
        oscVowelBox.setSelectedItemIndex(paramVowelIndex, juce::dontSendNotification);
    }

    const auto modeIndex = juce::jmax(0, oscModeBox.getSelectedItemIndex());
    if (modeIndex == lastOscModeIndex)
    {
        return;
    }

    lastOscModeIndex = modeIndex;

    struct ModeUi
    {
        const char* a;
        const char* b;
        const char* c;
        int count;
        bool showVowel;
    };

    static const std::array<ModeUi, 20> modeUi { {
        { "", "", "", 0, false },                // SINE
        { "", "", "", 0, false },                // SAW
        { "", "", "", 0, false },                // SQUARE
        { "", "", "", 0, false },                // TRIANGLE
        { "COLOR", "", "", 1, false },           // NOISE
        { "COLOR", "", "", 1, false },           // PINK NOISE
        { "DETUNE", "SPREAD", "", 2, false },    // SUPER SAW
        { "WIDTH", "", "", 1, false },           // PWM
        { "POSITION", "", "", 1, false },        // WAVETABLE
        { "TILT", "ODD/EVEN", "ROLL", 3, false },// ADDITIVE
        { "MORPH", "COLOR", "", 2, true },       // FORMANT
        { "RATIO", "INDEX", "", 2, false },      // FM
        { "SYNC", "DRIVE", "", 2, false },       // HARD SYNC
        { "DECAY", "BRIGHT", "", 2, false },     // KARPLUS
        { "TONE", "CLICK", "", 2, false },       // ORGAN
        { "BITS", "RATE", "", 2, false },        // DIGITAL
        { "DECAY", "MATERIAL", "", 2, false },   // PHYSICAL
        { "TRANS", "BODY", "CHAOS", 3, false },  // ROB
        { "SPREAD", "ODD/EVEN", "ROLL", 3, false }, // ISAAC
        { "MORPH", "CHAR", "MOVE", 3, false }    // PX3
    } };

    const auto ui = modeUi[static_cast<std::size_t>(juce::jlimit(0, static_cast<int>(modeUi.size()) - 1, modeIndex))];

    const std::array<juce::Slider*, 3> sliders { &oscSineKnob, &oscSawKnob, &oscSquareKnob };
    const std::array<KnobLabel*, 3> labels { &oscSineLabel, &oscSawLabel, &oscSquareLabel };
    const std::array<const char*, 3> texts { ui.a, ui.b, ui.c };

    for (int i = 0; i < 3; ++i)
    {
        const auto show = i < ui.count;
        sliders[static_cast<std::size_t>(i)]->setVisible(show);
        labels[static_cast<std::size_t>(i)]->setVisible(show);
        labels[static_cast<std::size_t>(i)]->setText(show ? texts[static_cast<std::size_t>(i)] : "", juce::dontSendNotification);
        const auto tooltipText = show ? juce::String(texts[static_cast<std::size_t>(i)]) : juce::String();
        labels[static_cast<std::size_t>(i)]->setTooltip(tooltipText);
        sliders[static_cast<std::size_t>(i)]->setTooltip(tooltipText);
    }

    oscVowelBox.setVisible(ui.showVowel);
    oscVowelLabel.setVisible(ui.showVowel);

    // Mode changes alter how many OSC knobs are visible; re-run OSC layout immediately
    // so hidden/visible knobs don't keep stale bounds from the previous mode.
    layoutKnobGroup(knobGroupAreas[0], 0, 3, kGroupAccents[0]);
    repaint();
}

void SynthProjectAudioProcessorEditor::layoutKnobGroup(const juce::Rectangle<int>& groupArea,
                                                        int startIndex,
                                                        int knobCount,
                                                        const juce::Colour& sectionAccent)
{
    if (knobCount <= 0)
    {
        return;
    }

    const auto labelHeight = 22;
    const auto minGap = 8;

    if (startIndex == 0)
    {
        auto oscArea = groupArea.reduced(10, 8);
        oscArea.removeFromTop(30);

        oscArea.removeFromLeft(oscArea.getWidth() / 2);
        auto oscRight = oscArea.reduced(2, 0);
        oscRight.removeFromBottom(52);

        std::vector<int> visibleIndices;
        for (int i = 0; i < knobCount; ++i)
        {
            auto& binding = knobBindings[static_cast<std::size_t>(startIndex + i)];
            if (binding.slider != nullptr && binding.slider->isVisible())
            {
                visibleIndices.push_back(i);
            }
        }

        if (visibleIndices.empty())
        {
            return;
        }

        const auto visibleCount = static_cast<int>(visibleIndices.size());
        const auto totalGap = juce::jmax(0, visibleCount - 1) * minGap;
        const auto rowHeight = juce::jmax(62, (oscRight.getHeight() - totalGap) / visibleCount);
        const auto maxKnobByHeight = juce::jmax(44, rowHeight - labelHeight - 8);
        const auto maxKnobByWidth = juce::jmax(40, oscRight.getWidth() - 12);
        const auto knobSize = juce::jlimit(44, 78, juce::jmin(maxKnobByWidth, maxKnobByHeight));
        auto rowY = oscRight.getY();

        const auto setTextSizedLabel = [&oscRight](juce::Label* label, const juce::Rectangle<int>& rowBounds, int knobCenterX)
        {
            if (label == nullptr)
            {
                return;
            }

            const auto approxTextWidth = static_cast<int>(label->getText().length()) * 8 + 16;
            const auto textWidth = juce::jlimit(42,
                                                oscRight.getWidth() - 6,
                                                approxTextWidth);
            const auto left = juce::jlimit(rowBounds.getX(),
                                           rowBounds.getRight() - textWidth,
                                           knobCenterX - textWidth / 2);
            label->setBounds(left, rowBounds.getY(), textWidth, 18);
        };

        if (visibleCount == 3)
        {
            const auto smallKnobSize = juce::jlimit(38, 62, knobSize - 8);
            constexpr int threeKnobCenterGap = 12;
            constexpr int threeKnobVerticalGap = 6;
            auto work = oscRight;

            auto topRow = work.removeFromTop(work.getHeight() / 2 + 4);
            work.removeFromTop(threeKnobVerticalGap);
            auto bottomRow = work.reduced(0, 0);

            {
                const auto i = visibleIndices[0];
                auto& binding = knobBindings[static_cast<std::size_t>(startIndex + i)];
                if (binding.slider != nullptr && binding.label != nullptr)
                {
                    binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);

                    auto row = topRow.reduced(2, 1);
                    auto labelRow = row.removeFromTop(labelHeight);
                    row.removeFromTop(8);

                    const auto knobBounds = juce::Rectangle<int>(smallKnobSize, smallKnobSize).withCentre(row.getCentre());
                    setTextSizedLabel(binding.label, labelRow, knobBounds.getCentreX());
                    binding.slider->setBounds(knobBounds);
                }
            }

            {
                auto leftCell = bottomRow.removeFromLeft((bottomRow.getWidth() - threeKnobCenterGap) / 2).reduced(1, 1);
                bottomRow.removeFromLeft(threeKnobCenterGap);
                auto rightCell = bottomRow.reduced(1, 1);
                const std::array<juce::Rectangle<int>, 2> cells { leftCell, rightCell };

                for (int idx = 0; idx < 2; ++idx)
                {
                    const auto vi = visibleIndices[static_cast<std::size_t>(idx + 1)];
                    auto& binding = knobBindings[static_cast<std::size_t>(startIndex + vi)];
                    if (binding.slider == nullptr || binding.label == nullptr)
                    {
                        continue;
                    }

                    binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);

                    auto row = cells[static_cast<std::size_t>(idx)];
                    auto labelRow = row.removeFromTop(labelHeight);
                    row.removeFromTop(8);

                    const auto cellKnobSize = juce::jlimit(36,
                                                           smallKnobSize,
                                                           juce::jmin(row.getWidth() - 2, row.getHeight() - 2));
                    const auto knobBounds = juce::Rectangle<int>(cellKnobSize, cellKnobSize).withCentre(row.getCentre());
                    setTextSizedLabel(binding.label, labelRow, knobBounds.getCentreX());
                    binding.slider->setBounds(knobBounds);
                }
            }

            return;
        }

        if (visibleCount == 2)
        {
            constexpr int twoKnobHorizontalGap = 34;
            auto row = oscRight.reduced(0, 4);
            const auto labelRow = row.removeFromTop(labelHeight);
            row.removeFromTop(8);

            const auto pairWidth = juce::jmax(2, row.getWidth() - twoKnobHorizontalGap);
            const auto cellWidth = juce::jmax(1, pairWidth / 2);
            const auto rowLeft = row.getX() + (row.getWidth() - (cellWidth * 2 + twoKnobHorizontalGap)) / 2;

            auto leftCell = juce::Rectangle<int>(rowLeft, row.getY(), cellWidth, row.getHeight()).reduced(2, 0);
            auto rightCell = juce::Rectangle<int>(rowLeft + cellWidth + twoKnobHorizontalGap,
                                                  row.getY(),
                                                  cellWidth,
                                                  row.getHeight()).reduced(2, 0);
            const std::array<juce::Rectangle<int>, 2> cells { leftCell, rightCell };

            const auto maxByWidth = juce::jmax(36, cellWidth - 4);
            const auto maxByHeight = juce::jmax(36, row.getHeight() - 2);
            const auto cellKnobSize = juce::jlimit(38, 72, juce::jmin(maxByWidth, maxByHeight));

            for (int idx = 0; idx < 2; ++idx)
            {
                const auto vi = visibleIndices[static_cast<std::size_t>(idx)];
                auto& binding = knobBindings[static_cast<std::size_t>(startIndex + vi)];
                if (binding.slider == nullptr || binding.label == nullptr)
                {
                    continue;
                }

                binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);

                const auto knobBounds = juce::Rectangle<int>(cellKnobSize, cellKnobSize)
                                            .withCentre(cells[static_cast<std::size_t>(idx)].getCentre());
                setTextSizedLabel(binding.label, labelRow, knobBounds.getCentreX());
                binding.slider->setBounds(knobBounds);
            }

            return;
        }

        for (int visibleIdx = 0; visibleIdx < visibleCount; ++visibleIdx)
        {
            const auto i = visibleIndices[static_cast<std::size_t>(visibleIdx)];
            auto& binding = knobBindings[static_cast<std::size_t>(startIndex + i)];
            if (binding.slider == nullptr || binding.label == nullptr)
            {
                continue;
            }

            binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);

            auto rowBounds = juce::Rectangle<int>(oscRight.getX(), rowY, oscRight.getWidth(), rowHeight);
            auto labelBounds = rowBounds.removeFromTop(labelHeight);
            rowBounds.removeFromTop(6);
            const auto knobBounds = juce::Rectangle<int>(knobSize, knobSize).withCentre(rowBounds.getCentre());

            setTextSizedLabel(binding.label, labelBounds, knobBounds.getCentreX());
            binding.slider->setBounds(knobBounds);

            rowY += rowHeight + minGap;
        }

        return;
    }

    auto area = groupArea.reduced(10, 8);
    area.removeFromTop(30);

    const auto knobTop = area.getY() + labelHeight + 10;
    const auto maxKnobByHeight = juce::jmax(56, area.getHeight() - labelHeight - 8);
    const auto maxKnobByWidth = (area.getWidth() - (knobCount - 1) * minGap) / knobCount;
    const auto baseKnobSize = juce::jlimit(56, 110, juce::jmin(maxKnobByWidth, maxKnobByHeight));
    const auto sizeScale = (startIndex == 5) ? 0.72 : 0.85;
    const auto knobSize = juce::jmax((startIndex == 5) ? 42 : 48,
                                     static_cast<int>(std::lround(static_cast<double>(baseKnobSize) * sizeScale)));

    const auto totalKnobWidth = knobSize * knobCount;
    const auto dynamicGap = knobCount > 1
                                ? juce::jmax(minGap, (area.getWidth() - totalKnobWidth) / (knobCount - 1))
                                : 0;
    const auto rowWidth = totalKnobWidth + (knobCount - 1) * dynamicGap;
    auto x = area.getX() + (area.getWidth() - rowWidth) / 2;

    for (int i = 0; i < knobCount; ++i)
    {
        auto& binding = knobBindings[static_cast<std::size_t>(startIndex + i)];

        if (binding.slider == nullptr || binding.label == nullptr)
        {
            continue;
        }

        binding.slider->setColour(juce::Slider::rotarySliderFillColourId, sectionAccent);
        binding.label->setBounds(x, area.getY(), knobSize, labelHeight);
        binding.slider->setBounds(x, knobTop, knobSize, knobSize);
        x += knobSize + dynamicGap;
    }
}

void SynthProjectAudioProcessorEditor::refreshAnyKeyDownState()
{
    const auto noteStates = audioProcessor.copyActiveNoteStates();
    const auto noteVelocities = audioProcessor.copyActiveNoteVelocities();
    const auto keyDown = std::any_of(noteStates.begin(), noteStates.end(), [](bool state) { return state; });
    anyKeyDown = keyDown;
    pianoKeyboard.setActiveNotes(noteStates, noteVelocities);
}

void SynthProjectAudioProcessorEditor::refreshGranularModeUI()
{
    const auto modeIndex = audioProcessor.getGranularModeParam().getIndex();
    if (granularModeBox.getSelectedItemIndex() != modeIndex)
    {
        granularModeBox.setSelectedItemIndex(modeIndex, juce::dontSendNotification);
    }

    if (modeIndex == lastGranularModeIndex)
    {
        return;
    }

    lastGranularModeIndex = modeIndex;

    switch (juce::jlimit(0, 3, modeIndex))
    {
        case 0: // CLASSIC
            delayTimeLabel.setText("TIME", juce::dontSendNotification);
            delayFeedbackLabel.setText("FEEDBACK", juce::dontSendNotification);
            granularSyncLabel.setText("SYNC", juce::dontSendNotification);
            break;
        case 1: // CLOUD
            delayTimeLabel.setText("SIZE", juce::dontSendNotification);
            delayFeedbackLabel.setText("DIFFUSE", juce::dontSendNotification);
            granularSyncLabel.setText("RATE", juce::dontSendNotification);
            break;
        case 2: // SHIMMER
            delayTimeLabel.setText("INTERVAL", juce::dontSendNotification);
            delayFeedbackLabel.setText("FEEDBACK", juce::dontSendNotification);
            granularSyncLabel.setText("RATE", juce::dontSendNotification);
            break;
        case 3: // RHYTHMIC
            delayTimeLabel.setText("SIZE", juce::dontSendNotification);
            delayFeedbackLabel.setText("SWING/FB", juce::dontSendNotification);
            granularSyncLabel.setText("RATE", juce::dontSendNotification);
            break;
        default:
            break;
    }

    repaint(isaacSectionArea);
}

void SynthProjectAudioProcessorEditor::refreshLfoAssignmentUI()
{
    const auto assignmentIndex = audioProcessor.getLfoAssignmentIndex();
    if (assignmentIndex == lastLfoAssignmentIndex)
    {
        return;
    }

    lastLfoAssignmentIndex = assignmentIndex;
    lfoAssignBox.setSelectedId(assignmentIndex + 1, juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshLfoFrequencyLabel()
{
    const auto hz = juce::jlimit(0.01f, 20.0f, audioProcessor.getLfoFrequencyParam().get());
    lfoFrequencyValueLabel.setText(juce::String(hz, 2) + " Hz", juce::dontSendNotification);
}

void SynthProjectAudioProcessorEditor::refreshEnvelopeGraphUI()
{
    if (envelopeGraph != nullptr)
    {
        envelopeGraph->refreshFromParameters();
    }
}

void SynthProjectAudioProcessorEditor::refreshFxBypassUI()
{
    const auto robEnabled = audioProcessor.getRobEnabledParam().get();
    const auto delayEnabled = audioProcessor.getDelayEnabledParam().get();
    const auto delayIsGranular = audioProcessor.getDelayAlgorithmParam().getIndex() == 0;
    const auto granularModeSelectable = delayEnabled && delayIsGranular;
    const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

    robBypassButton.setToggleState(robEnabled, juce::dontSendNotification);
    delayBypassButton.setToggleState(delayEnabled, juce::dontSendNotification);
    reverbBypassButton.setToggleState(reverbEnabled, juce::dontSendNotification);

    robWarmthKnob.setEnabled(robEnabled);
    robWarmthLabel.setEnabled(robEnabled);
    robWarmthKnob.getProperties().set("psychedelicBypassGray", !robEnabled);

    isaacTextureKnob.setEnabled(delayEnabled);
    isaacTextureLabel.setEnabled(delayEnabled);
    delayAlgoBox.setEnabled(delayEnabled);
    delayAlgoLabel.setEnabled(delayEnabled);
    delayTimeKnob.setEnabled(delayEnabled);
    delayTimeLabel.setEnabled(delayEnabled);
    delayFeedbackKnob.setEnabled(delayEnabled);
    delayFeedbackLabel.setEnabled(delayEnabled);
    granularSyncBox.setEnabled(delayEnabled);
    granularSyncLabel.setEnabled(delayEnabled);
    granularModeBox.setEnabled(granularModeSelectable);
    granularModeLabel.setEnabled(granularModeSelectable);
    isaacTextureKnob.getProperties().set("psychedelicBypassGray", !delayEnabled);
    delayTimeKnob.getProperties().set("psychedelicBypassGray", !delayEnabled);
    delayFeedbackKnob.getProperties().set("psychedelicBypassGray", !delayEnabled);

    reverbKnob.setEnabled(reverbEnabled);
    reverbLabel.setEnabled(reverbEnabled);
    reverbTypeBox.setEnabled(reverbEnabled);
    reverbTypeLabel.setEnabled(reverbEnabled);
    reverbKnob.getProperties().set("psychedelicBypassGray", !reverbEnabled);
}

void SynthProjectAudioProcessorEditor::timerCallback()
{
    // Timer drives non-audio UI synchronization only. DSP state is never
    // computed here; this keeps audio-thread responsibilities isolated.
    if (draggingFxSection < 0)
    {
        const auto processorOrder = audioProcessor.getFxProcessingOrder();
        if (processorOrder != fxSectionOrder)
        {
            fxSectionOrder = processorOrder;
            for (int stage = 0; stage < 3; ++stage)
            {
                const auto slot = indexForFxSection(stage);
                if (slot >= 0)
                {
                    fxSectionTargetAreas[static_cast<std::size_t>(stage)] =
                        fxSectionSlots[static_cast<std::size_t>(slot)].toFloat();
                    fxSectionCurrentAreas[static_cast<std::size_t>(stage)] =
                        fxSectionTargetAreas[static_cast<std::size_t>(stage)];
                }
            }
            layoutFxSectionsFromCurrentAreas();
            repaint(headerPlaceholderArea);
        }
    }

    animateFxSections();
    refreshOscillatorModeUI();
    refreshGranularModeUI();
    refreshLfoAssignmentUI();
    refreshLfoFrequencyLabel();
    refreshEnvelopeGraphUI();
    refreshFxBypassUI();

    if (debugPanelVisible)
    {
        // Throttle detached debug refresh to keep developer diagnostics useful
        // without making the main UI feel sluggish.
        if ((debugRefreshTickCounter++ % 4) == 0)
        {
            refreshDebugPanel(false);
        }
    }
    else
    {
        debugRefreshTickCounter = 0;
    }

    oscVizPhase += 0.09f;
    if (oscVizPhase > juce::MathConstants<float>::twoPi)
    {
        oscVizPhase -= juce::MathConstants<float>::twoPi;
    }
    repaint(knobGroupAreas[0].expanded(4));

    // MIDI status bar is temporarily disabled.
    const auto latestStatus = audioProcessor.copyMidiStatus();
    if (latestStatus.noteNumber != midiStatus.noteNumber
        || latestStatus.velocity != midiStatus.velocity
        || latestStatus.noteOn != midiStatus.noteOn)
    {
        midiStatus = latestStatus;
        if (midiStatus.noteOn)
        {
            const auto velNorm = juce::jlimit(0.0f, 1.0f, static_cast<float>(midiStatus.velocity) / 127.0f);
            logoVibrationIntensity = juce::jmax(logoVibrationIntensity, velNorm);
        }
    }

    refreshAnyKeyDownState();

    performanceControls.setControllerState(audioProcessor.copyPitchBendNormalized(),
                                           audioProcessor.copyModWheelNormalized(),
                                           audioProcessor.copyPitchBendActivity(),
                                           audioProcessor.copyModWheelActivity());

    if (++dirtyUpdateCounter >= 5)
    {
        dirtyUpdateCounter = 0;
        updatePresetDirtyState();
    }

    if (presetBrowserVisible)
    {
        presetBrowserPanel.setAlwaysOnTop(true);
        presetBrowserPanel.toFront(false);
    }

    if (logoVibrationIntensity > 0.001f || anyKeyDown)
    {
        logoVibrationPhase += 0.38f;

        if (logoVibrationPhase > juce::MathConstants<float>::twoPi)
        {
            logoVibrationPhase -= juce::MathConstants<float>::twoPi;
        }

        const auto decay = anyKeyDown ? 0.968f : 0.928f;
        logoVibrationIntensity *= decay;

        repaint();
    }
}
