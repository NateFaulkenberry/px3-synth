#include "PluginEditor.h"

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

void PX3SynthAudioProcessorEditor::KnobLabel::paint(juce::Graphics& g)
{
    if (getText().isEmpty())
    {
        return;
    }

    const auto compactLabel = static_cast<bool>(getProperties().getWithDefault("compactLabel", false));
    const auto horizontalPadding = compactLabel ? 4.0f : 8.0f;
    auto area = getLocalBounds().toFloat().reduced(2.0f, 1.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 54));
    g.fillRoundedRectangle(area, 7.0f);

    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 96));
    g.drawRoundedRectangle(area, 7.0f, 1.0f);

    g.setColour(findColour(juce::Label::textColourId));
    g.setFont(getFont());
    g.drawText(getText(),
               area.reduced(horizontalPadding, 0.0f).toNearestInt(),
               juce::Justification::centred,
               true);
}

juce::String PX3SynthAudioProcessorEditor::noteNameForMidi(int midiNote)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    if (midiNote < 0 || midiNote > 127)
    {
        return "-";
    }

    const auto octave = (midiNote / 12) - 1;
    return juce::String(names[midiNote % 12]) + juce::String(octave);
}

juce::String PX3SynthAudioProcessorEditor::fxModuleIdFromSection(int sectionId)
{
    return moduleIdFromSectionId(sectionId);
}

int PX3SynthAudioProcessorEditor::fxSectionFromModuleId(const juce::String& moduleId)
{
    return sectionIdFromModuleId(moduleId);
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
        KnobBinding { &osc2SineKnob, &osc2SineLabel, nullptr },
        KnobBinding { &osc2SawKnob, &osc2SawLabel, nullptr },
        KnobBinding { &osc2SquareKnob, &osc2SquareLabel, nullptr },
        KnobBinding { &osc3SineKnob, &osc3SineLabel, nullptr },
        KnobBinding { &osc3SawKnob, &osc3SawLabel, nullptr },
        KnobBinding { &osc3SquareKnob, &osc3SquareLabel, nullptr },
        KnobBinding { &cutoffKnob, &cutoffLabel, nullptr },
        KnobBinding { &resonanceKnob, &resonanceLabel, nullptr },
        KnobBinding { &cutoff2Knob, &cutoff2Label, nullptr },
        KnobBinding { &resonance2Knob, &resonance2Label, nullptr },
        KnobBinding { &attackKnob, &attackLabel, nullptr },
        KnobBinding { &decayKnob, &decayLabel, nullptr },
        KnobBinding { &sustainKnob, &sustainLabel, nullptr },
        KnobBinding { &releaseKnob, &releaseLabel, nullptr },
        KnobBinding { &lfoFrequencyKnob, &lfoFrequencyLabel, nullptr },
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
    configureKnob(knobBindings[9], "Cutoff", audioProcessor.getFilterCutoffParam(0));
    configureKnob(knobBindings[10], "Reso", audioProcessor.getFilterResonanceParam(0));
    configureKnob(knobBindings[11], "Cutoff", audioProcessor.getFilterCutoffParam(1));
    configureKnob(knobBindings[12], "Reso", audioProcessor.getFilterResonanceParam(1));
    configureKnob(knobBindings[13], "Attack", audioProcessor.getAttackParam());
    configureKnob(knobBindings[14], "Decay", audioProcessor.getDecayParam());
    configureKnob(knobBindings[15], "Sustain", audioProcessor.getSustainParam());
    configureKnob(knobBindings[16], "Release", audioProcessor.getReleaseParam());
    configureKnob(knobBindings[17], "Freq", audioProcessor.getLfoFrequencyParam());
    configureKnob(knobBindings[18], "Master", audioProcessor.getMasterGainParam());

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

    lfoFrequencyKnob.onValueChange = [this]()
    {
        refreshLfoFrequencyLabel();
    };

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
    lfoWaveformLabel.setInterceptsMouseClicks(false, false);

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

    const auto configureMixFader = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearVertical);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange(0.0, 1.0, 0.0);
        slider.setDoubleClickReturnValue(true, 1.0);
        slider.setScrollWheelEnabled(false);
    };

    configureMixFader(subOscLevelKnob);
    configureMixFader(osc1LevelFader);
    configureMixFader(osc2LevelFader);
    configureMixFader(osc3LevelFader);

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
    subOscOctaveLabel.setInterceptsMouseClicks(false, false);

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
    subOscWaveformLabel.setInterceptsMouseClicks(false, false);

    subOscEnabledLabel.setText("ON", juce::dontSendNotification);
    subOscEnabledLabel.setJustificationType(juce::Justification::centredLeft);
    subOscEnabledLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    subOscEnabledLabel.setFont(juce::FontOptions(11.5f));
    subOscEnabledLabel.setInterceptsMouseClicks(false, false);

    subOscEnabledButton.setButtonText("");
    subOscEnabledButton.setClickingTogglesState(true);
    subOscEnabledButton.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
    subOscEnabledButton.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));

    const auto configureOscEnabledControl = [](juce::Label& label, juce::ToggleButton& button)
    {
        label.setText("ON", juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centredLeft);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
        label.setFont(juce::FontOptions(11.5f));
        label.setInterceptsMouseClicks(false, false);

        button.setButtonText("");
        button.setClickingTogglesState(true);
        button.setColour(juce::ToggleButton::textColourId, juce::Colour::fromRGB(210, 210, 210));
        button.setColour(juce::ToggleButton::tickColourId, juce::Colour::fromRGB(196, 196, 196));
    };

    configureOscEnabledControl(osc1EnabledLabel, osc1EnabledButton);
    configureOscEnabledControl(osc2EnabledLabel, osc2EnabledButton);
    configureOscEnabledControl(osc3EnabledLabel, osc3EnabledButton);
    configureOscEnabledControl(filter1EnabledLabel, filter1EnabledButton);
    configureOscEnabledControl(filter2EnabledLabel, filter2EnabledButton);

    filter1EnabledButton.onClick = [this]()
    {
        refreshFilterUI();
    };
    filter2EnabledButton.onClick = [this]()
    {
        refreshFilterUI();
    };

    // OSC macro labels can become long in some modes; use a slightly smaller font.
    oscSineLabel.setFont(juce::FontOptions(11.0f));
    oscSawLabel.setFont(juce::FontOptions(11.0f));
    oscSquareLabel.setFont(juce::FontOptions(11.0f));
    osc2SineLabel.setFont(juce::FontOptions(11.0f));
    osc2SawLabel.setFont(juce::FontOptions(11.0f));
    osc2SquareLabel.setFont(juce::FontOptions(11.0f));
    osc3SineLabel.setFont(juce::FontOptions(11.0f));
    osc3SawLabel.setFont(juce::FontOptions(11.0f));
    osc3SquareLabel.setFont(juce::FontOptions(11.0f));

    configureEffectKnob(vibeAmountKnob, vibeAmountLabel, "AMOUNT", audioProcessor.getVibeAmountParam());
    configureEffectKnob(isaacTextureKnob, isaacTextureLabel, "", audioProcessor.getDelayAmountParam());
    configureEffectKnob(delayTimeKnob, delayTimeLabel, "TIME", audioProcessor.getDelayTimeParam());
    configureEffectKnob(delayFeedbackKnob, delayFeedbackLabel, "FEEDBACK", audioProcessor.getDelayFeedbackParam());
    configureEffectKnob(reverbKnob, reverbLabel, "", audioProcessor.getReverbAmountParam());

    // isaacTextureLabel.setText("INTENSITY", juce::dontSendNotification);
    // isaacTextureLabel.setVisible(true);

    // Compact labels and tooltips keep delay controls readable in narrow layouts.
    isaacTextureLabel.getProperties().set("compactLabel", true);
    delayTimeLabel.getProperties().set("compactLabel", true);
    delayFeedbackLabel.getProperties().set("compactLabel", true);
    // isaacTextureLabel.setTooltip("INTENSITY");
    // isaacTextureKnob.setTooltip("INTENSITY");
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
    vibeTypeLabel.setInterceptsMouseClicks(false, false);

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
        modeLabel.setInterceptsMouseClicks(false, false);

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
        vowelLabel.setInterceptsMouseClicks(false, false);
    };

    configureOscSelector(0, oscModeBox, oscModeLabel, oscVowelBox, oscVowelLabel);
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
    delayAlgoLabel.setText("ALGO", juce::dontSendNotification);
    delayAlgoLabel.setJustificationType(juce::Justification::centred);
    delayAlgoLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(232, 232, 232));
    delayAlgoLabel.setFont(juce::FontOptions(11.5f));
    delayAlgoLabel.setInterceptsMouseClicks(false, false);

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

    oscPanel = std::make_unique<OscPanel>(subOscEnabledButton,
                                          subOscEnabledLabel,
                                          subOscOctaveBox,
                                          subOscOctaveLabel,
                                          subOscWaveformBox,
                                          subOscWaveformLabel,
                                          oscSineKnob,
                                          oscSawKnob,
                                          oscSquareKnob,
                                          osc1EnabledButton,
                                          osc1EnabledLabel,
                                          oscSineLabel,
                                          oscSawLabel,
                                          oscSquareLabel,
                                          oscModeBox,
                                          oscModeLabel,
                                          oscVowelBox,
                                          oscVowelLabel,
                                          osc2SineKnob,
                                          osc2SawKnob,
                                          osc2SquareKnob,
                                          osc2EnabledButton,
                                          osc2EnabledLabel,
                                          osc2SineLabel,
                                          osc2SawLabel,
                                          osc2SquareLabel,
                                          osc2ModeBox,
                                          osc2ModeLabel,
                                          osc2VowelBox,
                                          osc2VowelLabel,
                                          osc3SineKnob,
                                          osc3SawKnob,
                                          osc3SquareKnob,
                                          osc3EnabledButton,
                                          osc3EnabledLabel,
                                          osc3SineLabel,
                                          osc3SawLabel,
                                          osc3SquareLabel,
                                          osc3ModeBox,
                                          osc3ModeLabel,
                                          osc3VowelBox,
                                          osc3VowelLabel,
                                          lfoAssignLabel,
                                          lfoAssignBox,
                                          lfoFrequencyKnob,
                                          lfoFrequencyLabel,
                                          lfoFrequencyValueLabel,
                                          lfoWaveformBox,
                                          lfoWaveformLabel,
                                          juce::Colour::fromRGB(120, 180, 255),
                                          kGroupAccents[0],
                                          kGroupAccents[3]);
    envPanel = std::make_unique<EnvPanel>(audioProcessor.getAttackParam(),
                                          audioProcessor.getDecayParam(),
                                          audioProcessor.getSustainParam(),
                                          audioProcessor.getReleaseParam(),
                                          kGroupAccents[2]);
    fltPanel = std::make_unique<FltPanel>(std::array<juce::ToggleButton*, kFilterInstanceCount> { { &filter1EnabledButton, &filter2EnabledButton } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &filter1EnabledLabel, &filter2EnabledLabel } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &cutoffKnob, &cutoff2Knob } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &cutoffLabel, &cutoff2Label } },
                                          std::array<juce::Slider*, kFilterInstanceCount> { { &resonanceKnob, &resonance2Knob } },
                                          std::array<juce::Label*, kFilterInstanceCount> { { &resonanceLabel, &resonance2Label } },
                                          std::array<juce::ComboBox*, kFilterInstanceCount> { { &filterTypeBox, &filter2TypeBox } },
                                          std::array<juce::AudioParameterBool*, kFilterInstanceCount> { { &audioProcessor.getFilterEnabledParam(0), &audioProcessor.getFilterEnabledParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterCutoffParam(0), &audioProcessor.getFilterCutoffParam(1) } },
                                          std::array<juce::AudioParameterFloat*, kFilterInstanceCount> { { &audioProcessor.getFilterResonanceParam(0), &audioProcessor.getFilterResonanceParam(1) } },
                                          std::array<juce::AudioParameterChoice*, kFilterInstanceCount> { { &audioProcessor.getFilterTypeParam(0), &audioProcessor.getFilterTypeParam(1) } },
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
                                        reverbBypassButton,
                                        reverbKnob,
                                        reverbLabel,
                                        reverbTypeBox,
                                        reverbTypeLabel,
                                        juce::Colour::fromRGB(120, 186, 255));
    mixPanel = std::make_unique<MixPanel>(subOscLevelKnob,
                                          subOscLevelLabel,
                                          osc1LevelFader,
                                          osc1LevelLabel,
                                          osc2LevelFader,
                                          osc2LevelLabel,
                                          osc3LevelFader,
                                          osc3LevelLabel,
                                          juce::Colour::fromRGB(212, 212, 212));

    addAndMakeVisible(*oscPanel);
    addAndMakeVisible(*envPanel);
    addAndMakeVisible(*fltPanel);
    addAndMakeVisible(*fxPanel);
    addAndMakeVisible(*mixPanel);
    fxPanel->addMouseListener(this, true);

    for (auto& binding : knobBindings)
    {
        attachSlider(*binding.parameter, *binding.slider);
    }

    attachSlider(audioProcessor.getVibeAmountParam(), vibeAmountKnob);
    attachSlider(audioProcessor.getDelayAmountParam(), isaacTextureKnob);
    attachSlider(audioProcessor.getDelayTimeParam(), delayTimeKnob);
    attachSlider(audioProcessor.getDelayFeedbackParam(), delayFeedbackKnob);
    attachSlider(audioProcessor.getReverbAmountParam(), reverbKnob);
    attachSlider(audioProcessor.getSubOscLevelParam(), subOscLevelKnob);
    attachSlider(audioProcessor.getOscillatorLevelParam(0), osc1LevelFader);
    attachSlider(audioProcessor.getOscillatorLevelParam(1), osc2LevelFader);
    attachSlider(audioProcessor.getOscillatorLevelParam(2), osc3LevelFader);

    attachComboBox(audioProcessor.getFilterTypeParam(0), filterTypeBox);
    attachComboBox(audioProcessor.getFilterTypeParam(1), filter2TypeBox);
    attachComboBox(audioProcessor.getOscillatorModeParam(0), oscModeBox);
    attachComboBox(audioProcessor.getOscillatorVowelParam(0), oscVowelBox);
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
    attachComboBox(audioProcessor.getReverbAlgorithmParam(), reverbTypeBox);
    attachComboBox(audioProcessor.getVibeTypeParam(), vibeTypeBox);

    attachButton(audioProcessor.getVibeEnabledParam(), robBypassButton);
    attachButton(audioProcessor.getDelayEnabledParam(), delayBypassButton);
    attachButton(audioProcessor.getReverbEnabledParam(), reverbBypassButton);
    attachButton(audioProcessor.getFilterEnabledParam(0), filter1EnabledButton);
    attachButton(audioProcessor.getFilterEnabledParam(1), filter2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(0), osc1EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(1), osc2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(2), osc3EnabledButton);
    attachButton(audioProcessor.getSubOscEnabledParam(), subOscEnabledButton);

    // MIDI status bar is temporarily disabled.
    // midiStatusLabel.setText("MIDI In: waiting for note...", juce::dontSendNotification);
    // midiStatusLabel.setJustificationType(juce::Justification::centred);
    // midiStatusLabel.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 172, 88));
    // midiStatusLabel.setFont(juce::FontOptions(14.0f));
    // midiStatusLabel.setInterceptsMouseClicks(false, false);
    // addAndMakeVisible(midiStatusLabel);

    topMenuBar = std::make_unique<TopMenuBar>();

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

        const auto next = (currentIndex - 1 + static_cast<int>(presetFiltered.size())) % static_cast<int>(presetFiltered.size());
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

        const auto next = (currentIndex + 1) % static_cast<int>(presetFiltered.size());
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
    addAndMakeVisible(debugPerformanceOverlayLabel);
    debugPerformanceOverlayLabel.toFront(false);
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

    if (fxPanel != nullptr)
    {
        fxPanel->removeMouseListener(this);
    }

    // Panels hold child components that reference editor-owned controls.
    // Tear panels down first to avoid shutdown-order lifetime hazards.
    oscPanel.reset();
    envPanel.reset();
    fltPanel.reset();
    fxPanel.reset();
    mixPanel.reset();

    closeDebugWindow();
    audioProcessor.debugNotifyEditorDestroyed(this);

    for (auto& binding : knobBindings)
    {
        binding.slider->setLookAndFeel(nullptr);
    }

    vibeAmountKnob.setLookAndFeel(nullptr);
    isaacTextureKnob.setLookAndFeel(nullptr);
    delayTimeKnob.setLookAndFeel(nullptr);
    delayFeedbackKnob.setLookAndFeel(nullptr);
    reverbKnob.setLookAndFeel(nullptr);
}

void PX3SynthAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto bg = uiConfig != nullptr
                        ? uiConfig->getColour("editor.background.baseColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                        : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A);
    const auto stripRadius = uiConfig != nullptr ? uiConfig->getFloat("editor.topStrip.cornerRadius", 12.0f) : 12.0f;
    g.fillAll(bg);

    g.setColour(uiConfig != nullptr
                    ? uiConfig->getColour("editor.topStrip.fillColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                    : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(topMenuStripArea.toFloat(), stripRadius);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 44));
    g.drawRoundedRectangle(topMenuStripArea.toFloat(), stripRadius, 1.0f);
    g.setColour(juce::Colour::fromRGBA(255, 255, 255, 30));
    g.drawLine(static_cast<float>(logoPanelArea.getRight() + 6),
               static_cast<float>(topMenuStripArea.getY() + 10),
               static_cast<float>(logoPanelArea.getRight() + 6),
               static_cast<float>(topMenuStripArea.getBottom() - 10),
               1.0f);
    g.drawLine(static_cast<float>(topMenuPresetClusterArea.getRight() + 6),
               static_cast<float>(topMenuStripArea.getY() + 10),
               static_cast<float>(topMenuPresetClusterArea.getRight() + 6),
               static_cast<float>(topMenuStripArea.getBottom() - 10),
               1.0f);
    g.drawLine(static_cast<float>(topMenuSectionButtonsArea.getRight() + 4),
               static_cast<float>(topMenuStripArea.getY() + 10),
               static_cast<float>(topMenuSectionButtonsArea.getRight() + 4),
               static_cast<float>(topMenuStripArea.getBottom() - 10),
               1.0f);
    g.drawLine(static_cast<float>(topMenuMenuButtonArea.getX() - 5),
               static_cast<float>(topMenuStripArea.getY() + 10),
               static_cast<float>(topMenuMenuButtonArea.getX() - 5),
               static_cast<float>(topMenuStripArea.getBottom() - 10),
               1.0f);

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

    g.setColour(uiConfig != nullptr
                    ? uiConfig->getColour("editor.logo.fillColour", juce::Colour::fromRGB(0x1A, 0x1A, 0x1A))
                    : juce::Colour::fromRGB(0x1A, 0x1A, 0x1A));
    g.fillRoundedRectangle(logoPanelArea.toFloat(), stripRadius);

    if (logoFrame.isValid())
    {
                const auto subtitleHeight = 18.0f;
                const auto subtitleGap = 4.0f;
                const auto logoSize = static_cast<float>(juce::jlimit(80, 120, logoPanelArea.getHeight() - 46));
                const auto contentHeight = logoSize + subtitleGap + subtitleHeight;
                const auto contentTop = static_cast<float>(logoPanelArea.getY())
                                                                + (static_cast<float>(logoPanelArea.getHeight()) - contentHeight) * 0.5f
                                                                + 2.0f;

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

                g.setColour(uiConfig != nullptr
                                ? uiConfig->getColour("editor.logo.subtitle.colour", juce::Colour::fromRGB(232, 232, 232))
                                : juce::Colour::fromRGB(232, 232, 232));
                g.setFont(juce::FontOptions(uiConfig != nullptr
                                                ? uiConfig->getFloat("editor.logo.subtitle.fontSize", 14.0f)
                                                : 14.0f));
                const auto subtitleArea = juce::Rectangle<int>(logoPanelArea.getX() + 10,
                                                                                                             static_cast<int>(std::round(logoArea.getBottom() + subtitleGap - 2.0f)),
                                                                                                             logoPanelArea.getWidth() - 20,
                                                                                                             static_cast<int>(subtitleHeight));
                g.drawText("Synth v" + px3::version::string(), subtitleArea, juce::Justification::centred);
    }

    if (isPanelVisible(3))
    {
        const auto fxOffset = fxPanel->getPosition();
        const auto robArea = robSectionArea.translated(fxOffset.x, fxOffset.y);
        const auto delayArea = isaacSectionArea.translated(fxOffset.x, fxOffset.y);
        const auto revArea = reverbSectionArea.translated(fxOffset.x, fxOffset.y);
        const auto vibeEnabled = audioProcessor.getVibeEnabledParam().get();
        const auto delayEnabled = audioProcessor.getDelayEnabledParam().get();
        const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

        g.setColour(vibeEnabled ? juce::Colour::fromRGBA(104, 194, 255, 35)
                       : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(robArea.toFloat(), 10.0f);
        g.setColour(vibeEnabled ? juce::Colour::fromRGBA(104, 194, 255, 180)
                       : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(robArea.toFloat(), 10.0f, 1.0f);

        g.setColour(delayEnabled ? juce::Colour::fromRGBA(255, 198, 110, 35)
                     : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(delayArea.toFloat(), 10.0f);
        g.setColour(delayEnabled ? juce::Colour::fromRGBA(255, 198, 110, 180)
                     : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(delayArea.toFloat(), 10.0f, 1.0f);

        g.setColour(reverbEnabled ? juce::Colour::fromRGBA(128, 208, 255, 30)
                      : juce::Colour::fromRGBA(120, 120, 120, 30));
        g.fillRoundedRectangle(revArea.toFloat(), 10.0f);
        g.setColour(reverbEnabled ? juce::Colour::fromRGBA(128, 208, 255, 150)
                      : juce::Colour::fromRGBA(150, 150, 150, 130));
        g.drawRoundedRectangle(revArea.toFloat(), 10.0f, 1.0f);

        g.setColour(vibeEnabled ? juce::Colour::fromRGB(240, 245, 255)
                       : juce::Colour::fromRGB(170, 170, 170));
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText("VIBE", robArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);

        g.setColour(delayEnabled ? juce::Colour::fromRGB(250, 244, 224)
                     : juce::Colour::fromRGB(170, 170, 170));
        g.drawText("DELAY", delayArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);

        g.setColour(reverbEnabled ? juce::Colour::fromRGB(224, 245, 255)
                      : juce::Colour::fromRGB(170, 170, 170));
        g.drawText("REVERB", revArea.withTrimmedTop(5).withHeight(18), juce::Justification::centred);
    }

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

}

void PX3SynthAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
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

void PX3SynthAudioProcessorEditor::resized()
{
    // setResizeLimits() can trigger resized() during construction before
    // extracted panel components are created.
    if (oscPanel == nullptr || envPanel == nullptr || fltPanel == nullptr || fxPanel == nullptr || mixPanel == nullptr)
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
    const auto keyboardHeight = juce::jlimit(106, 144, static_cast<int>(std::lround(static_cast<double>(getHeight()) * 0.15)));
    // const auto statusHeight = 36;
    const auto sectionGap = uiConfig != nullptr ? uiConfig->getInt("editor.layout.sectionGap", 10) : 10;

    headerArea = bounds.removeFromTop(headerHeight);
    topMenuStripArea = headerArea;

    auto topStripContent = topMenuStripArea.reduced(8, 8);
    const auto logoWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoPanelWidth", 150) : 150;
    logoPanelArea = topStripContent.removeFromLeft(logoWidth);
    topStripContent.removeFromLeft(10);

    const auto gainWidth = uiConfig != nullptr ? uiConfig->getInt("editor.layout.gainPanelWidth", 100) : 100;
    topMenuGainArea = topStripContent.removeFromRight(gainWidth);
    topStripContent.removeFromRight(10);

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

    auto gainArea = topMenuGainArea.reduced(9, 4);
    const auto gainKnobSize = juce::jlimit(46, 60, juce::jmin(gainArea.getWidth() - 6, gainArea.getHeight() - 22));
    gainKnob.setBounds(juce::Rectangle<int>(gainKnobSize, gainKnobSize)
                           .withCentre({ gainArea.getCentreX(), gainArea.getY() + gainKnobSize / 2 + 4 }));
    gainLabel.setBounds(gainArea.getX(), gainArea.getBottom() - 16, gainArea.getWidth(), 14);

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

    panelViewportArea = controlsArea.reduced(8, 8);
    oscPanel->setBounds(panelViewportArea);
    envPanel->setBounds(panelViewportArea);
    fltPanel->setBounds(panelViewportArea);
    fxPanel->setBounds(panelViewportArea);
    mixPanel->setBounds(panelViewportArea);

    layoutOscPanel();
    layoutEnvelopePanel();
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

#if PX3_DEBUG_PANEL
    constexpr int overlayWidth = 170;
    constexpr int overlayHeight = 20;
    constexpr int overlayMargin = 12;
    debugPerformanceOverlayArea = { overlayMargin,
                                    getHeight() - overlayHeight - overlayMargin,
                                    overlayWidth,
                                    overlayHeight };
    debugPerformanceOverlayLabel.setBounds(debugPerformanceOverlayArea);
    debugPerformanceOverlayLabel.toFront(false);
#endif

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
    if (fxPanel != nullptr)
    {
        fxPanel->setUIConfig(uiConfig);
    }
    if (envPanel != nullptr)
    {
        envPanel->setUIConfig(uiConfig);
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

    if (uiConfig != nullptr)
    {
        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        uiConfig->applyComboStyle(comboStyle, lfoWaveformBox);
        uiConfig->applyComboStyle(comboStyle, lfoAssignBox);
        uiConfig->applyComboStyle(comboStyle, subOscOctaveBox);
        uiConfig->applyComboStyle(comboStyle, subOscWaveformBox);
        uiConfig->applyComboStyle(comboStyle, vibeTypeBox);
        uiConfig->applyComboStyle(comboStyle, filterTypeBox);
        uiConfig->applyComboStyle(comboStyle, filter2TypeBox);
        uiConfig->applyComboStyle(comboStyle, oscModeBox);
        uiConfig->applyComboStyle(comboStyle, osc2ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc3ModeBox);
        uiConfig->applyComboStyle(comboStyle, oscVowelBox);
        uiConfig->applyComboStyle(comboStyle, osc2VowelBox);
        uiConfig->applyComboStyle(comboStyle, osc3VowelBox);
        uiConfig->applyComboStyle(comboStyle, delayAlgoBox);
        uiConfig->applyComboStyle(comboStyle, granularSyncBox);
        uiConfig->applyComboStyle(comboStyle, granularModeBox);
        uiConfig->applyComboStyle(comboStyle, reverbTypeBox);
    }

    resized();
    repaint();
}

void PX3SynthAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();

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
    if (logoPanelArea.contains(point))
    {
        logoClickArmed = true;
        logoMouseDownPoint = point;
        return;
    }

    if (!isPanelVisible(3))
    {
        return;
    }

    const auto sectionId = fxSectionAtPoint(point);
    if (sectionId < 0)
    {
        return;
    }

    if (auto* eventComponent = event.eventComponent)
    {
        if (dynamic_cast<juce::Slider*>(eventComponent) != nullptr
            || dynamic_cast<juce::ComboBox*>(eventComponent) != nullptr
            || dynamic_cast<juce::Button*>(eventComponent) != nullptr)
        {
            return;
        }
    }

    draggingFxSection = sectionId;
    pressedFxSection = sectionId;
    fxDragStartPoint = point;
    fxDragHasMoved = false;
    const auto localPoint = point - fxPanel->getPosition();
    draggingSectionOffsetX = static_cast<float>(localPoint.x) - fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)].getX();
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

    if (draggingFxSection < 0)
    {
        return;
    }

    if (!isPanelVisible(3))
    {
        return;
    }

    const auto localPoint = point - fxPanel->getPosition();
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
    auto newX = static_cast<float>(localPoint.x) - draggingSectionOffsetX;
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
    repaint(fxPanel->getBounds());
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
        if (logoPanelArea.contains(point))
        {
            juce::URL("https://px3px3.com").launchInDefaultBrowser();
        }
        return;
    }

    if (draggingFxSection < 0)
    {
        return;
    }

    if (!isPanelVisible(3))
    {
        draggingFxSection = -1;
        pressedFxSection = -1;
        fxDragHasMoved = false;
        return;
    }

    const auto releasePoint = point - fxPanel->getPosition();
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
                    auto& p = audioProcessor.getVibeEnabledParam();
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
    repaint(fxPanel->getBounds());
}

void PX3SynthAudioProcessorEditor::updateFxSectionTargets(const juce::Rectangle<int>& topArea, int topGap)
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

void PX3SynthAudioProcessorEditor::layoutFxSectionsFromCurrentAreas()
{
    robSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionDrive)].toNearestInt();
    isaacSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionDelay)].toNearestInt();
    reverbSectionArea = fxSectionCurrentAreas[static_cast<std::size_t>(kFxSectionReverb)].toNearestInt();

    if (fxPanel != nullptr)
    {
        fxPanel->setSectionBounds(robSectionArea, isaacSectionArea, reverbSectionArea);
    }
}

void PX3SynthAudioProcessorEditor::animateFxSections()
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
        repaint(fxPanel->getBounds());
    }
}

int PX3SynthAudioProcessorEditor::indexForFxSection(int sectionId) const
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

int PX3SynthAudioProcessorEditor::fxSectionAtPoint(juce::Point<int> point) const
{
    if (!isPanelVisible(3))
    {
        return -1;
    }

    const auto localPoint = point - fxPanel->getPosition();
    for (int sectionId = 0; sectionId < 3; ++sectionId)
    {
        if (fxSectionCurrentAreas[static_cast<std::size_t>(sectionId)].toNearestInt().contains(localPoint))
        {
            return sectionId;
        }
    }

    return -1;
}

void PX3SynthAudioProcessorEditor::moveFxSectionToSlot(int sectionId, int slotIndex)
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
    juce::String name = hasCurrentPreset ? currentPreset.metadata.name : juce::String("INIT");
    if (currentPresetDirty)
    {
        name << "*";
    }

    if (topMenuBar != nullptr)
    {
        topMenuBar->setPresetName(name);
    }
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
    presetBrowserBackdropSnapshot = {};
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
        debug
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
#endif

    if (topMenuBar == nullptr)
    {
        return;
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&topMenuBar->getPresetMenuButton()),
                       [this](int result)
                       {
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
    const auto clamped = juce::jlimit(0, 4, sectionIndex);
    selectedTopMenuSection = clamped;

    if (topMenuBar != nullptr)
    {
        topMenuBar->setSelectedSection(clamped);
    }

    updatePanelVisibility();

    if (clamped == 0)
    {
        refreshOscillatorModeUI();
        refreshLfoAssignmentUI();
        refreshLfoUI();
    }
    else if (clamped == 1)
    {
        refreshEnvelopeGraphUI();
    }
    else if (clamped == 2)
    {
        refreshFilterUI();
    }
    else if (clamped == 3)
    {
        refreshGranularModeUI();
        refreshFxBypassUI();
    }
    else if (clamped == 4)
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

void PX3SynthAudioProcessorEditor::refreshOscillatorModeUI()
{
    for (int oscIndex = 0; oscIndex < 3; ++oscIndex)
    {
        juce::ComboBox* modeBox = nullptr;
        juce::ComboBox* vowelBox = nullptr;

        if (oscIndex == 0)
        {
            modeBox = &oscModeBox;
            vowelBox = &oscVowelBox;
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

    // isaacTextureLabel.setText("INTENSITY", juce::dontSendNotification);
    // isaacTextureLabel.setTooltip("INTENSITY");
    // isaacTextureKnob.setTooltip("INTENSITY");
    delayFeedbackLabel.setTooltip("FEEDBACK");
    delayFeedbackKnob.setTooltip("FEEDBACK");
    delayTimeLabel.setTooltip(delayTimeLabel.getText());
    delayTimeKnob.setTooltip(delayTimeLabel.getText());

    repaint(isaacSectionArea);
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

void PX3SynthAudioProcessorEditor::refreshLfoFrequencyLabel()
{
    const auto hz = juce::jlimit(0.01f, 20.0f, audioProcessor.getLfoFrequencyParam().get());
    lfoFrequencyValueLabel.setText(juce::String(hz, 2) + " Hz", juce::dontSendNotification);
}

void PX3SynthAudioProcessorEditor::refreshLfoUI()
{
    refreshLfoFrequencyLabel();

    if (oscPanel != nullptr)
    {
        oscPanel->refreshLfoFromParameters(audioProcessor.getLfoFrequencyParam().get(),
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
    if (envPanel != nullptr)
    {
        envPanel->refreshFromParameters();
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
    return selectedTopMenuSection == juce::jlimit(0, 4, sectionIndex);
}

void PX3SynthAudioProcessorEditor::updatePanelVisibility()
{
    oscPanel->setVisible(isPanelVisible(0));
    envPanel->setVisible(isPanelVisible(1));
    fltPanel->setVisible(isPanelVisible(2));
    fxPanel->setVisible(isPanelVisible(3));
    mixPanel->setVisible(isPanelVisible(4));
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

void PX3SynthAudioProcessorEditor::layoutEnvelopePanel()
{
    if (envPanel != nullptr)
    {
        envPanel->resized();
    }
}

void PX3SynthAudioProcessorEditor::layoutFxPanel()
{
    const auto padX = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padX", 12) : 12;
    const auto padY = uiConfig != nullptr ? uiConfig->getInt("fx.panel.layout.padY", 10) : 10;
    auto panelArea = fxPanel->getLocalBounds().reduced(padX, padY);
    updateFxSectionTargets(panelArea, 12);
    layoutFxSectionsFromCurrentAreas();
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
    const auto reverbEnabled = audioProcessor.getReverbEnabledParam().get();

    robBypassButton.setToggleState(vibeEnabled, juce::dontSendNotification);
    delayBypassButton.setToggleState(delayEnabled, juce::dontSendNotification);
    reverbBypassButton.setToggleState(reverbEnabled, juce::dontSendNotification);

    if (fxPanel != nullptr)
    {
        fxPanel->setActive(vibeEnabled, delayEnabled, granularModeSelectable, reverbEnabled);
    }
}

void PX3SynthAudioProcessorEditor::timerCallback()
{
    loadUiConfig(false);

    const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto deltaSeconds = (lastAnimationTickSeconds > 0.0)
                                  ? static_cast<float>(nowSeconds - lastAnimationTickSeconds)
                                  : (1.0f / 30.0f);
    lastAnimationTickSeconds = nowSeconds;

    // Timer drives non-audio UI synchronization only. DSP state is never
    // computed here; this keeps audio-thread responsibilities isolated.
    if (isPanelVisible(3) && draggingFxSection < 0)
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
            repaint(fxPanel->getBounds());
        }
    }

    if (isPanelVisible(3))
    {
        animateFxSections();
        refreshGranularModeUI();
        refreshFxBypassUI();
    }

    if (isPanelVisible(0))
    {
        refreshOscillatorModeUI();
        refreshLfoAssignmentUI();
        refreshLfoUI();
        refreshSubOscUI();
    }
    else if (isPanelVisible(1))
    {
        refreshEnvelopeGraphUI();
    }
    else if (isPanelVisible(2))
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

    if (isPanelVisible(0) && oscPanel != nullptr)
    {
        oscPanel->advanceAnimation(0.09f, deltaSeconds);
    }

#if PX3_DEBUG_PANEL
    refreshDebugPerformanceOverlay();
#endif

    if (isPanelVisible(4) && mixPanel != nullptr)
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
    const auto activeInstances = juce::jmax(1, audioProcessor.debugGetActiveInstanceCount());
    const auto ramMb = juce::jlimit(0.0, 99999.0, processResidentMemoryMb() / static_cast<double>(activeInstances));
    debugPerformanceOverlayLabel.setText("CPU: " + juce::String(cpuPercent, 1) + "% | RAM: " + juce::String(ramMb, 1) + " MB",
                                         juce::dontSendNotification);
#endif
}
