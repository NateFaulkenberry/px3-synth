#include "PluginEditor.h"
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

#if JUCE_MAC
#include <mach/mach.h>
#endif

namespace
{
const std::array<juce::Colour, 4> kGroupAccents {
    juce::Colour::fromRGB(74, 153, 255),   // OSC: blue
    juce::Colour::fromRGB(255, 88, 88),    // FILTER: red
    juce::Colour::fromRGB(73, 222, 121),   // AMP ENV: green
    juce::Colour::fromRGB(186, 112, 255)   // LFO: purple
};

constexpr int kFxSectionDrive = 0;
constexpr int kFxSectionDelay = 1;
constexpr int kFxSectionReverb = 2;
constexpr int kFxSectionMood = 3;

constexpr int kSectionOsc = 0;
constexpr int kSectionMod = 1;
constexpr int kSectionAmp = 2;
constexpr int kSectionFilter = 3;
constexpr int kSectionFx = 4;
constexpr int kSectionMix = 5;

juce::String moduleIdFromSectionId(int sectionId)
{
    switch (sectionId)
    {
        case kFxSectionDelay:
            return "delay";
        case kFxSectionMood:
            return "mood";
        case kFxSectionReverb:
            return "reverb";
        case kFxSectionDrive:
        default:
            return "harmonicDrive";
    }
}

void enableLabelHoverOverlay(juce::Label& label, const juce::String& tooltipText = {})
{
    label.setInterceptsMouseClicks(true, false);
    const auto text = tooltipText.isNotEmpty() ? tooltipText : label.getText();
    if (text.isNotEmpty())
    {
        label.setTooltip(text);
    }
}

#if PX3_DEBUG_PANEL
// Resident size of the WHOLE process - the DAW, every other plug-in it has
// loaded, and every sample in its memory. There is no per-instance figure
// available here and there cannot be: a plug-in shares its host's address
// space, and nothing in it distinguishes one instance's pages from another's.
//
// It used to be divided by the number of PX3 instances and labelled RAM, which
// made it arithmetic rather than measurement: with a single instance open it
// reported the entirety of Logic as PX3's footprint, which is why it never
// resembled what the memory tests report. Run PX3Mem for the real per-instance
// figure - that one counts allocations rather than dividing a total.
//
// It is still worth showing undivided. The absolute number means little, but
// watching it climb while nothing is being played is how a leak announces
// itself, and that is what this readout is for.
double processResidentMemoryMb()
{
#if JUCE_MAC
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    const auto status = task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count);
    if (status == KERN_SUCCESS)
    {
        return static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
    }
#endif
    return 0.0;
}
#endif

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
}


juce::String PX3SynthAudioProcessorEditor::fxModuleIdFromSection(int sectionId)
{
    return moduleIdFromSectionId(sectionId);
}

void PX3SynthAudioProcessorEditor::configureKnob(KnobBinding& binding,
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

void PX3SynthAudioProcessorEditor::configureEffectKnob(juce::Slider& slider,
                                                       juce::AudioParameterFloat& parameter)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    const auto& range = parameter.getNormalisableRange();
    slider.setRange(range.start, range.end);

    slider.setLookAndFeel(&knobLookAndFeel);

    addAndMakeVisible(slider);
}

void PX3SynthAudioProcessorEditor::configureEffectKnob(juce::Slider& slider,
                                                           KnobLabel& label,
                                                           const juce::String& labelText,
                                                           juce::AudioParameterFloat& parameter)
{
    configureEffectKnob(slider, parameter);

    label.setText(labelText, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    label.setFont(juce::FontOptions(11.5f));
    label.setInterceptsMouseClicks(true, false);
    label.setTooltip(labelText);

    addAndMakeVisible(label);
}

void PX3SynthAudioProcessorEditor::attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider)
{
    sliderAttachments.push_back(std::make_unique<juce::SliderParameterAttachment>(parameter, slider, nullptr));
}

void PX3SynthAudioProcessorEditor::attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox)
{
    comboBoxAttachments.push_back(std::make_unique<juce::ComboBoxParameterAttachment>(parameter, comboBox, nullptr));
}

void PX3SynthAudioProcessorEditor::attachButton(juce::RangedAudioParameter& parameter, juce::Button& button)
{
    buttonAttachments.push_back(std::make_unique<juce::ButtonParameterAttachment>(parameter, button, nullptr));
}

PX3SynthAudioProcessorEditor::PX3SynthAudioProcessorEditor(PX3SynthAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    tooltipWindow(this, 450),
    presetManager(p)
{
    uiConfigManager.setConfigFile(resolveUiConfigFile());
    loadUiConfig(true);

    backgroundImage = juce::ImageFileFormat::loadFrom(BinaryData::ppp_png, BinaryData::ppp_pngSize);
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

    addAndMakeVisible(performanceControls);
    addAndMakeVisible(pianoKeyboard);
    addAndMakeVisible(sparkOverlay);

    // Each animator asks the overlay to repaint; neither draws its own
    // particles any more.
    pianoKeyboard.onSparksChanged = [this]() { sparkOverlay.repaintParticles(); };
    performanceControls.onSparklesChanged = [this]() { sparkOverlay.repaintParticles(); };

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
        KnobBinding { &osc1MacroAKnob, &osc1MacroALabel, nullptr },
        KnobBinding { &osc1MacroBKnob, &osc1MacroBLabel, nullptr },
        KnobBinding { &osc1MacroCKnob, &osc1MacroCLabel, nullptr },
        KnobBinding { &osc2MacroAKnob, &osc2MacroALabel, nullptr },
        KnobBinding { &osc2MacroBKnob, &osc2MacroBLabel, nullptr },
        KnobBinding { &osc2MacroCKnob, &osc2MacroCLabel, nullptr },
        KnobBinding { &osc3MacroAKnob, &osc3MacroALabel, nullptr },
        KnobBinding { &osc3MacroBKnob, &osc3MacroBLabel, nullptr },
        KnobBinding { &osc3MacroCKnob, &osc3MacroCLabel, nullptr },
        KnobBinding { &osc1PitchKnob, &osc1PitchLabel, nullptr },
        KnobBinding { &osc2PitchKnob, &osc2PitchLabel, nullptr },
        KnobBinding { &osc3PitchKnob, &osc3PitchLabel, nullptr },
        KnobBinding { &subOscPitchKnob, &subOscPitchLabel, nullptr },
        KnobBinding { &cutoffKnob, &cutoffLabel, nullptr },
        KnobBinding { &resonanceKnob, &resonanceLabel, nullptr },
        KnobBinding { &cutoff2Knob, &cutoff2Label, nullptr },
        KnobBinding { &resonance2Knob, &resonance2Label, nullptr },
        KnobBinding { &attackKnob, &attackLabel, nullptr },
        KnobBinding { &decayKnob, &decayLabel, nullptr },
        KnobBinding { &sustainKnob, &sustainLabel, nullptr },
        KnobBinding { &releaseKnob, &releaseLabel, nullptr },
        KnobBinding { &lfoFrequencyKnob, &lfoFrequencyLabel, nullptr },
        KnobBinding { &lfoAmountKnob, &lfoAmountLabel, nullptr },
        KnobBinding { &gainKnob, &gainLabel, nullptr }
    };

    configureKnob(knobBindings[0], "PARAM A", audioProcessor.getOscillatorMacroAParam(0));
    configureKnob(knobBindings[1], "PARAM B", audioProcessor.getOscillatorMacroBParam(0));
    configureKnob(knobBindings[2], "PARAM C", audioProcessor.getOscillatorMacroCParam(0));
    configureKnob(knobBindings[3], "PARAM A", audioProcessor.getOscillatorMacroAParam(1));
    configureKnob(knobBindings[4], "PARAM B", audioProcessor.getOscillatorMacroBParam(1));
    configureKnob(knobBindings[5], "PARAM C", audioProcessor.getOscillatorMacroCParam(1));
    configureKnob(knobBindings[6], "PARAM A", audioProcessor.getOscillatorMacroAParam(2));
    configureKnob(knobBindings[7], "PARAM B", audioProcessor.getOscillatorMacroBParam(2));
    configureKnob(knobBindings[8], "PARAM C", audioProcessor.getOscillatorMacroCParam(2));
    configureKnob(knobBindings[9], "TUNE", audioProcessor.getOscillatorPitchParam(0));
    configureKnob(knobBindings[10], "TUNE", audioProcessor.getOscillatorPitchParam(1));
    configureKnob(knobBindings[11], "TUNE", audioProcessor.getOscillatorPitchParam(2));
    configureKnob(knobBindings[12], "TUNE", audioProcessor.getSubOscPitchParam());
    configureKnob(knobBindings[13], "CUTOFF", audioProcessor.getFilterCutoffParam(0));
    configureKnob(knobBindings[14], "RESONANCE", audioProcessor.getFilterResonanceParam(0));
    configureKnob(knobBindings[15], "CUTOFF", audioProcessor.getFilterCutoffParam(1));
    configureKnob(knobBindings[16], "RESONANCE", audioProcessor.getFilterResonanceParam(1));
    configureKnob(knobBindings[17], "Attack", audioProcessor.getAttackParam());
    configureKnob(knobBindings[18], "Decay", audioProcessor.getDecayParam());
    configureKnob(knobBindings[19], "Sustain", audioProcessor.getSustainParam());
    configureKnob(knobBindings[20], "Release", audioProcessor.getReleaseParam());
    configureKnob(knobBindings[21], "RATE", audioProcessor.getLfoFrequencyParam());
    configureKnob(knobBindings[22], "AMOUNT", audioProcessor.getLfoAmountParam());
    // Amount runs -100%..+100%, so zero is dead centre and worth a detent. The
    // extremes are not: unlike pan, full depth is not a position people aim at.
    lfoAmountKnob.setCentreDetent(0.06);
    lfoAmountKnob.setExtremeDetent(0.0);
    // ---- comb mode -------------------------------------------------------
    // Configured directly rather than through knobBindings: the bindings array
    // is indexed by hand at every call site, so growing it by twelve would mean
    // renumbering every entry after it.
    for (int filterIndex = 0; filterIndex < kFilterInstanceCount; ++filterIndex)
    {
        const auto slot = static_cast<std::size_t>(filterIndex);

        const auto setupCombKnob = [this](juce::Slider& knob,
                                          KnobLabel& label,
                                          const juce::String& text,
                                          juce::AudioParameterFloat& parameter)
        {
            knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            const auto& range = parameter.getNormalisableRange();
            knob.setRange(range.start, range.end);
            knob.setLookAndFeel(&knobLookAndFeel);
            knob.setTooltip(text);

            label.setText(text, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
            label.setFont(juce::FontOptions(11.5f));
            label.setInterceptsMouseClicks(false, false);

            attachSlider(parameter, knob);
        };

        setupCombKnob(combTuneKnobs[slot], combTuneLabels[slot], "TUNE",
                      audioProcessor.getFilterCombTuneParam(filterIndex));
        setupCombKnob(combDecayKnobs[slot], combDecayLabels[slot], "DECAY",
                      audioProcessor.getFilterCombDecayParam(filterIndex));
        setupCombKnob(combDampingKnobs[slot], combDampingLabels[slot], "DAMP",
                      audioProcessor.getFilterCombDampingParam(filterIndex));
        setupCombKnob(combDispersionKnobs[slot], combDispersionLabels[slot], "DISPERSE",
                      audioProcessor.getFilterCombDispersionParam(filterIndex));
        setupCombKnob(combDriveKnobs[slot], combDriveLabels[slot], "DRIVE",
                      audioProcessor.getFilterCombDriveParam(filterIndex));
        setupCombKnob(combMixKnobs[slot], combMixLabels[slot], "MIX",
                      audioProcessor.getFilterCombMixParam(filterIndex));

        // Polarity is a two-state choice, so it is a switch rather than a knob
        // - the same chip Mood's Freeze uses.
        auto& invert = combInvertButtons[slot];
        invert.setButtonText("PHASE +");
        invert.setStateLabels("PHASE -", "PHASE +");
        invert.setTooltip("Invert the resonator's feedback polarity");
        attachButton(audioProcessor.getFilterCombInvertParam(filterIndex), invert);
    }

    configureKnob(knobBindings[23], "MASTER", audioProcessor.getMasterGainParam());
    // The caption is gone from the layout, so the name lives on the knob.
    gainKnob.setTooltip("Master gain");
    gainLabel.setVisible(false);

    const auto formatPitchCents = [](double semitoneValue)
    {
        const auto cents = static_cast<int>(std::lround(semitoneValue * 100.0));
        if (cents > 0)
        {
            return "+" + juce::String(cents) + " c";
        }
        return juce::String(cents) + " c";
    };

    const auto configurePitchReadout = [&](PanKnob& pitchKnob, juce::Label& valueLabel)
    {
        pitchKnob.getProperties().set("isMixerPanKnob", true);
        pitchKnob.setRange(-0.24, 0.24, 0.01);

        // The same detents the mixer pan knobs have, scaled to this knob's much
        // narrower range: dead centre is the value people want most, and the
        // two extremes are edges worth landing on exactly.
        constexpr double pitchSpan = 0.48;
        pitchKnob.setCentreDetent(pitchSpan * 0.07);
        pitchKnob.setExtremeDetent(pitchSpan * 0.03);

        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 214, 224));
        valueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        valueLabel.setFont(juce::FontOptions(11.0f));
        valueLabel.setInterceptsMouseClicks(false, false);

        pitchKnob.onValueChange = [&pitchKnob, &valueLabel, formatPitchCents]()
        {
            valueLabel.setText(formatPitchCents(pitchKnob.getValue()), juce::dontSendNotification);
        };

        valueLabel.setText(formatPitchCents(pitchKnob.getValue()), juce::dontSendNotification);
    };

    // Macro readouts. The macros are 0..1 normalised, so they read as a
    // percentage - the same way the LFO and ENV amount knobs already do.
    const auto configureMacroReadout = [](juce::Slider& knob, juce::Label& valueLabel)
    {
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 214, 224));
        valueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        valueLabel.setFont(juce::FontOptions(11.0f));
        valueLabel.setInterceptsMouseClicks(false, false);

        const auto refresh = [&knob, &valueLabel]()
        {
            const auto percent = juce::roundToInt(juce::jlimit(0.0, 1.0, knob.getValue()) * 100.0);
            valueLabel.setText(juce::String(percent) + "%", juce::dontSendNotification);
        };
        knob.onValueChange = refresh;
        refresh();
    };

    configureMacroReadout(osc1MacroAKnob, osc1MacroAValueLabel);
    configureMacroReadout(osc1MacroBKnob, osc1MacroBValueLabel);
    configureMacroReadout(osc1MacroCKnob, osc1MacroCValueLabel);
    configureMacroReadout(osc2MacroAKnob, osc2MacroAValueLabel);
    configureMacroReadout(osc2MacroBKnob, osc2MacroBValueLabel);
    configureMacroReadout(osc2MacroCKnob, osc2MacroCValueLabel);
    configureMacroReadout(osc3MacroAKnob, osc3MacroAValueLabel);
    configureMacroReadout(osc3MacroBKnob, osc3MacroBValueLabel);
    configureMacroReadout(osc3MacroCKnob, osc3MacroCValueLabel);

    configurePitchReadout(osc1PitchKnob, osc1PitchValueLabel);
    configurePitchReadout(osc2PitchKnob, osc2PitchValueLabel);
    configurePitchReadout(osc3PitchKnob, osc3PitchValueLabel);
    configurePitchReadout(subOscPitchKnob, subOscPitchValueLabel);

    // ADSR graph replaces visible envelope knobs; parameter attachments remain unchanged.
    attackKnob.setVisible(false);
    decayKnob.setVisible(false);
    sustainKnob.setVisible(false);
    releaseKnob.setVisible(false);
    attackLabel.setVisible(false);
    decayLabel.setVisible(false);
    sustainLabel.setVisible(false);
    releaseLabel.setVisible(false);

    lfoFrequencyValueLabel.setJustificationType(juce::Justification::centred);
    lfoFrequencyValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    lfoFrequencyValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lfoFrequencyValueLabel.setFont(juce::FontOptions(11.0f));
    lfoFrequencyValueLabel.setInterceptsMouseClicks(false, false);

    lfoAmountValueLabel.setJustificationType(juce::Justification::centred);
    lfoAmountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    lfoAmountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lfoAmountValueLabel.setFont(juce::FontOptions(11.0f));
    lfoAmountValueLabel.setInterceptsMouseClicks(false, false);

    envAmountValueLabel.setJustificationType(juce::Justification::centred);
    envAmountValueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(218, 218, 228));
    envAmountValueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    envAmountValueLabel.setFont(juce::FontOptions(11.0f));
    envAmountValueLabel.setInterceptsMouseClicks(false, false);

    lfoFrequencyKnob.onValueChange = [this]()
    {
        refreshLfoFrequencyLabel();
    };

    lfoAmountKnob.onValueChange = [this]()
    {
        const auto amount = juce::jlimit(-1.0f, 1.0f, static_cast<float>(lfoAmountKnob.getValue()));
        const auto amountPercent = static_cast<int>(std::lround(amount * 100.0f));
        const auto prefix = amountPercent > 0 ? juce::String("+") : juce::String();
        lfoAmountValueLabel.setText(prefix + juce::String(amountPercent) + "%", juce::dontSendNotification);
    };

    lfoAmountLabel.setText("AMOUNT", juce::dontSendNotification);
    lfoAmountLabel.setJustificationType(juce::Justification::centred);
    lfoAmountLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoAmountLabel.setFont(juce::FontOptions(11.0f));
    lfoAmountLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lfoAmountLabel.setInterceptsMouseClicks(false, false);

    lfoAmountKnob.onValueChange();

    auto& lfoWaveformParam = audioProcessor.getLfoWaveformParam();
    for (int i = 0; i < lfoWaveformParam.choices.size(); ++i)
    {
        lfoWaveformBox.addItem(lfoWaveformParam.choices[i], i + 1);
    }
    lfoWaveformBox.setSelectedItemIndex(lfoWaveformParam.getIndex(), juce::dontSendNotification);
    lfoWaveformBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    lfoWaveformBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoWaveformBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    lfoWaveformLabel.setText("WAVE", juce::dontSendNotification);
    lfoWaveformLabel.setJustificationType(juce::Justification::centredLeft);
    lfoWaveformLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoWaveformLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(lfoWaveformLabel, "Waveform");

    lfoAssignLabel.setText("ASSIGN", juce::dontSendNotification);
    lfoAssignLabel.setJustificationType(juce::Justification::centred);
    lfoAssignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoAssignLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(lfoAssignLabel, "LFO Assignment");

    envAssignLabel.setText("ASSIGN", juce::dontSendNotification);
    envAssignLabel.setJustificationType(juce::Justification::centred);
    envAssignLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    envAssignLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(envAssignLabel, "Envelope Assignment");

    envBypassButton.setButtonText("");
    envBypassButton.setClickingTogglesState(true);
    envBypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    envBypassButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    lfoBypassButton.setButtonText("");
    lfoBypassButton.setClickingTogglesState(true);
    lfoBypassButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    lfoBypassButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));
    lfoAssignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    lfoAssignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    lfoAssignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    envAssignBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    envAssignBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    envAssignBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

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

    auto& subOscOctaveParam = audioProcessor.getSubOscOctaveParam();
    for (int i = 0; i < subOscOctaveParam.choices.size(); ++i)
    {
        subOscOctaveBox.addItem(subOscOctaveParam.choices[i], i + 1);
    }
    subOscOctaveBox.setSelectedItemIndex(subOscOctaveParam.getIndex(), juce::dontSendNotification);
    subOscOctaveBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    subOscOctaveBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    subOscOctaveBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    subOscOctaveLabel.setText("OCT", juce::dontSendNotification);
    subOscOctaveLabel.setJustificationType(juce::Justification::centredLeft);
    subOscOctaveLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    subOscOctaveLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(subOscOctaveLabel, "Octave");

    auto& subOscWaveformParam = audioProcessor.getSubOscWaveformParam();
    for (int i = 0; i < subOscWaveformParam.choices.size(); ++i)
    {
        subOscWaveformBox.addItem(subOscWaveformParam.choices[i], i + 1);
    }
    subOscWaveformBox.setSelectedItemIndex(subOscWaveformParam.getIndex(), juce::dontSendNotification);
    subOscWaveformBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    subOscWaveformBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    subOscWaveformBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    subOscWaveformLabel.setText("WAVE", juce::dontSendNotification);
    subOscWaveformLabel.setJustificationType(juce::Justification::centredLeft);
    subOscWaveformLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    subOscWaveformLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(subOscWaveformLabel, "Waveform");

    subOscEnabledButton.setButtonText("");
    subOscEnabledButton.setClickingTogglesState(true);
    subOscEnabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    subOscEnabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    osc1EnabledButton.setClickingTogglesState(true);
    osc2EnabledButton.setClickingTogglesState(true);
    osc3EnabledButton.setClickingTogglesState(true);
    // The filter type dropdown had no caption; the other dropdowns in the
    // plugin all name themselves.
    const auto configureDropdownLabel = [](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        label.setFont(juce::FontOptions(11.5f));
        label.setInterceptsMouseClicks(false, false);
    };
    configureDropdownLabel(filter1TypeLabel, "TYPE");
    configureDropdownLabel(filter2TypeLabel, "TYPE");
    filter1EnabledButton.setClickingTogglesState(true);
    filter2EnabledButton.setClickingTogglesState(true);

    filter1EnabledButton.onClick = [this]()
    {
        refreshFilterUI();
    };
    filter2EnabledButton.onClick = [this]()
    {
        refreshFilterUI();
    };

    // OSC macro labels can become long in some modes; use a slightly smaller font.
    osc1MacroALabel.setFont(juce::FontOptions(11.0f));
    osc1MacroBLabel.setFont(juce::FontOptions(11.0f));
    osc1MacroCLabel.setFont(juce::FontOptions(11.0f));
    osc2MacroALabel.setFont(juce::FontOptions(11.0f));
    osc2MacroBLabel.setFont(juce::FontOptions(11.0f));
    osc2MacroCLabel.setFont(juce::FontOptions(11.0f));
    osc3MacroALabel.setFont(juce::FontOptions(11.0f));
    osc3MacroBLabel.setFont(juce::FontOptions(11.0f));
    osc3MacroCLabel.setFont(juce::FontOptions(11.0f));
    osc1PitchLabel.setFont(juce::FontOptions(11.0f));
    osc2PitchLabel.setFont(juce::FontOptions(11.0f));
    osc3PitchLabel.setFont(juce::FontOptions(11.0f));
    subOscPitchLabel.setFont(juce::FontOptions(11.0f));

    configureEffectKnob(vibeAmountKnob, vibeAmountLabel, "AMOUNT", audioProcessor.getVibeAmountParam());
    configureEffectKnob(isaacTextureKnob, isaacTextureLabel, "AMOUNT", audioProcessor.getDelayAmountParam());
    configureEffectKnob(delayTimeKnob, delayTimeLabel, "TIME", audioProcessor.getDelayTimeParam());
    configureEffectKnob(delayFeedbackKnob, delayFeedbackLabel, "FEEDBACK", audioProcessor.getDelayFeedbackParam());
    configureEffectKnob(reverbKnob, reverbLabel, "AMOUNT", audioProcessor.getReverbAmountParam());
    configureEffectKnob(moodMixKnob, moodMixLabel, "MIX", audioProcessor.getMoodMixParam());
    configureEffectKnob(moodClockKnob, moodClockLabel, "CLOCK", audioProcessor.getMoodClockParam());
    configureEffectKnob(moodWetTimeKnob, moodWetTimeLabel, "WET TIME", audioProcessor.getMoodWetTimeParam());
    configureEffectKnob(moodWetModifyKnob, moodWetModifyLabel, "WET MOD", audioProcessor.getMoodWetModifyParam());
    configureEffectKnob(moodLoopLengthKnob, moodLoopLengthLabel, "LOOP LEN", audioProcessor.getMoodLoopLengthParam());
    configureEffectKnob(moodLoopModifyKnob, moodLoopModifyLabel, "LOOP MOD", audioProcessor.getMoodLoopModifyParam());
    configureEffectKnob(moodFeedbackKnob, moodFeedbackLabel, "FEEDBACK", audioProcessor.getMoodFeedbackParam());
    configureEffectKnob(moodSpreadKnob, moodSpreadLabel, "SPREAD", audioProcessor.getMoodSpreadParam());
    configureEffectKnob(moodDegradeKnob, moodDegradeLabel, "DEGRADE", audioProcessor.getMoodDegradeParam());


    // Compact labels and tooltips keep delay controls readable in narrow layouts.
    isaacTextureLabel.getProperties().set("compactLabel", true);
    delayTimeLabel.getProperties().set("compactLabel", true);
    delayFeedbackLabel.getProperties().set("compactLabel", true);
    delayFeedbackLabel.setTooltip("FEEDBACK");
    delayFeedbackKnob.setTooltip("FEEDBACK");

    vibeAmountKnob.getProperties().set("psychedelicFx", true);
    isaacTextureKnob.getProperties().set("psychedelicFx", true);
    reverbKnob.getProperties().set("psychedelicFx", true);

    auto& vibeTypeParam = audioProcessor.getVibeTypeParam();
    for (int i = 0; i < vibeTypeParam.choices.size(); ++i)
    {
        vibeTypeBox.addItem(vibeTypeParam.choices[i], i + 1);
    }
    vibeTypeBox.setSelectedItemIndex(vibeTypeParam.getIndex(), juce::dontSendNotification);
    vibeTypeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    vibeTypeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    vibeTypeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    vibeTypeLabel.setText("TYPE", juce::dontSendNotification);
    vibeTypeLabel.setJustificationType(juce::Justification::centred);
    vibeTypeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    vibeTypeLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(vibeTypeLabel, "Type");

    const auto configureFilterSelector = [this](int filterIndex, juce::ComboBox& filterBox)
    {
        auto& filterTypeParam = audioProcessor.getFilterTypeParam(filterIndex);
        const auto filterChoiceCount = filterTypeParam.choices.size();
        for (int i = 0; i < filterChoiceCount; ++i)
        {
            filterBox.addItem(filterTypeParam.choices[i], i + 1);
        }
        filterBox.setSelectedItemIndex(filterTypeParam.getIndex(), juce::dontSendNotification);
        filterBox.onChange = [this]()
        {
            refreshFilterUI();
        };
        filterBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
        filterBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
        filterBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    };

    configureFilterSelector(0, filterTypeBox);
    configureFilterSelector(1, filter2TypeBox);

    auto configureOscSelector = [this](int oscIndex, juce::ComboBox& modeBox, KnobLabel& modeLabel, juce::ComboBox& vowelBox, KnobLabel& vowelLabel)
    {
        auto& oscModeParam = audioProcessor.getOscillatorModeParam(oscIndex);
        for (int i = 0; i < oscModeParam.choices.size(); ++i)
        {
            modeBox.addItem(oscModeParam.choices[i], i + 1);
        }
        modeBox.setSelectedItemIndex(oscModeParam.getIndex(), juce::dontSendNotification);
        modeBox.onChange = [this]()
        {
            refreshOscillatorModeUI();
        };
        modeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
        modeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
        modeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

        modeLabel.setText("MODE", juce::dontSendNotification);
        modeLabel.setJustificationType(juce::Justification::centred);
        modeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        modeLabel.setFont(juce::FontOptions(11.5f));
        enableLabelHoverOverlay(modeLabel, "Mode");

        auto& oscVowelParam = audioProcessor.getOscillatorVowelParam(oscIndex);
        for (int i = 0; i < oscVowelParam.choices.size(); ++i)
        {
            vowelBox.addItem(oscVowelParam.choices[i], i + 1);
        }
        vowelBox.setSelectedItemIndex(oscVowelParam.getIndex(), juce::dontSendNotification);
        vowelBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
        vowelBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
        vowelBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

        vowelLabel.setText("VOWEL", juce::dontSendNotification);
        vowelLabel.setJustificationType(juce::Justification::centred);
        vowelLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        vowelLabel.setFont(juce::FontOptions(11.5f));
        enableLabelHoverOverlay(vowelLabel, "Vowel");
    };

    configureOscSelector(0, osc1ModeBox, osc1ModeLabel, osc1VowelBox, osc1VowelLabel);
    configureOscSelector(1, osc2ModeBox, osc2ModeLabel, osc2VowelBox, osc2VowelLabel);
    configureOscSelector(2, osc3ModeBox, osc3ModeLabel, osc3VowelBox, osc3VowelLabel);

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
    delayAlgoLabel.setText("TYPE", juce::dontSendNotification);
    delayAlgoLabel.setJustificationType(juce::Justification::centred);
    delayAlgoLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    delayAlgoLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(delayAlgoLabel, "Algorithm");

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
    enableLabelHoverOverlay(granularSyncLabel, "Sync Division");

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
    enableLabelHoverOverlay(granularModeLabel, "Granular Mode");

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
    reverbTypeLabel.setText("TYPE", juce::dontSendNotification);
    reverbTypeLabel.setJustificationType(juce::Justification::centred);
    reverbTypeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    reverbTypeLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(reverbTypeLabel, "Algorithm");

    auto& moodRoutingParam = audioProcessor.getMoodRoutingParam();
    for (int i = 0; i < moodRoutingParam.choices.size(); ++i)
    {
        moodRoutingBox.addItem(moodRoutingParam.choices[i], i + 1);
    }
    moodRoutingBox.setSelectedItemIndex(moodRoutingParam.getIndex(), juce::dontSendNotification);
    moodRoutingBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    moodRoutingBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodRoutingBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    moodRoutingLabel.setText("ROUTE", juce::dontSendNotification);
    moodRoutingLabel.setJustificationType(juce::Justification::centred);
    moodRoutingLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodRoutingLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(moodRoutingLabel, "Routing");

    auto& moodWetModeParam = audioProcessor.getMoodWetModeParam();
    for (int i = 0; i < moodWetModeParam.choices.size(); ++i)
    {
        moodWetModeBox.addItem(moodWetModeParam.choices[i], i + 1);
    }
    moodWetModeBox.setSelectedItemIndex(moodWetModeParam.getIndex(), juce::dontSendNotification);
    moodWetModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    moodWetModeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodWetModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    moodWetModeLabel.setText("WET", juce::dontSendNotification);
    moodWetModeLabel.setJustificationType(juce::Justification::centred);
    moodWetModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodWetModeLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(moodWetModeLabel, "Wet Mode");

    auto& moodLoopModeParam = audioProcessor.getMoodLoopModeParam();
    for (int i = 0; i < moodLoopModeParam.choices.size(); ++i)
    {
        moodLoopModeBox.addItem(moodLoopModeParam.choices[i], i + 1);
    }
    moodLoopModeBox.setSelectedItemIndex(moodLoopModeParam.getIndex(), juce::dontSendNotification);
    moodLoopModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
    moodLoopModeBox.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodLoopModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));
    moodLoopModeLabel.setText("LOOP", juce::dontSendNotification);
    moodLoopModeLabel.setJustificationType(juce::Justification::centred);
    moodLoopModeLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    moodLoopModeLabel.setFont(juce::FontOptions(11.5f));
    enableLabelHoverOverlay(moodLoopModeLabel, "Loop Mode");

    // Freeze is not a power toggle: it is a labelled chip that carries its own
    // text and shows its state by filling.
    moodFreezeButton.setButtonText("FREEZE OFF");
    moodFreezeButton.setStateLabels("FREEZE ON", "FREEZE OFF");
    moodFreezeButton.setTooltip("Freeze the Mood loop");

    // Only the section name here - the hover text needs it. Each card tints its
    // own button from its own identity colour, so there is one source for that
    // and it survives a UIConfig reload.
    const auto namePower = [](px3::ui::BypassButton& button, const juce::String& section)
    {
        button.setSectionName(section);
    };

    namePower(subOscEnabledButton, "Sub Osc");
    namePower(osc1EnabledButton, "Osc 1");
    namePower(osc2EnabledButton, "Osc 2");
    namePower(osc3EnabledButton, "Osc 3");
    namePower(filter1EnabledButton, "Filter 1");
    namePower(filter2EnabledButton, "Filter 2");
    namePower(lfoBypassButton, "LFO 1");
    namePower(envBypassButton, "ENV 1");
    namePower(robBypassButton, "Vibe");
    namePower(delayBypassButton, "Delay");
    namePower(moodBypassButton, "Mood");
    namePower(reverbBypassButton, "Reverb");

    oscPanel = std::make_unique<OscPanel>(subOscEnabledButton,
                                          subOscPitchKnob,
                                          subOscPitchLabel,
                                          subOscPitchValueLabel,
                                          subOscOctaveBox,
                                          subOscOctaveLabel,
                                          subOscWaveformBox,
                                          subOscWaveformLabel,
                                          osc1PitchKnob,
                                          osc1PitchLabel,
                                          osc1PitchValueLabel,
                                          osc1MacroAKnob,
                                          osc1MacroBKnob,
                                          osc1MacroCKnob,
                                          osc1EnabledButton,
                                          osc1MacroALabel,
                                          osc1MacroBLabel,
                                          osc1MacroCLabel,
                                          osc1MacroAValueLabel,
                                          osc1MacroBValueLabel,
                                          osc1MacroCValueLabel,
                                          osc1ModeBox,
                                          osc1ModeLabel,
                                          osc1VowelBox,
                                          osc1VowelLabel,
                                          osc2PitchKnob,
                                          osc2PitchLabel,
                                          osc2PitchValueLabel,
                                          osc2MacroAKnob,
                                          osc2MacroBKnob,
                                          osc2MacroCKnob,
                                          osc2EnabledButton,
                                          osc2MacroALabel,
                                          osc2MacroBLabel,
                                          osc2MacroCLabel,
                                          osc2MacroAValueLabel,
                                          osc2MacroBValueLabel,
                                          osc2MacroCValueLabel,
                                          osc2ModeBox,
                                          osc2ModeLabel,
                                          osc2VowelBox,
                                          osc2VowelLabel,
                                          osc3PitchKnob,
                                          osc3PitchLabel,
                                          osc3PitchValueLabel,
                                          osc3MacroAKnob,
                                          osc3MacroBKnob,
                                          osc3MacroCKnob,
                                          osc3EnabledButton,
                                          osc3MacroALabel,
                                          osc3MacroBLabel,
                                          osc3MacroCLabel,
                                          osc3MacroAValueLabel,
                                          osc3MacroBValueLabel,
                                          osc3MacroCValueLabel,
                                          osc3ModeBox,
                                          osc3ModeLabel,
                                          osc3VowelBox,
                                          osc3VowelLabel,
                                          juce::Colour::fromRGB(120, 180, 255),
                                          kGroupAccents[0]);
    modPanel = std::make_unique<ModPanel>(audioProcessor,
                                          lfoBypassButton,
                                          lfoAssignLabel,
                                          lfoAssignBox,
                                          lfoFrequencyKnob,
                                          lfoFrequencyLabel,
                                          lfoFrequencyValueLabel,
                                          lfoAmountKnob,
                                          lfoAmountLabel,
                                          lfoAmountValueLabel,
                                          lfoWaveformBox,
                                          lfoWaveformLabel,
                                          &knobLookAndFeel,
                                          kGroupAccents[2],
                                          kGroupAccents[3]);
    // After the oscillator panel exists, since the controls are handed to the
    // cards it owns.
    configureWavetableControls();

    ampPanel = std::make_unique<AmpPanel>(audioProcessor, kGroupAccents[2]);
    fltPanel = std::make_unique<FltPanel>(std::array<juce::ToggleButton*, kFilterInstanceCount> { { &filter1EnabledButton, &filter2EnabledButton } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &cutoffKnob, &cutoff2Knob } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &cutoffLabel, &cutoff2Label } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &resonanceKnob, &resonance2Knob } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &resonanceLabel, &resonance2Label } },
                                          std::array<juce::ComboBox*, kFilterInstanceCount> { { &filterTypeBox, &filter2TypeBox } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &filter1TypeLabel, &filter2TypeLabel } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combTuneKnobs[0], &combTuneKnobs[1] } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combDecayKnobs[0], &combDecayKnobs[1] } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combDampingKnobs[0], &combDampingKnobs[1] } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combDispersionKnobs[0], &combDispersionKnobs[1] } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combDriveKnobs[0], &combDriveKnobs[1] } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &combMixKnobs[0], &combMixKnobs[1] } },
                                          std::array<juce::Button*, kFilterInstanceCount> { { &combInvertButtons[0], &combInvertButtons[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combTuneLabels[0], &combTuneLabels[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combDecayLabels[0], &combDecayLabels[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combDampingLabels[0], &combDampingLabels[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combDispersionLabels[0], &combDispersionLabels[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combDriveLabels[0], &combDriveLabels[1] } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &combMixLabels[0], &combMixLabels[1] } },
                                          std::array<juce::AudioParameterBool*, kFilterInstanceCount> { { &audioProcessor.getFilterEnabledParam(0), &audioProcessor.getFilterEnabledParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterCutoffParam(0), &audioProcessor.getFilterCutoffParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterResonanceParam(0), &audioProcessor.getFilterResonanceParam(1) } },
                                          std::array<juce::AudioParameterChoice*, kFilterInstanceCount> { { &audioProcessor.getFilterTypeParam(0), &audioProcessor.getFilterTypeParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterCombTuneParam(0), &audioProcessor.getFilterCombTuneParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterCombDecayParam(0), &audioProcessor.getFilterCombDecayParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterCombDampingParam(0), &audioProcessor.getFilterCombDampingParam(1) } },
                                          kGroupAccents[1]);
    fxPanel = std::make_unique<FxPanel>(robBypassButton,
                                        vibeAmountKnob,
                                        vibeAmountLabel,
                                        vibeTypeBox,
                                        vibeTypeLabel,
                                        delayBypassButton,
                                        isaacTextureKnob,
                                        isaacTextureLabel,
                                        delayAlgoBox,
                                        delayAlgoLabel,
                                        granularSyncBox,
                                        granularSyncLabel,
                                        granularModeBox,
                                        granularModeLabel,
                                        delayTimeKnob,
                                        delayTimeLabel,
                                        delayFeedbackKnob,
                                        delayFeedbackLabel,
                                        moodBypassButton,
                                        moodFreezeButton,
                                        moodMixKnob,
                                        moodMixLabel,
                                        moodClockKnob,
                                        moodClockLabel,
                                        moodWetTimeKnob,
                                        moodWetTimeLabel,
                                        moodWetModifyKnob,
                                        moodWetModifyLabel,
                                        moodLoopLengthKnob,
                                        moodLoopLengthLabel,
                                        moodLoopModifyKnob,
                                        moodLoopModifyLabel,
                                        moodFeedbackKnob,
                                        moodFeedbackLabel,
                                        moodSpreadKnob,
                                        moodSpreadLabel,
                                        moodDegradeKnob,
                                        moodDegradeLabel,
                                        moodRoutingBox,
                                        moodRoutingLabel,
                                        moodWetModeBox,
                                        moodWetModeLabel,
                                        moodLoopModeBox,
                                        moodLoopModeLabel,
                                        reverbBypassButton,
                                        reverbKnob,
                                        reverbLabel,
                                        reverbTypeBox,
                                        reverbTypeLabel,
                                        juce::Colour::fromRGB(120, 186, 255));
    mixPanel = std::make_unique<MixPanel>(audioProcessor,
                                          &knobLookAndFeel,
                                          juce::Colour::fromRGB(212, 212, 212));

    oscPanelViewport.setViewedComponent(oscPanel.get(), false);
    oscPanelViewport.setScrollBarThickness(10);
    oscPanelViewport.setSingleStepSizes(16, 24);
    addAndMakeVisible(oscPanelViewport);
    modPanelViewport.setViewedComponent(modPanel.get(), false);
    modPanelViewport.setScrollBarsShown(true, false);
    modPanelViewport.setScrollBarThickness(10);
    modPanelViewport.setSingleStepSizes(16, 24);
    addAndMakeVisible(modPanelViewport);
    addAndMakeVisible(*ampPanel);
    addAndMakeVisible(*fltPanel);
    addAndMakeVisible(*fxPanel);
    addAndMakeVisible(*mixPanel);

    busEqOverlay = std::make_unique<px3::ui::BusEqOverlay>(audioProcessor);
    busCompOverlay = std::make_unique<px3::ui::BusCompOverlay>(audioProcessor);
    for (auto* sheet : { static_cast<px3::ui::BusInsertOverlay*>(busEqOverlay.get()),
                         static_cast<px3::ui::BusInsertOverlay*>(busCompOverlay.get()) })
    {
        sheet->setKnobLookAndFeel(&knobLookAndFeel);
        sheet->onClose = [this]() { closeBusInsert(); };
        addChildComponent(*sheet);
    }
    addChildComponent(busInsertScrim);

    mixPanel->onOpenBusInsert = [this](int bus, bool wantsEq) { openBusInsert(bus, wantsEq); };

    buildDoomCard();
    buildLucyCard();
    buildChorusCard();
    buildStereoSpreadCard();

    fxPanel->onChainOrderChanged = [this](const px3::FxOrder& order)
    {
        applyFxChainOrder(order, "USER", "USER_DRAG_END", -1, -1);
    };
    fxPanel->setChainOrder(fxSectionOrder);

    for (auto& binding : knobBindings)
    {
        if (binding.parameter != nullptr && binding.slider != nullptr)
        {
            attachSlider(*binding.parameter, *binding.slider);
        }
    }

    attachSlider(audioProcessor.getVibeAmountParam(), vibeAmountKnob);
    attachSlider(audioProcessor.getDelayAmountParam(), isaacTextureKnob);
    attachSlider(audioProcessor.getDelayTimeParam(), delayTimeKnob);
    attachSlider(audioProcessor.getDelayFeedbackParam(), delayFeedbackKnob);
    attachSlider(audioProcessor.getMoodMixParam(), moodMixKnob);
    attachSlider(audioProcessor.getMoodClockParam(), moodClockKnob);
    attachSlider(audioProcessor.getMoodWetTimeParam(), moodWetTimeKnob);
    attachSlider(audioProcessor.getMoodWetModifyParam(), moodWetModifyKnob);
    attachSlider(audioProcessor.getMoodLoopLengthParam(), moodLoopLengthKnob);
    attachSlider(audioProcessor.getMoodLoopModifyParam(), moodLoopModifyKnob);
    attachSlider(audioProcessor.getMoodFeedbackParam(), moodFeedbackKnob);
    attachSlider(audioProcessor.getMoodSpreadParam(), moodSpreadKnob);
    attachSlider(audioProcessor.getMoodDegradeParam(), moodDegradeKnob);
    attachSlider(audioProcessor.getReverbAmountParam(), reverbKnob);
    attachComboBox(audioProcessor.getFilterTypeParam(0), filterTypeBox);
    attachComboBox(audioProcessor.getFilterTypeParam(1), filter2TypeBox);
    attachComboBox(audioProcessor.getOscillatorModeParam(0), osc1ModeBox);
    attachComboBox(audioProcessor.getOscillatorVowelParam(0), osc1VowelBox);
    attachComboBox(audioProcessor.getOscillatorModeParam(1), osc2ModeBox);
    attachComboBox(audioProcessor.getOscillatorVowelParam(1), osc2VowelBox);
    attachComboBox(audioProcessor.getOscillatorModeParam(2), osc3ModeBox);
    attachComboBox(audioProcessor.getOscillatorVowelParam(2), osc3VowelBox);
    attachComboBox(audioProcessor.getLfoWaveformParam(), lfoWaveformBox);
    attachComboBox(audioProcessor.getSubOscOctaveParam(), subOscOctaveBox);
    attachComboBox(audioProcessor.getSubOscWaveformParam(), subOscWaveformBox);
    attachComboBox(audioProcessor.getDelayAlgorithmParam(), delayAlgoBox);
    attachComboBox(audioProcessor.getGranularSyncDivisionParam(), granularSyncBox);
    attachComboBox(audioProcessor.getGranularModeParam(), granularModeBox);
    attachComboBox(audioProcessor.getMoodRoutingParam(), moodRoutingBox);
    attachComboBox(audioProcessor.getMoodWetModeParam(), moodWetModeBox);
    attachComboBox(audioProcessor.getMoodLoopModeParam(), moodLoopModeBox);
    attachComboBox(audioProcessor.getReverbAlgorithmParam(), reverbTypeBox);
    attachComboBox(audioProcessor.getVibeTypeParam(), vibeTypeBox);

    attachButton(audioProcessor.getVibeEnabledParam(), robBypassButton);
    attachButton(audioProcessor.getDelayEnabledParam(), delayBypassButton);
    attachButton(audioProcessor.getMoodEnabledParam(), moodBypassButton);
    attachButton(audioProcessor.getMoodFreezeParam(), moodFreezeButton);
    attachButton(audioProcessor.getReverbEnabledParam(), reverbBypassButton);
    attachButton(audioProcessor.getFilterEnabledParam(0), filter1EnabledButton);
    attachButton(audioProcessor.getFilterEnabledParam(1), filter2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(0), osc1EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(1), osc2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(2), osc3EnabledButton);
    attachButton(audioProcessor.getSubOscEnabledParam(), subOscEnabledButton);
    attachButton(audioProcessor.getLfoEnabledParam(), lfoBypassButton);

    // MIDI status bar is temporarily disabled.

    topMenuBar = std::make_unique<TopMenuBar>();

    // The arrows step through everything the browser lists, INIT included: it is
    // the first row, and skipping it would make the arrows disagree with the
    // list they are stepping through.
    topMenuBar->setOnPresetPrevious([this]()
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

        const auto count = static_cast<int>(presetFiltered.size());
        const auto next = ((currentIndex - 1) % count + count) % count;
        applyPresetRecord(presetFiltered[static_cast<std::size_t>(next)]);
    });

    topMenuBar->setOnPresetNext([this]()
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

        const auto count = static_cast<int>(presetFiltered.size());
        const auto next = (currentIndex + 1) % count;
        applyPresetRecord(presetFiltered[static_cast<std::size_t>(next)]);
    });

    topMenuBar->setOnPresetName([this]() { openPresetBrowser(); });
    topMenuBar->setOnPresetMenu([this]() { showPresetMenu(); });
    topMenuBar->setOnSectionSelected([this](int sectionIndex)
    {
        applyTopMenuSectionSelection(sectionIndex, true);
    });

    addAndMakeVisible(*topMenuBar);

    const auto setupPresetButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(40, 40, 40, 210));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 232, 232));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(68, 124, 180, 220));
    };

    applyTopMenuSectionSelection(audioProcessor.getTopMenuViewIndex(), false);

    // Added before the panel so that, with both marked always-on-top, the panel
    // still sits above the scrim.
    addChildComponent(presetBrowserScrim);

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
    // A full first row of FX cards needs fx.signalFlow.height + gapBelow +
    // fx.grid.rowHeight of panel, which lands the window at 838 - and that read
    // as too tall. 40px is trimmed back off deliberately, so the bottom of the
    // first row sits just under the fold and the grid scrolls to it.
    setSize(1320, 798);

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
    // Adopt whatever the processor is carrying, so a reopened window shows the
    // preset that is actually loaded rather than falling back to INIT.
    if (const auto restored = audioProcessor.getLoadedPreset(); restored.valid)
    {
        hasCurrentPreset = true;
        currentPreset = PresetManager::PresetRecord {};
        currentPreset.file = juce::File(restored.filePath);
        currentPreset.metadata.name = restored.name;
        currentPreset.metadata.category = restored.category;
        currentPreset.metadata.author = restored.author;
        loadedStateHash = computeCurrentStateHash();
        currentPresetDirty = false;
    }

    refreshPresetNameDisplay();

    // Before the first timer tick: a patch loaded with every source bypassed
    // must come up greyed and warning, not animate for a frame first.
    refreshOscillatorEngagedState();

    refreshOscillatorModeUI();
    refreshGranularModeUI();
    refreshLfoAssignmentUI();
    refreshEnvelopeAssignmentUI();
    refreshLfoUI();
    refreshSubOscUI();
    refreshEnvelopeGraphUI();
    refreshFilterUI();
    refreshFxBypassUI();
    applyUiConfig();
    debugEditorCreatedTime = audioProcessor.debugNowTimestamp();

#if PX3_DEBUG_PANEL
    debugPerformanceOverlayLabel.setJustificationType(juce::Justification::centredLeft);
    debugPerformanceOverlayLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    debugPerformanceOverlayLabel.setColour(juce::Label::backgroundColourId, juce::Colour::fromRGBA(0, 0, 0, 240));
    debugPerformanceOverlayLabel.setColour(juce::Label::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 56));
    debugPerformanceOverlayLabel.setFont(juce::FontOptions(11.0f));
    debugPerformanceOverlayLabel.setInterceptsMouseClicks(false, false);
    // NOT added to the editor. It used to float over the bottom-left corner of
    // the plugin window, on top of whatever happened to be there; it lives in
    // the debug console's title row now. See layoutDebugPanel.
    refreshDebugPerformanceOverlay();
#endif

#if PX3_DEBUG_PANEL
    setupDebugPanel();
#endif
    audioProcessor.debugNotifyEditorCreated(this);

    startTimerHz(30);
}

PX3SynthAudioProcessorEditor::~PX3SynthAudioProcessorEditor()
{
    stopTimer();

    // Attachments FIRST, before anything that owns a control they point at.
    //
    // This used to be safe in the other order, because every panel held
    // references to controls the editor owned - so the controls outlived the
    // panels either way. FxCardComponent changed that: those cards own their
    // sliders, boxes and buttons. Destroying the panel first therefore freed
    // the targets while the attachments were still holding raw pointers to
    // them, and ~SliderParameterAttachment called removeListener on freed
    // memory. Measured as a segfault on quit, in
    // juce::Slider::removeListener via ~SliderParameterAttachment.
    sliderAttachments.clear();
    comboBoxAttachments.clear();
    buttonAttachments.clear();

    // Panels hold child components that reference editor-owned controls.
    // Tear panels down after the attachments that point into them.
    oscPanel.reset();
    modPanelViewport.setViewedComponent(nullptr, false);
    modPanel.reset();
    ampPanel.reset();
    fltPanel.reset();
    fxPanel.reset();
    mixPanel.reset();

    closeDebugWindow();
    audioProcessor.debugNotifyEditorDestroyed(this);

    for (auto& binding : knobBindings)
    {
        if (binding.slider != nullptr)
        {
            binding.slider->setLookAndFeel(nullptr);
        }
    }

    vibeAmountKnob.setLookAndFeel(nullptr);
    isaacTextureKnob.setLookAndFeel(nullptr);
    delayTimeKnob.setLookAndFeel(nullptr);
    delayFeedbackKnob.setLookAndFeel(nullptr);
    moodMixKnob.setLookAndFeel(nullptr);
    moodClockKnob.setLookAndFeel(nullptr);
    moodWetTimeKnob.setLookAndFeel(nullptr);
    moodWetModifyKnob.setLookAndFeel(nullptr);
    moodLoopLengthKnob.setLookAndFeel(nullptr);
    moodLoopModifyKnob.setLookAndFeel(nullptr);
    moodFeedbackKnob.setLookAndFeel(nullptr);
    moodSpreadKnob.setLookAndFeel(nullptr);
    moodDegradeKnob.setLookAndFeel(nullptr);
    reverbKnob.setLookAndFeel(nullptr);
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

void PX3SynthAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    // The bus insert sheets draw their backdrop on the SCRIM, which is a
    // component below them, rather than over the top of everything with a hole
    // cut for the sheet. Their faces are translucent, and a hole would let the
    // sharp, undimmed editor show through them while everything beside them was
    // blurred and dark.
    if (busInsertVisible)
    {
        return;
    }

    if (!presetBrowserVisible)
    {
        return;
    }

    // The preset browser is drawn as a modal-like sheet over the main UI. The
    // rest of the editor is blurred and dimmed rather than replaced, so the
    // patch you are browsing away from stays in view.
    px3::ui::paintModalBackdrop(g,
                                getLocalBounds(),
                                presetBrowserPanel.getBounds().toFloat(),
                                presetBrowserBackdropSnapshot,
                                10.0f,
                                juce::Colour::fromRGBA(0, 0, 0, 180),
                                uiConfig != nullptr
                                    ? uiConfig->getFloat("busInserts.backdropBlur", 4.5f)
                                    : 4.5f);
}

void PX3SynthAudioProcessorEditor::resized()
{
    // setResizeLimits() can trigger resized() during construction before
    // extracted panel components are created.
    if (oscPanel == nullptr || modPanel == nullptr || ampPanel == nullptr || fltPanel == nullptr || fxPanel == nullptr || mixPanel == nullptr)
    {
        return;
    }

    // Layout policy:
    // - Header prioritizes logo/preset bar/fx cards for quick performance edits.
    // - Mid section hosts core synth controls.
    // - Bottom section reserves reliable space for performance strip + keyboard.
    // This balancing intentionally avoids dramatic jumps while resizing.
    auto bounds = getLocalBounds().reduced(16);

    const auto headerHeight = uiConfig != nullptr ? uiConfig->getInt("editor.layout.headerHeight", 120) : 120;
    const auto controlsHeight = juce::jlimit(150, 270, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.34)));
    // Fixed, not a fraction of the window. It was height * 0.15, which meant
    // every time the window grew to give the FX cards room the keyboard
    // silently took a share of it - and the fraction had to be re-based to
    // claw that back. 106px is what the fraction produced at every window
    // height up to about 707 anyway, because the lower clamp bound was doing
    // the work; making it explicit means the panels get all of any extra
    // height, at the default size and on resize.
    constexpr auto keyboardHeight = 106;
    const auto sectionGap = uiConfig != nullptr ? uiConfig->getInt("editor.layout.sectionGap", 10) : 10;

    headerArea = bounds.removeFromTop(headerHeight);
    topMenuStripArea = headerArea;

    // How far the bar's contents sit inside the strip. The tabs are meant to
    // read as part of the bar rather than as buttons placed on it, so this is
    // deliberately small.
    const auto stripPadX = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.stripPadX", 3) : 3;
    const auto stripPadY = uiConfig != nullptr ? uiConfig->getInt("topMenu.layout.stripPadY", 3) : 3;
    auto topStripContent = topMenuStripArea.reduced(stripPadX, stripPadY);
    const auto logoWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoPanelWidth", 150) : 150;
    logoPanelArea = topStripContent.removeFromLeft(logoWidth);

    // The logo is drawn across its whole panel but only opens the site from the
    // left of it. The right edge butts against the OSC button now that the
    // sections are flush, and losing the panel you meant to click because a
    // browser opened is a bad trade.
    const auto logoClickInset = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoClickInsetRight", 18) : 18;
    logoClickArea = logoPanelArea.withTrimmedRight(juce::jlimit(0, logoPanelArea.getWidth() / 2, logoClickInset));

    const auto gainWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.gainPanelWidth", 100) : 100;
    topMenuGainArea = topStripContent.removeFromRight(gainWidth);

    headerPlaceholderArea = topStripContent;
    if (topMenuBar != nullptr)
    {
        topMenuBar->setBounds(headerPlaceholderArea);

        const auto menuOrigin = topMenuBar->getPosition();
        topMenuSectionButtonsArea = topMenuBar->getSectionButtonsArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuPresetClusterArea = topMenuBar->getPresetClusterArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuMenuButtonArea = topMenuBar->getPresetMenuButtonBounds().translated(menuOrigin.x, menuOrigin.y);
        presetBarArea = topMenuPresetClusterArea;
    }

    // Knob and label as one group: the label sits directly under the knob
    // rather than pinned to the bottom of the area, which left a gap that grew
    // with the header height.
    auto gainArea = topMenuGainArea.reduced(9, 4);
    // The knob stands alone: no caption, and its name is on hover instead. The
    // top bar is the tightest space in the interface, and a permanent label
    // under a control whose function is obvious costs more than it explains.
    const auto gainKnobSize = juce::jlimit(46, 60,
                                           juce::jmin(gainArea.getWidth() - 6,
                                                      gainArea.getHeight() - 4));

    gainKnob.setBounds(juce::Rectangle<int>(gainKnobSize, gainKnobSize)
                           .withCentre(gainArea.getCentre()));
    gainLabel.setBounds({});

    bounds.removeFromTop(sectionGap);

    const auto desiredControlsHeight = juce::jmax(controlsHeight, bounds.getHeight() - keyboardHeight);
    controlsArea = bounds.removeFromTop(juce::jlimit(0, bounds.getHeight(), desiredControlsHeight));

    // No horizontal inset: the header, the panels and the keyboard all derive
    // from the same `bounds`, and a 4px reduce here was the only thing making
    // the keyboard start and end inboard of the other two.
    auto keyboardRow = bounds;
    const auto perfWidth = juce::jlimit(112, 190, keyboardRow.getWidth() / 8);
    performanceControlsArea = keyboardRow.removeFromLeft(perfWidth);

    // Both sit exactly where they belong; nothing is grown. Their particles are
    // drawn by sparkOverlay, which covers the pair plus the room the animation
    // needs above them.
    pianoKeyboard.setBounds(keyboardRow);
    performanceControls.setBounds(performanceControlsArea);

    const auto headroom = juce::jmin(keyboardSparkHeadroom, controlsArea.getHeight());
    const auto spill = juce::jmax(0, performanceSparkSpill);

    sparkOverlay.setBounds(performanceControlsArea.getUnion(keyboardRow)
                               .expanded(spill, 0)
                               .withTop(keyboardRow.getY() - headroom)
                               .withBottom(keyboardRow.getBottom() + spill)
                               .getIntersection(getLocalBounds()));

    // The overlay goes last, above both, and takes no mouse events - so the
    // keys and the wheels keep every click that was ever theirs.
    pianoKeyboard.toFront(false);
    performanceControls.toFront(false);
    sparkOverlay.toFront(false);

    // Vertical inset only. The horizontal 8 here was the reason the cards sat
    // inboard of the top nav: the header strip is drawn at the full width of
    // `bounds`, so any extra horizontal reduce on the panel area misaligns the
    // two edges.
    panelViewportArea = controlsArea.reduced(0, 8);
    // panels.osc: a declared height wins over the editor's allocation, and
    // overflowY decides whether the panel scrolls when its content is taller
    // than the space it has.
    {
        const auto panelStyle = px3::ui::PanelStyle::fromConfig(uiConfig.get(), "panels.osc");
        auto oscArea = panelViewportArea;
        if (panelStyle.height > 0)
        {
            oscArea = oscArea.withHeight(juce::jmin(panelStyle.height, panelViewportArea.getHeight()));
        }
        oscPanelViewport.setBounds(oscArea);
        oscPanelViewport.setScrollBarsShown(panelStyle.scrollVertically, false);

        // The viewed component keeps its declared height even when that exceeds
        // the viewport - that is what there is to scroll. Without scrolling it
        // matches the viewport exactly, so nothing can be clipped away.
        // A scrolling panel gets a tail of empty space past its last row, so
        // the bottom card can be scrolled clear of the viewport edge instead of
        // stopping flush against it.
        const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
        const auto contentHeight = panelStyle.scrollVertically && panelStyle.height > 0
                                       ? juce::jmax(panelStyle.height, oscArea.getHeight()) + scrollTail
                                       : oscPanelViewport.getMaximumVisibleHeight();
        const auto oscGutter = oscPanelViewport.isVerticalScrollBarShown() ? kScrollBarGutter : 0;
        oscPanel->setSize(juce::jmax(1, oscPanelViewport.getMaximumVisibleWidth() - oscGutter),
                          contentHeight);
    }
    modPanelViewport.setBounds(panelViewportArea);
    ampPanel->setBounds(panelViewportArea);
    fltPanel->setBounds(panelViewportArea);
    fxPanel->setBounds(panelViewportArea);
    mixPanel->setBounds(panelViewportArea);

    layoutOscPanel();
    layoutModPanel();
    layoutAmpPanel();
    layoutFilterPanel();
    layoutFxPanel();
    layoutMixPanel();

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
    presetBrowserScrim.setBounds(getLocalBounds());
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

juce::File PX3SynthAudioProcessorEditor::resolveUiConfigFile() const
{
    const auto executableFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto executableDir = executableFile.getParentDirectory();

#if JUCE_DEBUG || PX3_DEBUG_PANEL
    if (const auto envPath = juce::SystemStats::getEnvironmentVariable("PX3_UI_CONFIG_PATH", {});
        envPath.isNotEmpty())
    {
        auto envFile = juce::File(envPath);
        if (envFile.existsAsFile())
        {
            return envFile;
        }
    }

    const auto cwdCandidate = juce::File::getCurrentWorkingDirectory().getChildFile("Source/UI/UIConfig.json");
    if (cwdCandidate.existsAsFile())
    {
        return cwdCandidate;
    }

    // In debug builds, prefer source-tree config even when a bundled copy exists.
    auto probe = executableDir;
    for (int i = 0; i < 10; ++i)
    {
        const auto sourceCandidate = probe.getChildFile("Source/UI/UIConfig.json");
        if (sourceCandidate.existsAsFile())
        {
            return sourceCandidate;
        }

        const auto rootCandidate = probe.getChildFile("UIConfig.json");
        if (rootCandidate.existsAsFile())
        {
            return rootCandidate;
        }

        const auto parent = probe.getParentDirectory();
        if (parent == probe)
        {
            break;
        }
        probe = parent;
    }
#endif

    const auto contentsCandidate = executableDir.getParentDirectory().getChildFile("UIConfig.json");
    if (contentsCandidate.existsAsFile())
    {
        return contentsCandidate;
    }

    const auto resourcesCandidate = executableDir.getParentDirectory().getChildFile("Resources/UIConfig.json");
    if (resourcesCandidate.existsAsFile())
    {
        return resourcesCandidate;
    }

#if JUCE_DEBUG || PX3_DEBUG_PANEL
    return cwdCandidate;
#else
    return {};
#endif
}

void PX3SynthAudioProcessorEditor::loadUiConfig(bool forceReload)
{
    // Resolving the path probes several candidate locations and the reload check
    // stats the file, so an unthrottled call from the 30 Hz timer meant hundreds
    // of filesystem operations per second per open editor. A hot-reload only has
    // to feel immediate to a human editing the file.
    if (!forceReload)
    {
        constexpr double pollIntervalSeconds = 0.5;
        const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (lastUiConfigPollSeconds > 0.0 && nowSeconds - lastUiConfigPollSeconds < pollIntervalSeconds)
        {
            return;
        }
        lastUiConfigPollSeconds = nowSeconds;
    }

    const auto hadConfigBeforeLoad = (uiConfig != nullptr);
    const auto resolvedPath = resolveUiConfigFile();
    if (resolvedPath == juce::File())
    {
        return;
    }

    if (resolvedPath != uiConfigManager.getConfigFile())
    {
        uiConfigManager.setConfigFile(resolvedPath);
        forceReload = true;
        juce::Logger::writeToLog("[PX3 UIConfig] Switched config path to: " + resolvedPath.getFullPathName());
        audioProcessor.debugLogEvent("UI_CONFIG",
                                     "UI_CONFIG_PATH_SWITCHED",
                                     "file=\"" + resolvedPath.getFullPathName() + "\"");
    }

    const auto result = forceReload ? uiConfigManager.loadInitial() : uiConfigManager.reloadIfChanged();
    if (result.loaded)
    {
        uiConfig = uiConfigManager.getConfig();
        applyUiConfig();

        const auto mode = hadConfigBeforeLoad ? "HOT_RELOAD" : "INITIAL_LOAD";
        audioProcessor.debugLogEvent("UI_CONFIG",
                                     hadConfigBeforeLoad ? "UI_CONFIG_HOT_RELOADED" : "UI_CONFIG_LOADED",
                                     "mode=" + juce::String(mode)
                                         + " file=\"" + uiConfigManager.getConfigFile().getFullPathName() + "\""
                                         + " changed=" + juce::String(result.changed ? 1 : 0));

        if (result.message.isNotEmpty())
        {
            juce::Logger::writeToLog("[PX3 UIConfig] " + result.message);
        }
        return;
    }

    if (result.message.isNotEmpty())
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (forceReload || now - uiConfigLastErrorLogMs > 1000)
        {
            uiConfigLastErrorLogMs = now;
            juce::Logger::writeToLog("[PX3 UIConfig] " + result.message);
        }
    }
}

void PX3SynthAudioProcessorEditor::applyUiConfig()
{
    if (topMenuBar != nullptr)
    {
        topMenuBar->setUIConfig(uiConfig);
    }
    {
        // How far above the keys the sparks are allowed to travel. The keyboard
        // component is grown upward by this much and draws the keys at the
        // bottom of itself; the headroom is transparent and passes clicks
        // through. 0 restores the old behaviour, where sparks were clipped at
        // the top edge of the keys.
        // Sized from the spark physics, not from taste: the burst runs at 60 Hz
        // with a starting speed of up to 8.4 px/frame decaying by 0.93 each
        // frame, over a lifetime of up to 0.45 s. That integrates to about
        // 102 px of travel, which is why the first value of 46 still clipped.
        keyboardSparkHeadroom = uiConfig != nullptr ? uiConfig->getInt("keyboard.sparkHeadroom", 112) : 112;
        // The wheels throw the same sparks, so they get the same room - and
        // more of it, because theirs go out in every direction. resized()
        // turns this into the four margins, clamped to the window.
        // The instrument and the wheels, both fully styled from config.
        pianoKeyboard.setStyle(PianoKeyboard::Style::fromConfig(uiConfig.get(), "keyboard"));
        performanceControls.setStyle(PerformanceControls::Style::fromConfig(uiConfig.get(), "performance"));

        performanceSparkSpill = uiConfig != nullptr
                                    ? uiConfig->getInt("keyboard.wheelSparkSpill", keyboardSparkHeadroom)
                                    : keyboardSparkHeadroom;
        resized();
    }
    {
        // The warning shown when every oscillator source is bypassed. Insets
        // parsed by the same helper the cards use, so padding, paddingTop and
        // the rest mean here what they mean everywhere else.
        PianoKeyboard::WarningStyle warning;
        if (uiConfig != nullptr)
        {
            const juce::String path { "keyboard.warning" };

            const auto readInsets = [this](const juce::String& base, px3::ui::Insets fallback)
            {
                auto result = px3::ui::Insets::parse(uiConfig->getValue(base), fallback);
                const auto side = [&](const char* suffix, float& target)
                {
                    if (const auto v = uiConfig->getValue(base + suffix); ! v.isVoid())
                    {
                        target = static_cast<float>(v);
                    }
                };
                side("Top", result.top);
                side("Right", result.right);
                side("Bottom", result.bottom);
                side("Left", result.left);
                return result;
            };

            // The wording is NOT read from here. Copy belongs with copy, and
            // this file is styling - a string sitting among colours and insets
            // is the one property a translator would need and the last place
            // they would look. It stays compiled in until there is a config
            // that is actually about text.
            warning.background = uiConfig->getColour(path + ".background", warning.background);
            warning.border = uiConfig->getColour(path + ".border.color", warning.border);
            warning.borderWidth = uiConfig->getFloat(path + ".border.width", warning.borderWidth);
            warning.cornerRadius = uiConfig->getFloat(path + ".border.radius", warning.cornerRadius);
            warning.textColour = uiConfig->getColour(path + ".textColour", warning.textColour);
            warning.fontSize = uiConfig->getFloat(path + ".fontSize", warning.fontSize);
            warning.padding = readInsets(path + ".padding", warning.padding);
            warning.margin = readInsets(path + ".margin", warning.margin);

            const auto align = uiConfig->getString(path + ".align", "center");
            warning.alignment = align.equalsIgnoreCase("left")  ? juce::Justification::left
                              : align.equalsIgnoreCase("right") ? juce::Justification::right
                                                                : juce::Justification::centred;
        }
        pianoKeyboard.setWarningStyle(warning);
    }

    if (fxPanel != nullptr)
    {
        fxPanel->setUIConfig(uiConfig);
    }
    if (modPanel != nullptr)
    {
        modPanel->setUIConfig(uiConfig);
    }
    if (ampPanel != nullptr)
    {
        ampPanel->setUIConfig(uiConfig);
    }
    if (oscPanel != nullptr)
    {
        oscPanel->setUIConfig(uiConfig);
    }
    if (fltPanel != nullptr)
    {
        fltPanel->setUIConfig(uiConfig);
    }
    if (mixPanel != nullptr)
    {
        mixPanel->setUIConfig(uiConfig);
    }
    for (auto* sheet : { static_cast<px3::ui::BusInsertOverlay*>(busEqOverlay.get()),
                         static_cast<px3::ui::BusInsertOverlay*>(busCompOverlay.get()) })
    {
        if (sheet != nullptr)
        {
            sheet->setUIConfig(uiConfig);
        }
    }

    if (uiConfig != nullptr)
    {
        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        uiConfig->applyComboStyle(comboStyle, lfoWaveformBox);
        uiConfig->applyComboStyle(comboStyle, lfoAssignBox);
        uiConfig->applyComboStyle(comboStyle, envAssignBox);
        uiConfig->applyComboStyle(comboStyle, subOscOctaveBox);
        uiConfig->applyComboStyle(comboStyle, subOscWaveformBox);
        uiConfig->applyComboStyle(comboStyle, vibeTypeBox);
        uiConfig->applyComboStyle(comboStyle, filterTypeBox);
        uiConfig->applyComboStyle(comboStyle, filter2TypeBox);
        uiConfig->applyComboStyle(comboStyle, osc1ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc2ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc3ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc1VowelBox);
        uiConfig->applyComboStyle(comboStyle, osc2VowelBox);
        uiConfig->applyComboStyle(comboStyle, osc3VowelBox);
        uiConfig->applyComboStyle(comboStyle, delayAlgoBox);
        uiConfig->applyComboStyle(comboStyle, granularSyncBox);
        uiConfig->applyComboStyle(comboStyle, granularModeBox);
        uiConfig->applyComboStyle(comboStyle, moodRoutingBox);
        uiConfig->applyComboStyle(comboStyle, moodWetModeBox);
        uiConfig->applyComboStyle(comboStyle, moodLoopModeBox);
        uiConfig->applyComboStyle(comboStyle, reverbTypeBox);
    }

    resized();
    repaint();
}

void PX3SynthAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();

    if (busInsertVisible)
    {
        auto* sheet = activeBusInsertSheet();
        if (sheet == nullptr || ! sheet->getBounds().contains(point))
        {
            closeBusInsert();
        }
        return;
    }

    if (presetBrowserVisible)
    {
        const auto mousePos = point;
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

    logoClickArmed = false;
    if (logoClickArea.contains(point))
    {
        logoClickArmed = true;
        logoMouseDownPoint = point;
        return;
    }

}

void PX3SynthAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();

    if (presetBrowserVisible)
    {
        if (presetBrowserDragging)
        {
            auto newTopLeft = point - presetBrowserDragOffset;
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

    if (logoClickArmed)
    {
        if (point.getDistanceFrom(logoMouseDownPoint) >= 4)
        {
            logoClickArmed = false;
        }
        return;
    }

}

void PX3SynthAudioProcessorEditor::mouseUp(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();

    if (presetBrowserVisible)
    {
        juce::ignoreUnused(event);
        presetBrowserDragging = false;
        logoClickArmed = false;
        return;
    }

    if (logoClickArmed)
    {
        logoClickArmed = false;
        if (logoClickArea.contains(point))
        {
            juce::URL("https://px3px3.com").launchInDefaultBrowser();
        }
        return;
    }

}





void PX3SynthAudioProcessorEditor::applyFxChainOrder(const px3::FxOrder& order,
                                                     const juce::String& source,
                                                     const juce::String& reason,
                                                     int fromIndex,
                                                     int toIndex)
{
    fxSectionOrder = order;
    commitFxOrderToProcessor(source, reason, fromIndex, toIndex);

    if (fxPanel != nullptr)
    {
        fxPanel->setChainOrder(fxSectionOrder);
    }
}

void PX3SynthAudioProcessorEditor::commitFxOrderToProcessor(const juce::String& source,
                                                                const juce::String& reason,
                                                                int fromIndex,
                                                                int toIndex)
{
    audioProcessor.setFxProcessingOrderWithReason(fxSectionOrder, source, reason, fromIndex, toIndex);
}

void PX3SynthAudioProcessorEditor::rebuildPresetFilteredList()
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

void PX3SynthAudioProcessorEditor::refreshPresetNameDisplay()
{
    juce::String name = hasCurrentPreset ? currentPreset.metadata.name
                                         : px3::processor_internal::kNoPresetLabel;
    if (currentPresetDirty)
    {
        name << "*";
    }

    if (topMenuBar != nullptr)
    {
        topMenuBar->setPresetName(name);

        // Blank for INIT, which is not a preset and has no category or author
        // to report - the tab then draws as a single centred name.
        topMenuBar->setPresetDetails(hasCurrentPreset ? currentPreset.metadata.category : juce::String(),
                                     hasCurrentPreset ? currentPreset.metadata.author : juce::String());
    }

    // Hand the identity to the processor so it survives in DAW state. The
    // editor is destroyed with the window; without this, reopening it showed
    // INIT and no category or author over a patch that had not changed.
    PX3SynthAudioProcessor::LoadedPreset published;
    if (hasCurrentPreset)
    {
        published.name = currentPreset.metadata.name;
        published.category = currentPreset.metadata.category;
        published.author = currentPreset.metadata.author;
        published.filePath = currentPreset.file.getFullPathName();
        published.valid = true;
    }
    audioProcessor.setLoadedPreset(published);
}

void PX3SynthAudioProcessorEditor::applyPresetRecord(const PresetManager::PresetRecord& record)
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

void PX3SynthAudioProcessorEditor::openPresetBrowser()
{
    presetBrowserBackdropSnapshot = createComponentSnapshot(getLocalBounds());
    presetBrowserVisible = true;
    presetBrowserDragging = false;
    presetBrowserScrim.setBounds(getLocalBounds());
    presetBrowserScrim.setVisible(true);
    presetBrowserScrim.setAlwaysOnTop(true);
    presetBrowserScrim.toFront(false);
    presetBrowserPanel.setVisible(true);
    presetBrowserPanel.setAlwaysOnTop(true);
    presetBrowserPanel.toFront(true);
    rebuildPresetFilteredList();
    repaint();
}

void PX3SynthAudioProcessorEditor::closePresetBrowser()
{
    presetBrowserVisible = false;
    presetBrowserDragging = false;
    presetBrowserPanel.setAlwaysOnTop(false);
    presetBrowserPanel.setVisible(false);
    presetBrowserScrim.setAlwaysOnTop(false);
    presetBrowserScrim.setVisible(false);
    presetBrowserBackdropSnapshot = {};
    repaint();
}


// The insert sheets. Both share the preset browser's backdrop, its scrim and
// its click-outside-to-close, because that is what makes a sheet a sheet - and
// neither shares its face.
juce::Component* PX3SynthAudioProcessorEditor::activeBusInsertSheet() const
{
    if (busEqOverlay != nullptr && busEqOverlay->isVisible())
    {
        return busEqOverlay.get();
    }

    if (busCompOverlay != nullptr && busCompOverlay->isVisible())
    {
        return busCompOverlay.get();
    }

    return nullptr;
}

void PX3SynthAudioProcessorEditor::openBusInsert(int bus, bool wantsEq)
{
    if (busEqOverlay == nullptr || busCompOverlay == nullptr)
    {
        return;
    }

    // The preset browser and an insert sheet are both modal, so opening one
    // closes the other rather than stacking two scrims.
    if (presetBrowserVisible)
    {
        closePresetBrowser();
    }

    closeBusInsert();

    auto* sheet = wantsEq ? static_cast<px3::ui::BusInsertOverlay*>(busEqOverlay.get())
                          : static_cast<px3::ui::BusInsertOverlay*>(busCompOverlay.get());

    // Pressing the same strip button again closes it. The scrim normally makes
    // that unreachable, but a keyboard or accessibility activation can still
    // get through - and a control that opens something should be able to shut
    // it rather than being a one-way door.
    if (busInsertVisible && sheet->isVisible() && sheet->getBus() == bus)
    {
        closeBusInsert();
        return;
    }

    sheet->setBusName(bus == PX3SynthAudioProcessor::fxBusInsert ? "FX" : "DRY");
    sheet->setBus(bus);

    // Sized as a fraction of the window rather than in pixels: the sheet has to
    // stay the same proportion of the UI at any window size, and the width is
    // the design decision here, so the config owns it.
    const auto widthFraction = uiConfig != nullptr
                                   ? uiConfig->getFloat(wantsEq ? "busInserts.eq.widthFraction"
                                                                : "busInserts.comp.widthFraction",
                                                        wantsEq ? 0.70f : 0.58f)
                                   : (wantsEq ? 0.70f : 0.58f);
    const auto heightFraction = uiConfig != nullptr
                                    ? uiConfig->getFloat(wantsEq ? "busInserts.eq.heightFraction"
                                                                 : "busInserts.comp.heightFraction",
                                                         wantsEq ? 0.62f : 0.40f)
                                    : (wantsEq ? 0.62f : 0.40f);

    const auto sheetWidth = juce::roundToInt(static_cast<float>(getWidth()) * juce::jlimit(0.2f, 0.98f, widthFraction));
    const auto sheetHeight = juce::roundToInt(static_cast<float>(getHeight()) * juce::jlimit(0.2f, 0.98f, heightFraction));
    sheet->setBounds(juce::Rectangle<int>(0, 0, sheetWidth, sheetHeight)
                         .withCentre(getLocalBounds().getCentre()));

    busInsertBackdropSnapshot = createComponentSnapshot(getLocalBounds());
    busInsertVisible = true;
    // Handed to the scrim, which sits BELOW the sheet - so a sheet with a
    // translucent face shows the dimmed backdrop through itself instead of the
    // untreated editor.
    busInsertScrim.setBlurRadius(uiConfig != nullptr
                                     ? uiConfig->getFloat("busInserts.backdropBlur", 4.5f)
                                     : 4.5f);
    busInsertScrim.setBackdropImage(busInsertBackdropSnapshot);
    busInsertScrim.setBounds(getLocalBounds());
    busInsertScrim.setVisible(true);
    busInsertScrim.setAlwaysOnTop(true);
    busInsertScrim.toFront(false);
    sheet->setSheetVisible(true);
    sheet->setAlwaysOnTop(true);
    sheet->toFront(true);
    repaint();
}

void PX3SynthAudioProcessorEditor::closeBusInsert()
{
    for (auto* sheet : { static_cast<px3::ui::BusInsertOverlay*>(busEqOverlay.get()),
                         static_cast<px3::ui::BusInsertOverlay*>(busCompOverlay.get()) })
    {
        if (sheet != nullptr)
        {
            sheet->setAlwaysOnTop(false);
            // Not setVisible: the sheet also has to stop the spectrum tap, so
            // an overlay nobody is looking at costs the audio thread nothing.
            sheet->setSheetVisible(false);
        }
    }

    busInsertVisible = false;
    busInsertScrim.setAlwaysOnTop(false);
    busInsertScrim.setVisible(false);
    busInsertScrim.setBackdropImage({});
    busInsertBackdropSnapshot = {};
    repaint();
}

void PX3SynthAudioProcessorEditor::showPresetError(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           title,
                                           message,
                                           "OK",
                                           this);
}

void PX3SynthAudioProcessorEditor::savePreset(bool saveAs)
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

void PX3SynthAudioProcessorEditor::importPreset()
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

void PX3SynthAudioProcessorEditor::exportCurrentPreset()
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

void PX3SynthAudioProcessorEditor::showPresetMenu()
{
    enum MenuItemId
    {
        save = 1,
        saveAs,
        favorite,
        import,
        exportPreset,
        debug,
        // Never dispatched: the item carrying it is disabled, so it is a label
        // in the menu rather than a command.
        versionInfo
    };

    juce::PopupMenu menu;
    menu.addItem(MenuItemId::save, "Save");
    menu.addItem(MenuItemId::saveAs, "Save As");
    menu.addSeparator();
    menu.addItem(MenuItemId::favorite,
                 "Add to Favorites",
                 hasCurrentPreset,
                 hasCurrentPreset && currentPreset.isFavorite);
    menu.addSeparator();
    menu.addItem(MenuItemId::import, "Import");
    menu.addItem(MenuItemId::exportPreset, "Export", hasCurrentPreset);
#if PX3_DEBUG_PANEL
    menu.addSeparator();
    menu.addItem(MenuItemId::debug, "Debug");

    // Disabled, so it reads as information rather than as something to click.
    // This is where the version lives now that the logo panel does not show it.
    menu.addSeparator();
    menu.addItem(MenuItemId::versionInfo, "P(X3) Synth v" + px3::version::string(), false, false);
#endif

    if (topMenuBar == nullptr)
    {
        return;
    }

    // The button wears its active face for as long as the menu is up. Without
    // it the strip gives no sign of where the open menu came from - it is the
    // one control here that opens something and then looks untouched.
    auto& menuButton = topMenuBar->getPresetMenuButton();
    menuButton.setToggleState(true, juce::dontSendNotification);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton),
                       [this](int result)
                       {
                           // Runs for every dismissal, including clicking away,
                           // where result is 0 - so the button always comes
                           // back rather than sticking on.
                           if (topMenuBar != nullptr)
                           {
                               topMenuBar->getPresetMenuButton()
                                   .setToggleState(false, juce::dontSendNotification);
                           }

                           switch (result)
                           {
                               case MenuItemId::save:
                                   savePreset(false);
                                   break;
                               case MenuItemId::saveAs:
                                   savePreset(true);
                                   break;
                               case MenuItemId::favorite:
                               {
                                   if (!hasCurrentPreset)
                                   {
                                       return;
                                   }

                                   const auto nextFavorite = !currentPreset.isFavorite;
                                   juce::String error;
                                   if (!presetManager.setFavorite(currentPreset, nextFavorite, error))
                                   {
                                       showPresetError("Favorite Failed", error);
                                       return;
                                   }

                                   presetManager.refreshIndex();
                                   rebuildPresetFilteredList();
                                   if (const auto* found = presetManager.findByFile(currentPreset.file))
                                   {
                                       currentPreset = *found;
                                       refreshPresetNameDisplay();
                                   }
                                   break;
                               }
                               case MenuItemId::import:
                                   importPreset();
                                   break;
                               case MenuItemId::exportPreset:
                                   exportCurrentPreset();
                                   break;
#if PX3_DEBUG_PANEL
                               case MenuItemId::debug:
                                   toggleDebugWindow();
                                   break;
#endif
                               default:
                                   break;
                           }
                       });
}

void PX3SynthAudioProcessorEditor::applyTopMenuSectionSelection(int sectionIndex, bool pushToProcessor)
{
    const auto clamped = juce::jlimit(0, 5, sectionIndex);
    selectedTopMenuSection = clamped;

    if (topMenuBar != nullptr)
    {
        topMenuBar->setSelectedSection(clamped);
    }

    updatePanelVisibility();

    // The six panels are stacked in the same rectangle and swapped by
    // visibility, and paint() draws the FX section cards into that rectangle
    // itself rather than leaving them to a child - see the isPanelVisible
    // (kSectionFx) block. Those pixels belong to the editor, so nothing about
    // hiding fxPanel is guaranteed to clear them: Component::setVisible only
    // invalidates when the flag actually changes, and it invalidates the
    // child's bounds, not whatever the parent painted underneath.
    //
    // Repainting the shared area explicitly removes that whole class of
    // leftover. It costs one repaint per menu click, which is not a rate worth
    // optimising.
    if (! panelViewportArea.isEmpty())
    {
        repaint(panelViewportArea);
    }
    else
    {
        repaint();
    }

    if (clamped == kSectionOsc)
    {
        refreshOscillatorModeUI();
        refreshLfoAssignmentUI();
        refreshLfoUI();
    }
    else if (clamped == kSectionMod)
    {
        refreshLfoAssignmentUI();
        refreshEnvelopeAssignmentUI();
        refreshEnvelopeGraphUI();
    }
    else if (clamped == kSectionAmp)
    {
        refreshAmpEnvelopeUI();
    }
    else if (clamped == kSectionFilter)
    {
        refreshFilterUI();
    }
    else if (clamped == kSectionFx)
    {
        refreshGranularModeUI();
        refreshFxBypassUI();
    }
    else if (clamped == kSectionMix)
    {
        refreshSubOscUI();
    }

    if (pushToProcessor)
    {
        audioProcessor.setTopMenuViewIndex(clamped, true);
    }
}

void PX3SynthAudioProcessorEditor::refreshTopMenuSelectionFromProcessor()
{
    const auto processorIndex = audioProcessor.getTopMenuViewIndex();
    if (processorIndex != selectedTopMenuSection)
    {
        applyTopMenuSectionSelection(processorIndex, false);
    }
}

juce::String PX3SynthAudioProcessorEditor::computeCurrentStateHash() const
{
    auto state = audioProcessor.createPresetStateTree();
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        return {};
    }

    const auto text = xml->toString();
    return juce::String::toHexString(static_cast<juce::int64>(text.hashCode64()));
}

void PX3SynthAudioProcessorEditor::updatePresetDirtyState()
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

int PX3SynthAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(presetFiltered.size());
}

void PX3SynthAudioProcessorEditor::paintListBoxItem(int rowNumber,
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

void PX3SynthAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
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


// ---------------------------------------------------------------- wavetable --
namespace
{
// Factory tables take menu ids 1..N; user tables start here, so the two can
// never be confused by an id that happens to collide after the library grows.
constexpr int kUserWavetableMenuBase = 1000;
} // namespace

void PX3SynthAudioProcessorEditor::rebuildWavetableMenu(int oscIndex)
{
    const auto idx = juce::jlimit(0, 2, oscIndex);
    auto& box = oscWtTableBoxes[static_cast<std::size_t>(idx)];

    const auto previous = box.getSelectedId();
    box.clear(juce::dontSendNotification);

    const auto& factory = px3::factoryWavetables();
    juce::String category;
    for (int i = 0; i < static_cast<int>(factory.size()); ++i)
    {
        // Grouped by category, which is the only thing that keeps a list of
        // tables navigable once there are more than a handful.
        const juce::String next(factory[static_cast<std::size_t>(i)].category);
        if (next != category)
        {
            category = next;
            box.addSectionHeading(category);
        }
        box.addItem(factory[static_cast<std::size_t>(i)].name, i + 1);
    }

    const auto userTables = px3::WavetableLibrary::userTableNames();
    if (! userTables.isEmpty())
    {
        box.addSectionHeading("IMPORTED");
        for (int i = 0; i < userTables.size(); ++i)
        {
            box.addItem(userTables[i], kUserWavetableMenuBase + i);
        }
    }

    // Whatever is actually loaded, which after a preset load is not
    // necessarily what was selected a moment ago.
    const auto userName = audioProcessor.getUserWavetableName(idx);
    if (userName.isNotEmpty())
    {
        const auto found = userTables.indexOf(userName);
        box.setSelectedId(found >= 0 ? kUserWavetableMenuBase + found : previous,
                          juce::dontSendNotification);
    }
    else
    {
        box.setSelectedId(audioProcessor.getOscillatorWtTableParam(idx).getIndex() + 1,
                          juce::dontSendNotification);
    }
}

void PX3SynthAudioProcessorEditor::configureWavetableControls()
{
    for (int osc = 0; osc < 3; ++osc)
    {
        const auto index = static_cast<std::size_t>(osc);
        auto& box = oscWtTableBoxes[index];
        auto& knob = oscWtPositionKnobs[index];

        box.setColour(juce::ComboBox::backgroundColourId, juce::Colour::fromRGBA(34, 34, 34, 210));
        box.setColour(juce::ComboBox::textColourId, juce::Colour::fromRGB(232, 232, 232));
        box.setColour(juce::ComboBox::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 105));

        // Not attached to the choice parameter, because the menu holds user
        // tables too and an AudioParameterChoice cannot grow a list at runtime.
        // The two selections are kept in step by hand instead.
        box.onChange = [this, osc]()
        {
            const auto id = oscWtTableBoxes[static_cast<std::size_t>(osc)].getSelectedId();
            if (id <= 0)
            {
                return;
            }

            if (id >= kUserWavetableMenuBase)
            {
                const auto userTables = px3::WavetableLibrary::userTableNames();
                const auto which = id - kUserWavetableMenuBase;
                if (which >= 0 && which < userTables.size())
                {
                    audioProcessor.setUserWavetableName(osc, userTables[which]);
                }
                return;
            }

            // Back to a factory table: the user selection has to be cleared, or
            // it would win again on the next refresh.
            audioProcessor.setUserWavetableName(osc, {});
            auto& parameter = audioProcessor.getOscillatorWtTableParam(osc);
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost(
                parameter.convertTo0to1(static_cast<float>(id - 1)));
            parameter.endChangeGesture();
            audioProcessor.refreshWavetableSelections();
        };

        oscWtTableLabels[index].setText("TABLE", juce::dontSendNotification);
        oscWtTableLabels[index].setJustificationType(juce::Justification::centred);
        oscWtTableLabels[index].setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        oscWtTableLabels[index].setFont(juce::FontOptions(11.5f));

        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        attachSlider(audioProcessor.getOscillatorWtPositionParam(osc), knob);
        // Same readout as the macro knobs: a percentage, centred, not clickable.
        auto& valueLabel = oscWtPositionValues[index];
        valueLabel.setJustificationType(juce::Justification::centred);
        valueLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(214, 214, 224));
        valueLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        valueLabel.setFont(juce::FontOptions(11.0f));
        valueLabel.setInterceptsMouseClicks(false, false);
        knob.onValueChange = [&knob, &valueLabel]()
        {
            valueLabel.setText(juce::String(juce::roundToInt(
                juce::jlimit(0.0, 1.0, knob.getValue()) * 100.0)) + "%",
                juce::dontSendNotification);
        };
        knob.onValueChange();

        oscWtPositionLabels[index].setText("POSITION", juce::dontSendNotification);
        oscWtPositionLabels[index].setJustificationType(juce::Justification::centred);
        oscWtPositionLabels[index].setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        oscWtPositionLabels[index].setFont(juce::FontOptions(11.5f));

        rebuildWavetableMenu(osc);

        if (oscPanel != nullptr)
        {
            oscPanel->setWavetableControls(osc, box, oscWtTableLabels[index], knob,
                                           oscWtPositionLabels[index], oscWtPositionValues[index]);

            if (auto* graph = oscPanel->getWavetableGraph(osc))
            {
                graph->onFileDropped = [this, osc](const juce::File& file)
                {
                    importWavetableFile(osc, file);
                };
            }
        }
    }
}

void PX3SynthAudioProcessorEditor::refreshWavetableDisplays()
{
    if (oscPanel == nullptr)
    {
        return;
    }

    for (int osc = 0; osc < 3; ++osc)
    {
        auto* graph = oscPanel->getWavetableGraph(osc);
        if (graph == nullptr || ! graph->isVisible())
        {
            continue;
        }

        const auto index = static_cast<std::size_t>(osc);
        const auto loaded = audioProcessor.getLoadedWavetableName(osc);

        // Rebuilding the surface is the expensive part, so it happens when the
        // TABLE changes - not on every frame because the scan moved.
        if (loaded != shownWavetableNames[index])
        {
            shownWavetableNames[index] = loaded;
            graph->setDisplay(audioProcessor.getWavetableDisplay(osc, 40, 192));
            graph->setMissingTableName(audioProcessor.getMissingWavetableName(osc));
            rebuildWavetableMenu(osc);
        }

        const auto base = audioProcessor.getOscillatorWtPositionParam(osc).get();
        const auto modulated = audioProcessor.getModulatedWavetablePosition(osc);
        graph->setPosition(base, modulated);

        // The knob shows it too. The graph shows WHERE in the table the scan is;
        // the knob shows how far modulation is pushing the control, which is
        // what tells you the depth is set sensibly.
        auto& knob = oscWtPositionKnobs[index];
        const auto shown = static_cast<double>(
            knob.getProperties().getWithDefault("modulatedPos", -1.0));
        if (std::abs(shown - static_cast<double>(modulated)) > 0.001)
        {
            knob.getProperties().set("modulatedPos", static_cast<double>(modulated));
            knob.repaint();
        }
    }

    audioProcessor.collectRetiredWavetables();
}

void PX3SynthAudioProcessorEditor::importWavetableFile(int oscIndex, const juce::File& file)
{
    const auto extension = file.getFileExtension().toLowerCase();
    const auto isImage = extension == ".png" || extension == ".jpg"
                      || extension == ".jpeg" || extension == ".gif";

    px3::WavetableImporter::Result imported;

    if (isImage)
    {
        imported = px3::WavetableImporter::fromImage(juce::ImageFileFormat::loadFrom(file));
    }
    else
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
        {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "Import failed",
                "That file could not be read as audio.");
            return;
        }

        // Read in full and summed to mono. A stereo source makes two different
        // tables out of one sound if the channels are taken separately.
        const auto length = static_cast<int>(juce::jmin<juce::int64>(reader->lengthInSamples,
                                                                     48000 * 30));
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), length);
        reader->read(&buffer, 0, length, 0, true, true);

        std::vector<float> mono(static_cast<std::size_t>(length), 0.0f);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* source = buffer.getReadPointer(channel);
            for (int i = 0; i < length; ++i)
            {
                mono[static_cast<std::size_t>(i)] +=
                    source[i] / static_cast<float>(buffer.getNumChannels());
            }
        }

        imported = px3::WavetableImporter::fromAudio(mono.data(), length, reader->sampleRate);
    }

    if (! imported.ok())
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Import failed", imported.description);
        return;
    }

    juce::String error;
    if (! audioProcessor.importWavetable(oscIndex, file.getFileNameWithoutExtension(),
                                         imported.frames, error))
    {
        juce::NativeMessageBox::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon, "Import failed", error);
        return;
    }

    // Forces the display to notice, since the name may not have changed if the
    // same file was dropped twice.
    shownWavetableNames[static_cast<std::size_t>(juce::jlimit(0, 2, oscIndex))].clear();
    rebuildWavetableMenu(oscIndex);
    refreshWavetableDisplays();
}

void PX3SynthAudioProcessorEditor::refreshOscillatorModeUI()
{
    for (int oscIndex = 0; oscIndex < 3; ++oscIndex)
    {
        juce::ComboBox* modeBox = nullptr;
        juce::ComboBox* vowelBox = nullptr;

        if (oscIndex == 0)
        {
            modeBox = &osc1ModeBox;
            vowelBox = &osc1VowelBox;
        }
        else if (oscIndex == 1)
        {
            modeBox = &osc2ModeBox;
            vowelBox = &osc2VowelBox;
        }
        else
        {
            modeBox = &osc3ModeBox;
            vowelBox = &osc3VowelBox;
        }

        const auto paramModeIndex = audioProcessor.getOscillatorModeParam(oscIndex).getIndex();
        if (modeBox->getSelectedItemIndex() != paramModeIndex)
        {
            modeBox->setSelectedItemIndex(paramModeIndex, juce::dontSendNotification);
        }

        const auto enabled = audioProcessor.getOscillatorEnabledParam(oscIndex).get();

        const auto paramVowelIndex = audioProcessor.getOscillatorVowelParam(oscIndex).getIndex();
        if (vowelBox->getSelectedItemIndex() != paramVowelIndex)
        {
            vowelBox->setSelectedItemIndex(paramVowelIndex, juce::dontSendNotification);
        }

        if (oscPanel != nullptr)
        {
            oscPanel->refreshOscillatorFromParameters(oscIndex, enabled, paramModeIndex, paramVowelIndex);
        }
    }
}

void PX3SynthAudioProcessorEditor::refreshAnyKeyDownState()
{
    const auto noteStates = audioProcessor.copyActiveNoteStates();
    const auto noteVelocities = audioProcessor.copyActiveNoteVelocities();
    const auto keyDown = std::any_of(noteStates.begin(), noteStates.end(), [](bool state) { return state; });
    anyKeyDown = keyDown;
    pianoKeyboard.setActiveNotes(noteStates, noteVelocities);
}

void PX3SynthAudioProcessorEditor::refreshGranularModeUI()
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

    delayFeedbackLabel.setTooltip("FEEDBACK");
    delayFeedbackKnob.setTooltip("FEEDBACK");
    delayTimeLabel.setTooltip(delayTimeLabel.getText());
    delayTimeKnob.setTooltip(delayTimeLabel.getText());
}

void PX3SynthAudioProcessorEditor::refreshLfoAssignmentUI()
{
    const auto assignmentIndex = audioProcessor.getLfoAssignmentIndex();
    if (assignmentIndex == lastLfoAssignmentIndex)
    {
        return;
    }

    lastLfoAssignmentIndex = assignmentIndex;
    lfoAssignBox.setSelectedId(assignmentIndex + 1, juce::dontSendNotification);
}

void PX3SynthAudioProcessorEditor::refreshEnvelopeAssignmentUI()
{
    // ENV assignment controls are owned by ModPanel (ENV1/2/3) and refreshed there.
}

void PX3SynthAudioProcessorEditor::refreshLfoFrequencyLabel()
{
    const auto hz = juce::jlimit(0.01f, 20.0f, audioProcessor.getLfoFrequencyParam().get());
    lfoFrequencyValueLabel.setText(juce::String(hz, 2) + " Hz", juce::dontSendNotification);
}

void PX3SynthAudioProcessorEditor::refreshLfoUI()
{
    refreshLfoFrequencyLabel();

    if (modPanel != nullptr)
    {
        modPanel->refreshLfoFromParameters(audioProcessor.getLfoEnabledParam().get(),
                                           audioProcessor.getLfoFrequencyParam().get(),
                                           audioProcessor.getLfoWaveformParam().getIndex());
    }
}

void PX3SynthAudioProcessorEditor::refreshSubOscUI()
{
    if (oscPanel != nullptr)
    {
        oscPanel->refreshSubOscFromParameters(audioProcessor.getSubOscEnabledParam().get(),
                                              audioProcessor.getSubOscOctaveParam().getIndex(),
                                              audioProcessor.getSubOscWaveformParam().getIndex());
    }
}

void PX3SynthAudioProcessorEditor::refreshEnvelopeGraphUI()
{
    if (modPanel != nullptr)
    {
        modPanel->refreshFromParameters();
    }
}

void PX3SynthAudioProcessorEditor::refreshAmpEnvelopeUI()
{
    if (ampPanel != nullptr)
    {
        ampPanel->refreshFromParameters();
    }
}

void PX3SynthAudioProcessorEditor::refreshFilterUI()
{
    if (fltPanel != nullptr)
    {
        fltPanel->refreshFromParameters();
    }
}

bool PX3SynthAudioProcessorEditor::isPanelVisible(int sectionIndex) const
{
    return selectedTopMenuSection == juce::jlimit(0, 5, sectionIndex);
}

void PX3SynthAudioProcessorEditor::updatePanelVisibility()
{
    oscPanelViewport.setVisible(isPanelVisible(kSectionOsc));
    if (oscPanel != nullptr)
    {
        oscPanel->setVisible(true);
    }
    modPanelViewport.setVisible(isPanelVisible(kSectionMod));
    if (modPanel != nullptr)
    {
        modPanel->setVisible(true);
    }
    ampPanel->setVisible(isPanelVisible(kSectionAmp));
    fltPanel->setVisible(isPanelVisible(kSectionFilter));
    fxPanel->setVisible(isPanelVisible(kSectionFx));
    mixPanel->setVisible(isPanelVisible(kSectionMix));
}

void PX3SynthAudioProcessorEditor::layoutOscPanel()
{
    if (oscPanel != nullptr)
    {
        oscPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutFilterPanel()
{
    if (fltPanel != nullptr)
    {
        fltPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutAmpPanel()
{
    if (ampPanel != nullptr)
    {
        ampPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutModPanel()
{
    if (modPanel != nullptr && modPanelViewport.getWidth() > 0 && modPanelViewport.getHeight() > 0)
    {
        const auto preferredWidth = modPanel->getPreferredContentWidth();
        const auto preferredHeight = modPanel->getPreferredContentHeight();
        // A scrolling panel's content stops short of the scrollbar. getWidth()
        // includes the bar, so sizing to it put the cards underneath it.
        const auto gutter = modPanelViewport.isVerticalScrollBarShown() ? kScrollBarGutter : 0;
        const auto available = juce::jmax(1, modPanelViewport.getMaximumVisibleWidth() - gutter);
        const auto contentWidth = juce::jmax(available, preferredWidth);
        const auto scrollTail = uiConfig != nullptr ? uiConfig->getInt("editor.layout.scrollTail", 30) : 30;
        const auto contentHeight = preferredHeight + scrollTail;
        modPanel->setBounds(0, 0, contentWidth, contentHeight);
        modPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutFxPanel()
{
    // The panel lays itself out. It owns the signal-flow strip, the viewport
    // and the grid, so the editor's only job is to hand it the chain order.
    if (fxPanel != nullptr)
    {
        fxPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutMixPanel()
{
    if (mixPanel != nullptr)
    {
        mixPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::refreshFxBypassUI()
{
    const auto vibeEnabled = audioProcessor.getVibeEnabledParam().get();
    const auto delayEnabled = audioProcessor.getDelayEnabledParam().get();
    const auto delayIsGranular = audioProcessor.getDelayAlgorithmParam().getIndex() == 0;
    const auto granularModeSelectable = delayEnabled && delayIsGranular;
    const auto moodEnabled = audioProcessor.getMoodEnabledParam().get();
    const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

    robBypassButton.setToggleState(vibeEnabled, juce::dontSendNotification);
    delayBypassButton.setToggleState(delayEnabled, juce::dontSendNotification);
    moodBypassButton.setToggleState(moodEnabled, juce::dontSendNotification);
    reverbBypassButton.setToggleState(reverbEnabled, juce::dontSendNotification);

    if (fxPanel != nullptr)
    {
        fxPanel->setActive(vibeEnabled, delayEnabled, granularModeSelectable, moodEnabled, reverbEnabled);
    }

    // Cards that own their controls grey themselves out; the panel is told
    // separately so the signal-flow node dims with them.
    const auto doomEnabled = audioProcessor.getDoomEnabledParam().get();
    if (doomCard != nullptr)
    {
        doomCard->bypassButton().setToggleState(doomEnabled, juce::dontSendNotification);
        doomCard->setActive(doomEnabled);
    }
    const auto lucyEnabled = audioProcessor.getLucyEnabledParam().get();
    if (lucyCard != nullptr)
    {
        lucyCard->bypassButton().setToggleState(lucyEnabled, juce::dontSendNotification);
        lucyCard->setActive(lucyEnabled);
    }

    const auto chorusEnabled = audioProcessor.getChorusEnabledParam().get();
    if (chorusCard != nullptr)
    {
        chorusCard->bypassButton().setToggleState(chorusEnabled, juce::dontSendNotification);
        chorusCard->setActive(chorusEnabled);
    }

    const auto spreadEnabled = audioProcessor.getSpreadEnabledParam().get();
    if (spreadCard != nullptr)
    {
        spreadCard->bypassButton().setToggleState(spreadEnabled, juce::dontSendNotification);
        spreadCard->setActive(spreadEnabled);
    }

    if (fxPanel != nullptr)
    {
        fxPanel->setSectionActive(px3::fxStageDoom, doomEnabled);
        fxPanel->setSectionActive(px3::fxStageLucy, lucyEnabled);
        fxPanel->setSectionActive(px3::fxStageChorus, chorusEnabled);
        fxPanel->setSectionActive(px3::fxStageStereoSpread, spreadEnabled);
    }
}

void PX3SynthAudioProcessorEditor::buildDoomCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("doom", "DOOM");

    // Two channels, so two rows of state before the knobs: which channel is
    // running, and the three global switches.
    // Every state toggle in one block, three across, then the three mode
    // selectors. The three-across limit is toggleMaxColumns in UIConfig, so the
    // chips keep their proportions as the card resizes.
    card->addToggleRow({ { "loopActive", "LOOPER", "LISTEN", "Play the captured micro-loop, or keep listening" },
                         { "wetActive", "WET ON", "WET OFF", "Engage the wet channel" },
                         { "freeze", "FROZEN", "FREEZE", "Freeze the wet channel and repeat it" },
                         { "loopHalf", "HALF", "FULL", "Halve the micro-loop length" },
                         { "clockSmooth", "SMOOTH", "STEPPED",
                           "Sweep the clock continuously instead of in harmonised steps" },
                         { "crossSource", "CROSS: CHAN", "CROSS: INPUT",
                           "Modulate from your playing, or let each channel modulate the other" } });

    card->addChoiceRow({ { "loopMode", "LOOP", "Micro-looper mode",
                           audioProcessor.getDoomLoopModeParam().choices },
                         { "routing", "ROUTE", "What the wet channel processes",
                           audioProcessor.getDoomRoutingParam().choices },
                         { "wetMode", "WET", "Wet channel mode",
                           audioProcessor.getDoomWetModeParam().choices } });

    card->addKnobRow({ { "clock", "CLOCK", "Engine sample rate: loop length, pitch and wet time at once" },
                       { "loopLength", "LENGTH", "Micro-looper length (mode dependent)" },
                       { "loopModify", "MODIFY", "Micro-looper character (mode dependent)" },
                       { "wetTime", "TIME", "Wet channel time (mode dependent)" },
                       { "wetModify", "SHAPE", "Wet channel character (mode dependent)" },
                       { "balance", "BALANCE", "Micro-looper against wet channel" } });

    card->addKnobRow({ { "cross", "CROSS", "Signal-dependent interference in pitch and loudness" },
                       { "glue", "GLUE", "End of chain saturator, then destroyer" },
                       { "eq", "EQ", "Tilt: left removes highs, right removes lows" },
                       { "overdub", "OVERDUB", "Record onto the micro-loop" },
                       { "fade", "FADE", "How much of the loop survives each lap while overdubbing" },
                       { "blend", "BLEND", "Clean micro-loop blended past the wet channel" },
                       { "spread", "SPREAD", "Stereo processing depth" } });

    card->addFeatureKnobRow({ "mix", "MIX", "Dry against DOOM" });

    // Attaching by id rather than by reference: the card owns the controls, and
    // a typo here is a null dereference at startup rather than a control that
    // silently does nothing.
    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 14> knobAttachments { {
        { "mix", &audioProcessor.getDoomMixParam() },
        { "clock", &audioProcessor.getDoomClockParam() },
        { "loopLength", &audioProcessor.getDoomLoopLengthParam() },
        { "loopModify", &audioProcessor.getDoomLoopModifyParam() },
        { "overdub", &audioProcessor.getDoomOverdubParam() },
        { "fade", &audioProcessor.getDoomFadeParam() },
        { "wetTime", &audioProcessor.getDoomWetTimeParam() },
        { "wetModify", &audioProcessor.getDoomWetModifyParam() },
        { "cross", &audioProcessor.getDoomCrossParam() },
        { "glue", &audioProcessor.getDoomGlueParam() },
        { "eq", &audioProcessor.getDoomEqParam() },
        { "balance", &audioProcessor.getDoomBalanceParam() },
        { "blend", &audioProcessor.getDoomBlendParam() },
        { "spread", &audioProcessor.getDoomSpreadParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    struct ChoiceAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ChoiceAttachment, 3> choiceAttachments { {
        { "loopMode", &audioProcessor.getDoomLoopModeParam() },
        { "wetMode", &audioProcessor.getDoomWetModeParam() },
        { "routing", &audioProcessor.getDoomRoutingParam() },
    } };

    for (const auto& attachment : choiceAttachments)
    {
        auto* box = card->choice(attachment.id);
        jassert(box != nullptr);
        attachComboBox(*attachment.parameter, *box);
    }

    struct ToggleAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ToggleAttachment, 6> toggleAttachments { {
        { "loopActive", &audioProcessor.getDoomLoopActiveParam() },
        { "wetActive", &audioProcessor.getDoomWetActiveParam() },
        { "freeze", &audioProcessor.getDoomFreezeParam() },
        { "loopHalf", &audioProcessor.getDoomLoopHalfParam() },
        { "clockSmooth", &audioProcessor.getDoomClockSmoothParam() },
        { "crossSource", &audioProcessor.getDoomCrossSourceParam() },
    } };

    for (const auto& attachment : toggleAttachments)
    {
        auto* button = card->toggle(attachment.id);
        jassert(button != nullptr);
        attachButton(*attachment.parameter, *button);
    }

    attachButton(audioProcessor.getDoomEnabledParam(), card->bypassButton());

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("mix"))
    {
        knob->getProperties().set("psychedelicFx", true);
        knob->getProperties().set("psychedelicInverted", true);
    }

    doomCard = card.get();
    fxPanel->addCard(px3::fxStageDoom, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildLucyCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("lucy", "LUCY");

    card->addToggleRow({ { "freeze", "FROZEN", "FREEZE", "Freeze the spectrum" },
                         { "freezeSlushy", "SLUSHY", "SOLID",
                           "Let the freeze keep updating from what you play" },
                         { "gate", "GATE ON", "GATE OFF", "Silence anything below the cutoff" },
                         { "verbPost", "V-POST", "V-PRE",
                           "Reverb after the chain, or in front of it feeding the loss" },
                         { "filterInvert", "REJECT", "PASS",
                           "Keep the band, or keep everything but the band" },
                         { "slow", "SLOW ON", "SLOW OFF",
                           "Bigger, darker, slower, and with more latency" } });

    card->addChoiceRow({ { "mode", "MODE", "Loss mode",
                           audioProcessor.getLucyModeParam().choices },
                         { "slope", "SLOPE", "Filter slope",
                           audioProcessor.getLucySlopeParam().choices },
                         { "packets", "PACKETS", "Packet corruption mode",
                           audioProcessor.getLucyPacketsParam().choices } });

    card->addKnobRow({ { "loss", "LOSS", "Depth of the loss and packet effects, and which frequencies they reach" },
                       { "speed", "SPEED", "How fast the loss, packets and freeze update" },
                       { "filter", "FILTER", "Filter width; fully down is no filtering" },
                       { "filterFreq", "FREQ", "Filter centre frequency" },
                       { "verb", "VERB", "Reverb mix" },
                       { "verbDecay", "DECAY", "Reverb size and length" } });

    card->addKnobRow({ { "freezer", "FREEZER", "Live against frozen" },
                       { "gateCutoff", "CUTOFF", "Gate threshold" },
                       { "threshold", "LIMIT", "Limiter threshold; lower means more limiting" },
                       { "autoGain", "AUTO GAIN", "Gain compensation for the loss modes" },
                       { "weighting", "WEIGHT", "Dark, psychoacoustic, or bright frequency weighting" },
                       { "gain", "GAIN", "Wet gain, plus or minus 36 dB" },
                       { "spread", "SPREAD", "Packet alternation and reverb width" } });

    card->addFeatureKnobRow({ "global", "GLOBAL", "Overall amount of processing" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 14> knobAttachments { {
        { "global", &audioProcessor.getLucyGlobalParam() },
        { "loss", &audioProcessor.getLucyLossParam() },
        { "speed", &audioProcessor.getLucySpeedParam() },
        { "filter", &audioProcessor.getLucyFilterParam() },
        { "filterFreq", &audioProcessor.getLucyFilterFreqParam() },
        { "verb", &audioProcessor.getLucyVerbParam() },
        { "verbDecay", &audioProcessor.getLucyVerbDecayParam() },
        { "freezer", &audioProcessor.getLucyFreezerParam() },
        { "gateCutoff", &audioProcessor.getLucyGateCutoffParam() },
        { "threshold", &audioProcessor.getLucyThresholdParam() },
        { "autoGain", &audioProcessor.getLucyAutoGainParam() },
        { "weighting", &audioProcessor.getLucyWeightingParam() },
        { "gain", &audioProcessor.getLucyGainParam() },
        { "spread", &audioProcessor.getLucySpreadParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    struct ChoiceAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ChoiceAttachment, 3> choiceAttachments { {
        { "mode", &audioProcessor.getLucyModeParam() },
        { "packets", &audioProcessor.getLucyPacketsParam() },
        { "slope", &audioProcessor.getLucySlopeParam() },
    } };

    for (const auto& attachment : choiceAttachments)
    {
        auto* box = card->choice(attachment.id);
        jassert(box != nullptr);
        attachComboBox(*attachment.parameter, *box);
    }

    struct ToggleAttachment { const char* id; juce::RangedAudioParameter* parameter; };
    const std::array<ToggleAttachment, 6> toggleAttachments { {
        { "freeze", &audioProcessor.getLucyFreezeParam() },
        { "freezeSlushy", &audioProcessor.getLucyFreezeSlushyParam() },
        { "gate", &audioProcessor.getLucyGateParam() },
        { "verbPost", &audioProcessor.getLucyVerbPostParam() },
        { "filterInvert", &audioProcessor.getLucyFilterInvertParam() },
        { "slow", &audioProcessor.getLucySlowParam() },
    } };

    for (const auto& attachment : toggleAttachments)
    {
        auto* button = card->toggle(attachment.id);
        jassert(button != nullptr);
        attachButton(*attachment.parameter, *button);
    }

    attachButton(audioProcessor.getLucyEnabledParam(), card->bypassButton());

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("global"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

    lucyCard = card.get();
    fxPanel->addCard(px3::fxStageLucy, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildChorusCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("chorus", "CHORUS");

    card->addChoiceRow({ { "mode", "MODE", "Dimension mode, ensemble, or CE-style",
                           audioProcessor.getChorusModeParam().choices } });

    card->addKnobRow({ { "rate", "RATE", "Modulation rate" },
                       { "depth", "DEPTH", "Modulation excursion" },
                       { "width", "WIDTH", "Stereo expansion of the wet pair" },
                       { "spread", "SPREAD", "Phase offset between the two delay paths" } });

    card->addKnobRow({ { "tone", "TONE", "Warm against clear, on the wet path only" },
                       { "lowCut", "LOW CUT", "Wet-path high-pass: what anchors the bass" },
                       { "feedback", "FEEDBACK", "Colour; capped short of flanging" },
                       { "character", "CHARACTER", "BBD emphasis, companding and bandwidth" },
                       { "mix", "MIX", "Final dry against wet" } });

    card->addFeatureKnobRow({ "amount", "AMOUNT", "Overall intensity" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 10> knobAttachments { {
        { "amount", &audioProcessor.getChorusAmountParam() },
        { "rate", &audioProcessor.getChorusRateParam() },
        { "depth", &audioProcessor.getChorusDepthParam() },
        { "width", &audioProcessor.getChorusWidthParam() },
        { "spread", &audioProcessor.getChorusSpreadParam() },
        { "tone", &audioProcessor.getChorusToneParam() },
        { "lowCut", &audioProcessor.getChorusLowCutParam() },
        { "feedback", &audioProcessor.getChorusFeedbackParam() },
        { "character", &audioProcessor.getChorusCharacterParam() },
        { "mix", &audioProcessor.getChorusMixParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    attachComboBox(audioProcessor.getChorusModeParam(), *card->choice("mode"));
    attachButton(audioProcessor.getChorusEnabledParam(), card->bypassButton());

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("amount"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

    chorusCard = card.get();
    fxPanel->addCard(px3::fxStageChorus, std::move(card));
}

void PX3SynthAudioProcessorEditor::buildStereoSpreadCard()
{
    auto card = std::make_unique<px3::ui::FxCardComponent>("stereoSpread", "SPREAD");

    card->addChoiceRow({ { "mode", "MODE", "Widening strategy",
                           audioProcessor.getSpreadModeParam().choices } });

    card->addKnobRow({ { "width", "WIDTH", "Overall stereo expansion" },
                       { "depth", "DEPTH", "Decorrelation depth" },
                       { "center", "CENTER", "How strongly the middle is anchored" },
                       { "tone", "TONE", "Tilt on the side signal only" } });

    card->addKnobRow({ { "lowWidth", "LOW W", "Width permitted below the low crossover" },
                       { "highWidth", "HIGH W", "Width in the top band" },
                       { "lowFreq", "LOW XO", "Low crossover: below it, mono" },
                       { "highFreq", "HIGH XO", "High crossover: above it, level rather than phase" },
                       { "mix", "MIX", "Final dry against wet" } });

    card->addFeatureKnobRow({ "amount", "AMOUNT", "Overall amount of spatial processing" });

    struct KnobAttachment { const char* id; juce::AudioParameterFloat* parameter; };
    const std::array<KnobAttachment, 10> knobAttachments { {
        { "amount", &audioProcessor.getSpreadAmountParam() },
        { "width", &audioProcessor.getSpreadWidthParam() },
        { "depth", &audioProcessor.getSpreadDepthParam() },
        { "center", &audioProcessor.getSpreadCenterParam() },
        { "tone", &audioProcessor.getSpreadToneParam() },
        { "lowWidth", &audioProcessor.getSpreadLowWidthParam() },
        { "highWidth", &audioProcessor.getSpreadHighWidthParam() },
        { "lowFreq", &audioProcessor.getSpreadLowFreqParam() },
        { "highFreq", &audioProcessor.getSpreadHighFreqParam() },
        { "mix", &audioProcessor.getSpreadMixParam() },
    } };

    for (const auto& attachment : knobAttachments)
    {
        auto* slider = card->knob(attachment.id);
        jassert(slider != nullptr);
        const auto& range = attachment.parameter->getNormalisableRange();
        slider->setRange(range.start, range.end);
        slider->setLookAndFeel(&knobLookAndFeel);
        attachSlider(*attachment.parameter, *slider);
    }

    attachComboBox(audioProcessor.getSpreadModeParam(), *card->choice("mode"));
    attachButton(audioProcessor.getSpreadEnabledParam(), card->bypassButton());

    // The macro knob wears the rainbow ring, the same as VIBE's amount.
    if (auto* knob = card->knob("amount"))
    {
        knob->getProperties().set("psychedelicFx", true);
    }

    spreadCard = card.get();
    fxPanel->addCard(px3::fxStageStereoSpread, std::move(card));
}

void PX3SynthAudioProcessorEditor::refreshOscillatorEngagedState()
{
    auto engaged = audioProcessor.getSubOscEnabledParam().get();
    for (int osc = 0; osc < kOscillatorSourceCount; ++osc)
    {
        engaged = engaged || audioProcessor.getOscillatorEnabledParam(osc).get();
    }

    // Pushed every tick rather than only on a change. Tracking the previous
    // value here meant the keyboard's state was owned in two places, and if
    // they ever disagreed - a keyboard silenced while this still read
    // "engaged" - nothing would ever put it right. setSilenced is a no-op when
    // the state already matches, so this is self-correcting and costs nothing.
    pianoKeyboard.setSilenced(! engaged);

    const auto changed = engaged != anyOscillatorEngaged;
    anyOscillatorEngaged = engaged;

    if (changed && ! engaged)
    {
        // Stop the logo mid-shake rather than letting it ring out over a
        // keyboard that has just been greyed.
        logoVibrationIntensity = 0.0f;
        repaint(logoPanelArea);
    }
}

void PX3SynthAudioProcessorEditor::timerCallback()
{
    loadUiConfig(false);
    refreshWavetableDisplays();

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto deltaSeconds = (lastAnimationTickSeconds > 0.0)
                                  ? static_cast<float>(nowSeconds - lastAnimationTickSeconds)
                                  : (1.0f / 30.0f);
    lastAnimationTickSeconds = nowSeconds;

    // Timer drives non-audio UI synchronization only. DSP state is never
    // computed here; this keeps audio-thread responsibilities isolated.
    // The processor can reorder the chain without the UI (preset load, host
    // automation), so the strip and grid follow it back.
    if (isPanelVisible(kSectionFx))
    {
        const auto processorOrder = audioProcessor.getFxProcessingOrder();
        if (processorOrder != fxSectionOrder)
        {
            fxSectionOrder = processorOrder;

            if (fxPanel != nullptr)
            {
                fxPanel->setChainOrder(fxSectionOrder);
            }
        }
    }

    if (isPanelVisible(kSectionFx))
    {
        refreshGranularModeUI();
        refreshFxBypassUI();
    }

    const auto lfoUiVisible = isPanelVisible(kSectionOsc) || isPanelVisible(kSectionMod);
    if (lfoUiVisible)
    {
        refreshLfoUI();
    }

    if (isPanelVisible(kSectionOsc))
    {
        refreshOscillatorModeUI();
        refreshLfoAssignmentUI();
        refreshSubOscUI();
    }
    else if (isPanelVisible(kSectionMod))
    {
        refreshLfoAssignmentUI();
        refreshEnvelopeAssignmentUI();
        refreshEnvelopeGraphUI();
    }
    else if (isPanelVisible(kSectionAmp))
    {
        refreshAmpEnvelopeUI();
    }
    else if (isPanelVisible(kSectionFilter))
    {
        refreshFilterUI();
    }
    refreshTopMenuSelectionFromProcessor();

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

    if (isPanelVisible(kSectionOsc) && oscPanel != nullptr)
    {
        oscPanel->advanceAnimation(0.09f);
    }

    if (isPanelVisible(kSectionMod) && modPanel != nullptr)
    {
        modPanel->advanceAnimation(deltaSeconds);
    }

#if PX3_DEBUG_PANEL
    refreshDebugPerformanceOverlay();
#endif

    if (isPanelVisible(kSectionMix) && mixPanel != nullptr)
    {
        mixPanel->advanceAnimation(0.05f);
    }

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
            if (anyOscillatorEngaged)
            {
                logoVibrationIntensity = juce::jmax(logoVibrationIntensity, velNorm);
            }
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
        presetBrowserScrim.setAlwaysOnTop(true);
        presetBrowserScrim.toFront(false);
        presetBrowserPanel.setAlwaysOnTop(true);
        presetBrowserPanel.toFront(false);
    }

    refreshOscillatorEngagedState();

    if (anyOscillatorEngaged && (logoVibrationIntensity > 0.001f || anyKeyDown))
    {
        logoVibrationPhase += 0.38f;

        if (logoVibrationPhase > juce::MathConstants<float>::twoPi)
        {
            logoVibrationPhase -= juce::MathConstants<float>::twoPi;
        }

        const auto decay = anyKeyDown ? 0.968f : 0.928f;
        logoVibrationIntensity *= decay;

        // Only the logo panel animates here. A bare repaint() invalidated the
        // whole editor 30 times a second for as long as any key was held, and
        // a full repaint measured 14.5 ms - 43% of a core - against 0.44 ms for
        // this region. Nothing outside logoPanelArea changes: the shake and the
        // glitch offsets are bounded by a few pixels and are drawn inside it,
        // so the margin below covers the full extent of the movement.
        constexpr int logoAnimationMarginPx = 8;
        repaint(logoPanelArea.expanded(logoAnimationMarginPx));
    }
}

void PX3SynthAudioProcessorEditor::refreshDebugPerformanceOverlay()
{
#if PX3_DEBUG_PANEL
    constexpr uint32_t kUpdateIntervalMs = 200;
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (nowMs - debugPerformanceOverlayLastUpdateMs < kUpdateIntervalMs)
    {
        return;
    }
    debugPerformanceOverlayLastUpdateMs = nowMs;

    const auto cpuPercent = juce::jlimit(0.0, 999.0, static_cast<double>(audioProcessor.debugGetInstanceCpuLoadPercent()));

    // Labelled HOST RSS, not RAM, because that is what it is - see
    // processResidentMemoryMb. The delta since the editor opened is the part
    // worth watching: a number that only ever goes up is a leak, whatever the
    // host started at.
    const auto rssMb = juce::jlimit(0.0, 99999.0, processResidentMemoryMb());
    if (debugHostRssBaselineMb <= 0.0)
    {
        debugHostRssBaselineMb = rssMb;
    }
    const auto deltaMb = rssMb - debugHostRssBaselineMb;

    debugPerformanceOverlayLabel.setText("CPU: " + juce::String(cpuPercent, 1) + "%"
                                             + " | HOST RSS: " + juce::String(rssMb, 1) + " MB"
                                             + " (" + (deltaMb >= 0.0 ? "+" : "") + juce::String(deltaMb, 1) + ")",
                                         juce::dontSendNotification);
#endif
}
