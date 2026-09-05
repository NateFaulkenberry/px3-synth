// Building the editor: the constructor, in eleven pieces.
//
// Split out of PluginEditor.cpp, where it was a single 1,270-line function -
// 29% of that file, and the largest thing in the UI by a wide margin.
//
// Each of these is a contiguous run of the original, in its original order,
// and the constructor now calls them in that order. Nothing was reordered and
// nothing was rewritten: the split was cut at points where no local was live
// across the boundary, which is why the lambdas that configure groups of
// controls each sit wholly inside one of these.
//
// The order matters and is not obvious from reading them. configureWavetableControls()
// inside buildPanels() is a no-op unless the oscillator panel already exists,
// and finishConstruction() reads state from every panel. Both fail silently.
// EditorOrder_* in TestsEditorLayout.cpp is what stops a future reshuffle.

#include "PluginEditor.h"
#include "UpdateService.h"
#include "EditorSections.h"
#include "MacroLook.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "ModalBackdrop.h"
#include "RoundedRect.h"
#include "PX3Version.h"
#include "BinaryData.h"
#include "PluginProcessorInternals.h"
#include "WavetableLibrary.h"
#include "WavetableImporter.h"
#include "WavetableFactory.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

using namespace px3::ui;

namespace
{
// Both of these were file-local to PluginEditor.cpp and are used only here now:
// the accents by the panels that tint themselves, the hover overlay by the
// captions. They move with their only caller rather than into a header, which
// keeps them exactly as private as they were.
const std::array<juce::Colour, 4> kGroupAccents {
    juce::Colour::fromRGB(74, 153, 255),   // OSC: blue
    juce::Colour::fromRGB(255, 88, 88),    // FILTER: red
    juce::Colour::fromRGB(73, 222, 121),   // AMP ENV: green
    juce::Colour::fromRGB(186, 112, 255)   // LFO: purple
};

void enableLabelHoverOverlay(juce::Label& label, const juce::String& tooltipText = {})
{
    label.setInterceptsMouseClicks(true, false);
    const auto text = tooltipText.isNotEmpty() ? tooltipText : label.getText();
    if (text.isNotEmpty())
    {
        label.setTooltip(text);
    }
}
} // namespace


// The background, the logo, and the three colour-separation masks the glitch draws from.
void PX3SynthAudioProcessorEditor::buildImagesAndMasks()
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

}


// The keyboard and the performance strip, and what the editor does when they move.
void PX3SynthAudioProcessorEditor::buildKeyboardCallbacks()
{
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

}


// Every parameter knob: the bindings array, the knobs themselves, and the readouts under them.
void PX3SynthAudioProcessorEditor::buildParameterKnobs()
{
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

}


// The envelope and LFO controls, including the ADSR knobs the graph replaces.
void PX3SynthAudioProcessorEditor::buildEnvelopeAndLfoControls()
{
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
}


// The dropdowns: filter type, oscillator mode, vowel, and their captions.
void PX3SynthAudioProcessorEditor::buildSelectors()
{
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
    configureEffectKnob(moodMixKnob, moodMixLabel, "MIX", audioProcessor.getMoodMixParam());
    configureEffectKnob(moodClockKnob, moodClockLabel, "CLOCK", audioProcessor.getMoodClockParam());
    configureEffectKnob(moodWetTimeKnob, moodWetTimeLabel, "WET TIME", audioProcessor.getMoodWetTimeParam());
    configureEffectKnob(moodWetModifyKnob, moodWetModifyLabel, "WET MOD", audioProcessor.getMoodWetModifyParam());
    configureEffectKnob(moodLoopLengthKnob, moodLoopLengthLabel, "LOOP LEN", audioProcessor.getMoodLoopLengthParam());
    configureEffectKnob(moodLoopModifyKnob, moodLoopModifyLabel, "LOOP MOD", audioProcessor.getMoodLoopModifyParam());
    configureEffectKnob(moodFeedbackKnob, moodFeedbackLabel, "FEEDBACK", audioProcessor.getMoodFeedbackParam());
    configureEffectKnob(moodSpreadKnob, moodSpreadLabel, "SPREAD", audioProcessor.getMoodSpreadParam());
    configureEffectKnob(moodDegradeKnob, moodDegradeLabel, "DEGRADE", audioProcessor.getMoodDegradeParam());


}


// The effect controls - delay, reverb, mood - and the bypass chips that head their cards.
void PX3SynthAudioProcessorEditor::buildEffectControls()
{
    // Compact labels and tooltips keep delay controls readable in narrow layouts.
    isaacTextureLabel.getProperties().set("compactLabel", true);
    delayTimeLabel.getProperties().set("compactLabel", true);
    delayFeedbackLabel.getProperties().set("compactLabel", true);
    delayFeedbackLabel.setTooltip("FEEDBACK");
    delayFeedbackKnob.setTooltip("FEEDBACK");

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

}


// The six panels, the macro strip and the assign overlay.
void PX3SynthAudioProcessorEditor::buildPanels()
{
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

    macroStrip = std::make_unique<MacroStrip>(audioProcessor, &macroKnobLookAndFeel);

    // The depth button under each knob is a toggle: it opens that macro's
    // panel, and closes it if it is the one already open. Same button,
    // both directions, so the way out is where the way in was.
    macroStrip->onDepthToggled = [this](int macro)
    {
        if (depthPanelMacroIndex() == macro) { closeMacroDepthPanel(); }
        else                                 { openMacroDepthPanel(macro); }
    };
    addAndMakeVisible(*macroStrip);

    macroAssignOverlay = std::make_unique<MacroAssignOverlay>(*this);
    addChildComponent(*macroAssignOverlay);

    // The depth panel and the scrim that dismisses it. Both start hidden; the
    // scrim is added first so the panel is above it whichever order they are
    // brought to front in.
    addChildComponent(macroDepthScrim);
    macroDepthPanel = std::make_unique<px3::ui::MacroDepthPanel>(audioProcessor);
    macroDepthPanel->onCloseRequested = [this] { closeMacroDepthPanel(); };
    addChildComponent(*macroDepthPanel);

    ampPanel = std::make_unique<AmpPanel>(audioProcessor, kGroupAccents[2]);
    ampPanel->setKnobLookAndFeel(&knobLookAndFeel);
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
                                        juce::Colour::fromRGB(120, 186, 255));
    mixPanel = std::make_unique<MixPanel>(audioProcessor,
                                          &knobLookAndFeel,
                                          juce::Colour::fromRGB(212, 212, 212));

}


// The settings page, the bus insert overlays, and which panel starts visible.
void PX3SynthAudioProcessorEditor::buildSettingsAndOverlays()
{
    settingsPanel = std::make_unique<SettingsPanel>(audioProcessor,
                                                    juce::Colour::fromRGB(190, 196, 206));
    settingsPanel->onCloseRequested = [this]() { toggleSettingsView(); };
    addAndMakeVisible(*settingsPanel);

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
    buildReverbCard();
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
    attachComboBox(audioProcessor.getVibeTypeParam(), vibeTypeBox);

    attachButton(audioProcessor.getVibeEnabledParam(), robBypassButton);
    attachButton(audioProcessor.getDelayEnabledParam(), delayBypassButton);
    attachButton(audioProcessor.getMoodEnabledParam(), moodBypassButton);
    attachButton(audioProcessor.getMoodFreezeParam(), moodFreezeButton);
    attachButton(audioProcessor.getFilterEnabledParam(0), filter1EnabledButton);
    attachButton(audioProcessor.getFilterEnabledParam(1), filter2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(0), osc1EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(1), osc2EnabledButton);
    attachButton(audioProcessor.getOscillatorEnabledParam(2), osc3EnabledButton);
    attachButton(audioProcessor.getSubOscEnabledParam(), subOscEnabledButton);
    attachButton(audioProcessor.getLfoEnabledParam(), lfoBypassButton);

    // MIDI status bar is temporarily disabled.

}


// The section row, the gear, and the master gain that sit above every panel.
void PX3SynthAudioProcessorEditor::buildTopMenuBar()
{
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
    topMenuBar->setOnSettings([this]() { toggleSettingsView(); });
    topMenuBar->setOnSectionSelected([this](int sectionIndex)
    {
        applyTopMenuSectionSelection(sectionIndex, true);
    });

    addAndMakeVisible(*topMenuBar);

}


// The preset selector, its buttons, and the browser sheet behind them.
void PX3SynthAudioProcessorEditor::buildPresetBar()
{
    const auto setupPresetButton = [](juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGBA(40, 40, 40, 210));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(232, 232, 232));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGBA(68, 124, 180, 220));
    };

    applyTopMenuSectionSelection(audioProcessor.getTopMenuViewIndex(), false);

    // The order these two are added does NOT decide which ends up on top.
    // openPresetBrowser() raises the scrim and then the panel with explicit
    // toFront calls every time it opens, so that is where the z-order is
    // decided and Editor_PresetSheetItselfStaysClickable is what holds it.
    // This comment used to claim the opposite; splitting the constructor is a
    // good moment to stop believing it.
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
    // CANCEL rather than CLOSE: it sits beside LOAD, and the pair reads as the
    // choice actually being made - take this preset, or take none.
    presetBrowserCloseButton.setButtonText("CANCEL");
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

    // Same action, in the corner where every other panel now keeps it.
    presetBrowserCloseGlyph.onClick = [this]() { closePresetBrowser(); };
    presetBrowserPanel.addAndMakeVisible(presetBrowserCloseGlyph);
    presetBrowserLoadButton.onClick = [this]()
    {
        loadPresetRow(presetListBox.getSelectedRow());
    };

}


// First-frame state, the listeners, and the timer. Everything here needs the whole editor to exist.
void PX3SynthAudioProcessorEditor::finishConstruction()
{
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
    //
    // The editor takes the CURRENT global preference on open - which is how a
    // window opened after the setting changed comes up matching the others -
    // and is told about every change after that.
    px3::GlobalSettings::getInstance().addChangeListener(&animationPreferenceListener);
    applyAnimationPreference();

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

    // Select Mode ends when the assignment lands, without polling for it. The
    // processor OUTLIVES this editor, so the destructor clears this again -
    // a std::function holding `this` on an object that survives us is a
    // dangling call waiting for the next CC.
    audioProcessor.onMidiMappingAssigned = [this](int)
    {
        midiSelection.clear();
        refreshMidiMappingUI();
    };

    setWantsKeyboardFocus(true);

    // ---- updates -----------------------------------------------------------
    //
    // The gear glows when there is something to see, and a line under the bar
    // says so once. Both are views of UpdateService; the editor keeps no copy
    // of whether an update exists.
    px3::update::installDefaultConfiguration();
    px3::update::UpdateService::getInstance().addChangeListener(&updateStateListener);

    updateNotice.setJustificationType(juce::Justification::centredLeft);
    // The notice EATS clicks rather than passing them through.
    //
    // It hangs over the header, so anything it covers is a real control - and a
    // click aimed at the notice that lands on an oscillator's bypass instead is
    // a change to the patch nobody asked for. Blocking is the safe way round: a
    // click on a notice should do nothing, not something invisible.
    //
    // Both arguments true, so its children - the close glyph - still get theirs.
    updateNotice.setInterceptsMouseClicks(true, true);
    addChildComponent(updateNotice);

    // Dismissing by hand ends the announcement exactly as the timeout does,
    // glow included - the point of closing it is to stop being told.
    updateNoticeCloseButton.onClick = [this]() { dismissUpdateNotice(); };
    updateNotice.addAndMakeVisible(updateNoticeCloseButton);

    // A check when a window opens. Throttled by the service, so opening and
    // closing an editor repeatedly is still one request every ten minutes -
    // and asynchronous, so nothing here waits for a network.
    px3::update::UpdateService::getInstance().checkForUpdates();
    refreshUpdateAffordances();

    startTimerHz(30);
}
