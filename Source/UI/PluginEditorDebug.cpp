#include "PluginEditor.h"
#include "../DSP/PluginProcessorInternals.h"

#include "PX3Version.h"

#include <algorithm>
#include <functional>
#include <random>

namespace
{
constexpr int kFxSectionDrive = 0;
constexpr int kFxSectionDelay = 1;
constexpr int kFxSectionReverb = 2;
constexpr int kFxSectionMood = 3;

void setDebugTextStable(juce::TextEditor& editor,
                        const juce::String& text,
                        bool freezeWhileInteracting)
{
    if (editor.getText() == text)
    {
        return;
    }

    if (freezeWhileInteracting && (editor.hasKeyboardFocus(true) || editor.isMouseOverOrDragging(true)))
    {
        return;
    }

    const auto previousCaret = editor.getCaretPosition();
    const auto previousCharCount = editor.getTotalNumChars();
    const auto wasAtEnd = previousCaret >= juce::jmax(0, previousCharCount - 1);

    editor.setText(text, juce::dontSendNotification);

    if (!wasAtEnd)
    {
        const auto clamped = juce::jlimit(0, editor.getTotalNumChars(), previousCaret);
        editor.setCaretPosition(clamped);
    }
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

void PX3SynthAudioProcessorEditor::setupDebugPanel()
{
    // The debug console is intentionally detached from the main editor surface
    // so frequent diagnostic refreshes do not clutter or stall normal UI work.
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
    setupButton(debugOrderAButton, "DELAY -> REVERB -> VIBE");
    setupButton(debugOrderBButton, "REVERB -> VIBE -> DELAY");
    setupButton(debugOrderCButton, "VIBE -> DELAY -> REVERB");
    setupButton(debugInvalidOrderButton, "TEST INVALID MODULE ORDER");
    setupButton(debugRandomizeParamsButton, "RANDOMIZE PARAMETERS");
    setupButton(debugResetParamsButton, "RESET PARAMETERS");
    setupButton(debugWriteTestValuesButton, "WRITE TEST VALUES");
    setupButton(debugDumpPresetButton, "DUMP PRESET");

    setupLabel(debugInstanceLabel, "A. PLUGIN INSTANCE INFO");
    setupLabel(debugModuleOrderLabel, "B. MODULE ORDER STATE");
    setupLabel(debugValueTreeLabel, "C. VALUETREE STATE");
    setupLabel(debugBackendControlLabel, "D. ANALOG ENGINE");
    setupLabel(debugParameterLabel, "I. PARAMETER STATE");
    setupLabel(debugSerializedLabel, "F. VIBE / ANALOG IMPERFECTIONS");
    setupLabel(debugLfoLabel, "G. LFO DEBUG");
    setupLabel(debugEnvelopeLabel, "H. AMP ENVELOPE DEBUG");
    setupLabel(debugPresetToolsLabel, "E. PRESET / STATE TOOLS");
    setupLabel(debugSnapshotLabel, "J. STATE TESTING");
    setupLabel(debugEventLogLabel, "K. EVENT LOG");

    setupEditor(debugInstanceText);
    setupEditor(debugModuleOrderText);
    setupEditor(debugValueTreeText);
    // Kept as a sink for the serialization dump - section D now shows the
    // analog console instead, and the XML section already covers this content.
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

    // Author and category, so a dumped preset arrives with the metadata the
    // browser sorts and credits by rather than inheriting whatever happened to
    // be loaded.
    const auto styleFieldLabel = [](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(236, 236, 236));
        label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        label.setJustificationType(juce::Justification::centredLeft);
    };

    const auto styleFieldEditor = [](juce::TextEditor& editor, const juce::String& placeholder)
    {
        editor.setMultiLine(false);
        editor.setReturnKeyStartsNewLine(false);
        editor.setReadOnly(false);
        editor.setTextToShowWhenEmpty(placeholder, juce::Colour::fromRGBA(220, 220, 220, 130));
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour::fromRGBA(14, 14, 14, 220));
        editor.setColour(juce::TextEditor::textColourId, juce::Colour::fromRGB(232, 232, 232));
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colour::fromRGBA(255, 255, 255, 100));
        editor.setFont(juce::FontOptions(11.0f));
    };

    styleFieldLabel(debugDumpPresetAuthorLabel, "Author");
    styleFieldLabel(debugDumpPresetCategoryLabel, "Category");
    styleFieldEditor(debugDumpPresetAuthorEditor, "Required");

    // Both fields are required now, so the name says so rather than claiming
    // to be optional.
    debugDumpPresetNameEditor.setTextToShowWhenEmpty("Required",
                                                     juce::Colour::fromRGBA(220, 220, 220, 130));

    // The categories that actually exist, from the library itself - a
    // hard-coded list here would drift from the browser the moment one is
    // added.
    {
        juce::StringArray categoryNames;
        for (const auto& category : presetManager.getAllCategories())
        {
            categoryNames.add(category);
        }
        debugDumpPresetCategoryBox.addItemList(categoryNames, 1);
    }
    debugDumpPresetCategoryBox.setSelectedId(1, juce::dontSendNotification);
    debugDumpPresetCategoryBox.setColour(juce::ComboBox::backgroundColourId,
                                         juce::Colour::fromRGBA(14, 14, 14, 220));
    debugDumpPresetCategoryBox.setColour(juce::ComboBox::textColourId,
                                         juce::Colour::fromRGB(232, 232, 232));
    debugDumpPresetCategoryBox.setColour(juce::ComboBox::outlineColourId,
                                         juce::Colour::fromRGBA(255, 255, 255, 100));

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

    // Anything that scrolls belongs to the content component, not the panel.
    const auto addToSections = [this](juce::Component& c)
    {
        debugSectionsContent.addAndMakeVisible(c);
    };

    debugSectionsViewport.setViewedComponent(&debugSectionsContent, false);
    debugSectionsViewport.setScrollBarsShown(true, false);
    addToPanel(debugSectionsViewport);

    addToPanel(debugPanelTitle);
    // The CPU/RAM readout. It used to float over the plugin editor itself,
    // covering whatever was in the bottom-left corner; it belongs with the rest
    // of the diagnostics.
    addToPanel(debugPerformanceOverlayLabel);
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
    addToSections(debugInstanceLabel);
    addToSections(debugModuleOrderLabel);
    addToSections(debugValueTreeLabel);
    buildAnalogEngineDebugControls();
    debugAnalogViewport.setViewedComponent(&debugAnalogContent, false);
    debugAnalogViewport.setScrollBarsShown(true, false);
    addToSections(debugAnalogViewport);

    addToSections(debugSerializedLabel);
    addToSections(debugParameterLabel);
    addToSections(debugBackendControlLabel);
    addToSections(debugLfoLabel);
    addToSections(debugEnvelopeLabel);
    addToSections(debugPresetToolsLabel);
    addToSections(debugDumpPresetNameLabel);
    addToSections(debugDumpPresetAuthorLabel);
    addToSections(debugDumpPresetCategoryLabel);
    addToSections(debugSnapshotLabel);
    addToSections(debugEventLogLabel);
    addToSections(debugInstanceText);
    addToSections(debugModuleOrderText);
    addToSections(debugValueTreeText);
    addToSections(debugParameterInspectorText);
    addToSections(debugEventLogText);
    addToSections(debugSnapshotText);
    addToSections(debugLfoText);
    addToSections(debugEnvelopeText);
    addToSections(debugDumpPresetNameEditor);
    addToSections(debugDumpPresetAuthorEditor);
    addToSections(debugDumpPresetCategoryBox);
    addToSections(debugLfoAssignLabel);
    addToSections(debugLfoAssignBox);
    addToSections(debugDumpPresetButton);
    addToSections(debugParamViewport);

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

    // Live, on every keystroke: a button that only notices what you typed once
    // you click elsewhere reads as broken.
    debugDumpPresetNameEditor.onTextChange = [this]() { refreshDebugDumpPresetEnablement(); };
    debugDumpPresetAuthorEditor.onTextChange = [this]() { refreshDebugDumpPresetEnablement(); };
    refreshDebugDumpPresetEnablement();
    debugRestoreLastSerializedButton.onClick = [this]()
    {
        juce::String report;
        audioProcessor.debugRestoreLastSerializedState(report);
        debugSnapshotText.setText(report, juce::dontSendNotification);
        applyFxChainOrder(audioProcessor.getFxProcessingOrder(), "DEBUG_PANEL", "RESTORE_SERIALIZED", -1, -1);
        resized();
        refreshDebugPanel(true);
    };

    debugSnapshotButton.onClick = [this]() { debugCaptureSnapshot("SNAPSHOT_BUTTON"); };
    debugCompareSnapshotButton.onClick = [this]() { debugCompareWithSnapshot(); };

    debugResetOrderButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDrive, kFxSectionDelay, kFxSectionMood, kFxSectionReverb } }, "DEBUG_RESET_ORDER");
    };
    debugOrderAButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDelay, kFxSectionMood, kFxSectionReverb, kFxSectionDrive } }, "DEBUG_ORDER_DELAY_MOOD_REVERB_DRIVE");
    };
    debugOrderBButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionReverb, kFxSectionDrive, kFxSectionDelay, kFxSectionMood } }, "DEBUG_ORDER_REVERB_DRIVE_DELAY_MOOD");
    };
    debugOrderCButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionMood, kFxSectionDrive, kFxSectionDelay, kFxSectionReverb } }, "DEBUG_ORDER_MOOD_DRIVE_DELAY_REVERB");
    };
    debugInvalidOrderButton.onClick = [this]()
    {
        debugApplyModuleOrder({ { kFxSectionDelay, kFxSectionDelay, kFxSectionMood, kFxSectionReverb } }, "DEBUG_INVALID_ORDER_TEST");
    };

    debugRandomizeParamsButton.onClick = [this]() { debugRandomizeParameters(); };
    debugResetParamsButton.onClick = [this]() { debugResetParameters(); };
    debugWriteTestValuesButton.onClick = [this]() { debugWriteDeterministicTestValues(); };

    struct VibeControlSpec
    {
        const char* title;
        const char* key;
        double min;
        double max;
        double step;
        double initial;
    };

    const auto initialTuning = audioProcessor.debugGetVibeTuning();
    const std::array<VibeControlSpec, 12> specs {
        {
            { "Global Amount", "globalAmount", 0.0, 1.0, 0.0001, audioProcessor.getVibeAmountParam().get() },
            { "Bypass", "bypass", 0.0, 1.0, 1.0, audioProcessor.debugGetVibeBypass() ? 1.0 : 0.0 },
            { "Seed", "seed", 1.0, 65535.0, 1.0, static_cast<double>(audioProcessor.debugGetVibeSeed()) },
            { "Oscillator Drift", "oscillatorDrift", 0.0, 1.0, 0.0001, initialTuning.oscillatorDrift },
            { "Voice Variation", "voiceVariation", 0.0, 1.0, 0.0001, initialTuning.voiceVariation },
            { "Filter Variation", "filterVariation", 0.0, 1.0, 0.0001, initialTuning.filterVariation },
            { "Saturation", "saturation", 0.0, 1.0, 0.0001, initialTuning.saturation },
            { "Noise", "noise", 0.0, 1.0, 0.0001, initialTuning.noise },
            { "PSU Movement", "psuMovement", 0.0, 1.0, 0.0001, initialTuning.psuMovement },
            { "VCA Nonlinearity", "vcaNonlinearity", 0.0, 1.0, 0.0001, initialTuning.vcaNonlinearity },
            { "Waveform Asymmetry", "waveformAsymmetry", 0.0, 1.0, 0.0001, initialTuning.waveformAsymmetry },
            { "Temperature Drift", "temperatureDrift", 0.0, 1.0, 0.0001, initialTuning.temperatureDrift }
        }
    };

    for (const auto& spec : specs)
    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = spec.key;
        control->label.setText(spec.title, juce::dontSendNotification);
        control->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
        control->label.setFont(juce::FontOptions(11.0f));

        control->slider.setRange(spec.min, spec.max, spec.step);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(spec.initial, juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }

            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;

            if (ptr->key == "globalAmount")
            {
                auto& p = audioProcessor.getVibeAmountParam();
                p.beginChangeGesture();
                p.setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, requested));
                p.endChangeGesture();
            }
            else if (ptr->key == "bypass")
            {
                auto& p = audioProcessor.getVibeEnabledParam();
                p.beginChangeGesture();
                p.setValueNotifyingHost(requested >= 0.5f ? 0.0f : 1.0f);
                p.endChangeGesture();
            }
            else if (ptr->key == "seed")
            {
                audioProcessor.debugSetVibeSeed(static_cast<uint32_t>(juce::jmax(1, static_cast<int>(std::lround(requested)))));
            }
            else
            {
                audioProcessor.debugSetVibeTuningValue(ptr->key, requested);
            }
        };

        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugParamContent.addAndMakeVisible(control->label);
        debugParamContent.addAndMakeVisible(control->slider);
        debugParamContent.addAndMakeVisible(control->readback);
        debugParamControls.push_back(std::move(control));
    }

    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "correlatedChaos";
        control->label.setText("Correlated Chaos", juce::dontSendNotification);
        control->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
        control->label.setFont(juce::FontOptions(11.0f));
        control->slider.setRange(0.0, 1.0, 0.0001);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(initialTuning.correlatedChaos, juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }
            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;
            audioProcessor.debugSetVibeTuningValue(ptr->key, requested);
        };
        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugParamContent.addAndMakeVisible(control->label);
        debugParamContent.addAndMakeVisible(control->slider);
        debugParamContent.addAndMakeVisible(control->readback);
        debugParamControls.push_back(std::move(control));
    }

    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "fxSendGain";
        control->label.setText("FX Send Gain", juce::dontSendNotification);
        control->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
        control->label.setFont(juce::FontOptions(11.0f));
        control->slider.setRange(0.0, 1.0, 0.0001);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(audioProcessor.getFxSendGainParam().get(), juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }

            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;
            auto& p = audioProcessor.getFxSendGainParam();
            p.beginChangeGesture();
            p.setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, requested));
            p.endChangeGesture();
        };
        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugParamContent.addAndMakeVisible(control->label);
        debugParamContent.addAndMakeVisible(control->slider);
        debugParamContent.addAndMakeVisible(control->readback);
        debugParamControls.push_back(std::move(control));
    }

    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "fxReturnGain";
        control->label.setText("FX Return Gain", juce::dontSendNotification);
        control->label.setColour(juce::Label::textColourId, juce::Colour::fromRGB(230, 230, 230));
        control->label.setFont(juce::FontOptions(11.0f));
        control->slider.setRange(0.0, 1.0, 0.0001);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(audioProcessor.getFxReturnGainParam().get(), juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }

            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;
            auto& p = audioProcessor.getFxReturnGainParam();
            p.beginChangeGesture();
            p.setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, requested));
            p.endChangeGesture();
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

// ============================================================================
// AnalogEngine debug controls
// ============================================================================

void PX3SynthAudioProcessorEditor::buildAnalogEngineDebugControls()
{
    if (debugAnalogControlsInitialized)
    {
        return;
    }

    auto styleLabel = [](juce::Label& label, const juce::String& text, juce::Colour colour)
    {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, colour);
        label.setFont(juce::FontOptions(11.0f));
    };

    // ---- enable ------------------------------------------------------------
    // The engine ships disabled, so this is the first thing anyone needs.
    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "analog.enabled";
        styleLabel(control->label, "ANALOG ENGINE  (0 = off, 1 = on)",
                   juce::Colour::fromRGB(255, 214, 140));
        control->slider.setRange(0.0, 1.0, 1.0);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(audioProcessor.getAnalogEnabledParam().get() ? 1.0 : 0.0,
                                 juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }
            auto& p = audioProcessor.getAnalogEnabledParam();
            p.beginChangeGesture();
            p.setValueNotifyingHost(ptr->slider.getValue() >= 0.5 ? 1.0f : 0.0f);
            p.endChangeGesture();
        };
        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugAnalogContent.addAndMakeVisible(control->label);
        debugAnalogContent.addAndMakeVisible(control->slider);
        debugAnalogContent.addAndMakeVisible(control->readback);
        debugAnalogControls.push_back(std::move(control));
    }

    // ---- profile -----------------------------------------------------------
    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "analog.profile";
        styleLabel(control->label, "PROFILE", juce::Colour::fromRGB(255, 214, 140));

        control->box = std::make_unique<juce::ComboBox>();
        const auto names = px3::AnalogEngine::profileNames();
        for (int i = 0; i < names.size(); ++i)
        {
            control->box->addItem(names[i], i + 1);
        }
        control->box->setSelectedItemIndex(audioProcessor.getAnalogProfileParam().getIndex(),
                                           juce::dontSendNotification);
        control->box->onChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }
            auto& p = audioProcessor.getAnalogProfileParam();
            const auto index = juce::jmax(0, ptr->box->getSelectedItemIndex());
            p.beginChangeGesture();
            p.setValueNotifyingHost(p.convertTo0to1(static_cast<float>(index)));
            p.endChangeGesture();

            // Switching profile replaces the whole tuning set, so the sliders
            // below are now showing the previous profile's numbers. Pull them
            // back into step, and note that this discards any edits.
            refreshAnalogEngineDebugControls();
        };

        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugAnalogContent.addAndMakeVisible(control->label);
        debugAnalogContent.addAndMakeVisible(*control->box);
        debugAnalogContent.addAndMakeVisible(control->readback);
        debugAnalogControls.push_back(std::move(control));
    }

    // ---- reset -------------------------------------------------------------
    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = "analog.reset";
        styleLabel(control->label, "TUNING (internal - never saved to presets or DAW state)",
                   juce::Colour::fromRGB(255, 214, 140));

        control->button = std::make_unique<juce::TextButton>("RESET TO COMPILED DEFAULTS");
        control->button->onClick = [this]
        {
            audioProcessor.debugResetAnalogTuning();
            refreshAnalogEngineDebugControls();
        };

        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugAnalogContent.addAndMakeVisible(control->label);
        debugAnalogContent.addAndMakeVisible(*control->button);
        debugAnalogContent.addAndMakeVisible(control->readback);
        debugAnalogControls.push_back(std::move(control));
    }

    // ---- the tuning constants ----------------------------------------------
    // Ranges match the clamps in AnalogEngine::setTuningValue, so a slider
    // cannot ask for a value the engine will silently refuse.
    struct Spec
    {
        const char* key;
        const char* caption;
        double minimum;
        double maximum;
        double step;
    };

    static const std::array<Spec, 14> specs { {
        { "engineAmount",      "Engine Amount",          0.0,     1.0,     0.001 },
        { "pairDrive",         "Pair Drive (ch = bus)",  0.0,     3.0,     0.001 },
        { "masterDrive",       "Master Drive",           0.0,     3.0,     0.001 },
        { "fxBusTrim",         "FX Bus Trim (mix)",      0.0,     2.0,     0.001 },
        { "headroom",          "Headroom",               0.25,    3.0,     0.001 },
        { "curveBlend",        "Curve Blend (pre-warp)", 0.0,     1.0,     0.001 },
        { "evenHarmonic",      "Even Harmonic (2nd)",    0.0,     0.5,     0.0005 },
        { "slewEnhance",       "Slew Enhance",           0.0,     1.0,     0.001 },
        { "hfRolloffHz",       "HF Rolloff (Hz)",        1000.0,  22000.0, 10.0 },
        { "hfLevelDependence", "HF Level Dependence",    0.0,     1.0,     0.001 },
        { "lfCornerHz",        "LF Corner (Hz)",         1.0,     200.0,   0.5 },
        { "lfLevelTrim",       "LF Level Trim",          0.0,     1.0,     0.001 },
        { "dcBlockHz",         "DC Block (Hz)",          0.1,     50.0,    0.1 },
        { "outputTrim",        "Output Trim (per stage)", 0.25,   4.0,     0.001 },
    } };

    for (const auto& spec : specs)
    {
        auto control = std::make_unique<DebugParamControl>();
        control->key = spec.key;
        styleLabel(control->label, spec.caption, juce::Colour::fromRGB(230, 230, 230));
        control->slider.setRange(spec.minimum, spec.maximum, spec.step);
        control->slider.setSliderStyle(juce::Slider::LinearHorizontal);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 78, 18);
        control->slider.setScrollWheelEnabled(false);
        control->slider.setValue(audioProcessor.debugGetAnalogTuningValue(spec.key),
                                 juce::dontSendNotification);
        control->slider.onValueChange = [this, ptr = control.get()]
        {
            if (ptr->suppressCallbacks)
            {
                return;
            }
            const auto requested = static_cast<float>(ptr->slider.getValue());
            ptr->lastRequested = requested;
            audioProcessor.debugSetAnalogTuningValue(ptr->key, requested);
        };
        control->readback.setColour(juce::Label::textColourId, juce::Colour::fromRGB(184, 235, 184));
        control->readback.setFont(juce::FontOptions(10.0f));

        debugAnalogContent.addAndMakeVisible(control->label);
        debugAnalogContent.addAndMakeVisible(control->slider);
        debugAnalogContent.addAndMakeVisible(control->readback);
        debugAnalogControls.push_back(std::move(control));
    }

    debugAnalogControlsInitialized = true;
}

void PX3SynthAudioProcessorEditor::refreshAnalogEngineDebugControls()
{
    if (!debugAnalogControlsInitialized)
    {
        return;
    }

    for (auto& control : debugAnalogControls)
    {
        control->suppressCallbacks = true;

        if (control->key == "analog.enabled")
        {
            const auto on = audioProcessor.getAnalogEnabledParam().get();
            control->slider.setValue(on ? 1.0 : 0.0, juce::dontSendNotification);
            control->readback.setText(on ? "ACTIVE - channels, dry bus, FX bus and master"
                                         : "bypassed - the instrument is unchanged",
                                      juce::dontSendNotification);
        }
        else if (control->key == "analog.profile")
        {
            const auto index = audioProcessor.getAnalogProfileParam().getIndex();
            control->box->setSelectedItemIndex(index, juce::dontSendNotification);
            control->readback.setText("changing profile reloads its compiled tuning",
                                      juce::dontSendNotification);
        }
        else if (control->key == "analog.reset")
        {
            control->readback.setText("edits are live but never persisted",
                                      juce::dontSendNotification);
        }
        else
        {
            const auto actual = audioProcessor.debugGetAnalogTuningValue(control->key);
            control->slider.setValue(actual, juce::dontSendNotification);

            const auto compiled = px3::AnalogEngine::defaultTuningFor(
                static_cast<px3::AnalogEngine::Profile>(
                    juce::jlimit(0, px3::AnalogEngine::kProfileCount - 1,
                                 audioProcessor.getAnalogProfileParam().getIndex())));

            px3::AnalogEngine probe;
            probe.setProfile(static_cast<px3::AnalogEngine::Profile>(
                juce::jlimit(0, px3::AnalogEngine::kProfileCount - 1,
                             audioProcessor.getAnalogProfileParam().getIndex())));
            juce::ignoreUnused(compiled);
            const auto defaultValue = probe.getTuningValue(control->key);

            juce::String text;
            text << "Actual: " << juce::String(actual, 4);
            if (std::abs(actual - defaultValue) > 1.0e-6f)
            {
                text << "   (default " << juce::String(defaultValue, 4) << ")";
            }
            control->readback.setText(text, juce::dontSendNotification);
        }

        control->suppressCallbacks = false;
    }
}

void PX3SynthAudioProcessorEditor::openDebugWindow()
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

void PX3SynthAudioProcessorEditor::closeDebugWindow()
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

void PX3SynthAudioProcessorEditor::toggleDebugWindow()
{
    if (debugPanelVisible)
    {
        closeDebugWindow();
        return;
    }

    openDebugWindow();
}

void PX3SynthAudioProcessorEditor::layoutDebugPanel(const juce::Rectangle<int>& bounds)
{
    const auto previousViewPos = debugParamViewport.getViewPosition();

    debugPanel.setBounds(bounds);
    auto area = debugPanel.getLocalBounds().reduced(10);

    auto top = area.removeFromTop(30);
    debugPanelTitle.setBounds(top.removeFromLeft(320));
    debugPanelCloseButton.setBounds(top.removeFromRight(90));

    // In the gap the title row already has, between the title and the close
    // button - so it covers no control and no output pane. Taken from the
    // RIGHT of what is left, leaving a margin before the close button.
    top.removeFromRight(12);
    debugPerformanceOverlayLabel.setBounds(
        top.removeFromRight(juce::jmin(260, juce::jmax(0, top.getWidth()))));

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

    // Everything below the buttons scrolls. The content is laid out at its
    // NATURAL height and the viewport shows as much of it as the window has
    // room for, so a short window costs a scrollbar rather than hiding the
    // sections at the bottom.
    debugSectionsViewport.setBounds(area);

    // Each section is a label plus a body plus a gap. Summed per column so the
    // content knows how tall it has to be before anything is positioned - a
    // height that fell out of the layout afterwards would be one frame late.
    constexpr int labelHeight = 18;
    constexpr int gap = 4;
    auto stack = [](std::initializer_list<int> bodyHeights)
    {
        int total = 0;
        for (const auto h : bodyHeights) { total += labelHeight + h + gap; }
        return total;
    };

    const auto leftNaturalHeight = stack({ 78, 120, 80, 160 });
    const auto rightNaturalHeight = stack({ 24 + gap + 24, 120, 196, 66 + gap + 24, 180, 80, 120 });

    const auto viewportWidth = juce::jmax(0, debugSectionsViewport.getMaximumVisibleWidth());
    const auto contentHeight = juce::jmax(debugSectionsViewport.getMaximumVisibleHeight(),
                                          juce::jmax(leftNaturalHeight, rightNaturalHeight));
    debugSectionsContent.setBounds(0, 0, viewportWidth, contentHeight);

    auto sections = debugSectionsContent.getLocalBounds();
    auto left = sections.removeFromLeft(sections.getWidth() / 2);
    auto right = sections.withTrimmedLeft(8);

    auto section = [&left](juce::Label& label, juce::Component& component, int height)
    {
        label.setBounds(left.removeFromTop(18));
        component.setBounds(left.removeFromTop(height));
        left.removeFromTop(4);
    };

    section(debugInstanceLabel, debugInstanceText, 78);
    section(debugModuleOrderLabel, debugModuleOrderText, 120);
    section(debugValueTreeLabel, debugValueTreeText, 80);
    // Section D is the analog console. It replaced the serialization-events
    // dump, which duplicated what the XML section already showed.
    section(debugBackendControlLabel, debugAnalogViewport, juce::jmax(160, left.getHeight() - 24));

    {
        auto content = debugAnalogViewport.getLocalBounds().reduced(4);
        content.removeFromRight(12);
        int ay = 0;
        for (auto& control : debugAnalogControls)
        {
            control->label.setBounds(0, ay, content.getWidth(), 16);
            ay += 16;

            if (control->box != nullptr)
            {
                control->box->setBounds(0, ay, content.getWidth(), 22);
            }
            else if (control->button != nullptr)
            {
                control->button->setBounds(0, ay, content.getWidth(), 22);
            }
            else
            {
                control->slider.setBounds(0, ay, content.getWidth(), 22);
            }
            ay += 22;

            control->readback.setBounds(0, ay, content.getWidth(), 14);
            ay += 18;
        }
        debugAnalogContent.setBounds(0, 0, content.getWidth(),
                                     juce::jmax(content.getHeight(), ay));
    }

    auto sectionRight = [&right](juce::Label& label, juce::Component& component, int height)
    {
        label.setBounds(right.removeFromTop(18));
        component.setBounds(right.removeFromTop(height));
        right.removeFromTop(4);
    };

    // The preset dump is the section reached for most often, so it sits at the
    // top of the column instead of eight sections down. It and the parameter
    // inspector swapped places, letters included, so the headings still read in
    // order down the page.
    debugPresetToolsLabel.setBounds(right.removeFromTop(18));

    const auto fieldRow = [&right](juce::Label& label, juce::Component& field)
    {
        auto row = right.removeFromTop(24);
        label.setBounds(row.removeFromLeft(96));
        field.setBounds(row.reduced(1, 0));
        right.removeFromTop(4);
    };

    fieldRow(debugDumpPresetNameLabel, debugDumpPresetNameEditor);
    fieldRow(debugDumpPresetAuthorLabel, debugDumpPresetAuthorEditor);
    fieldRow(debugDumpPresetCategoryLabel, debugDumpPresetCategoryBox);
    debugDumpPresetButton.setBounds(right.removeFromTop(24));
    right.removeFromTop(4);

    sectionRight(debugSerializedLabel, debugParamViewport, 196);

    debugLfoLabel.setBounds(right.removeFromTop(18));
    debugLfoText.setBounds(right.removeFromTop(66));
    right.removeFromTop(4);
    auto lfoAssignRow = right.removeFromTop(24);
    debugLfoAssignLabel.setBounds(lfoAssignRow.removeFromLeft(130));
    debugLfoAssignBox.setBounds(lfoAssignRow.reduced(1, 0));
    right.removeFromTop(4);

    debugEnvelopeLabel.setBounds(right.removeFromTop(18));
    debugEnvelopeText.setBounds(right.removeFromTop(180));
    right.removeFromTop(4);

    sectionRight(debugParameterLabel, debugParameterInspectorText, 120);
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

void PX3SynthAudioProcessorEditor::refreshDebugPanel(bool includeHeavySections)
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

void PX3SynthAudioProcessorEditor::refreshDebugModuleState()
{
    const auto processor = describeProcessorOrder();
    const auto ui = describeUiOrder();
    const auto tree = describeStateTreeOrder();
    const auto mismatch = (processor != ui) || (processor != tree);

    const auto processorOrder = audioProcessor.getFxProcessingOrder();
    const auto treeOrder = readModuleOrderFromStateTree(audioProcessor.createParameterStateTree());
    using ModuleNames = std::array<juce::String, px3::kFxStageCount>;

    auto namesFor = [](const px3::FxOrder& order)
    {
        ModuleNames names {};
        for (int i = 0; i < px3::kFxStageCount; ++i)
        {
            names[static_cast<std::size_t>(i)] = fxModuleIdFromSection(order[static_cast<std::size_t>(i)]);
        }
        return names;
    };

    const auto uiOrderNames = namesFor(fxSectionOrder);
    const auto processorOrderNames = namesFor(processorOrder);

    const auto findPos = [](const juce::String& id, const ModuleNames& orderNames)
    {
        for (int i = 0; i < px3::kFxStageCount; ++i)
        {
            if (id.equalsIgnoreCase(orderNames[static_cast<std::size_t>(i)]))
            {
                return i;
            }
        }
        return -1;
    };

    juce::String text;
    text << "Processor: " << processor << "\n";
    text << "UI:        " << ui << "\n";
    text << "ValueTree: " << tree << "\n";
    text << "\nRaw Processor Array:\n";
    for (int i = 0; i < px3::kFxStageCount; ++i)
    {
        text << "[" << i << "] " << processorOrderNames[static_cast<std::size_t>(i)] << "\n";
    }
    text << "Raw UI Array:\n";
    for (int i = 0; i < px3::kFxStageCount; ++i)
    {
        text << "[" << i << "] " << uiOrderNames[static_cast<std::size_t>(i)] << "\n";
    }
    text << "Raw ValueTree MODULE_ORDER:\n";
    for (int i = 0; i < px3::kFxStageCount; ++i)
    {
        text << "module[" << i << "] = " << treeOrder[static_cast<std::size_t>(i)] << "\n";
    }
    text << "Generation: " << juce::String(static_cast<int64_t>(audioProcessor.debugGetModuleOrderGeneration())) << "\n";
    text << "Hash: " << juce::String(static_cast<int64_t>(audioProcessor.debugGetModuleOrderHash())) << "\n";

    const auto& modules = px3::processor_internal::kFxModuleIds;
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

void PX3SynthAudioProcessorEditor::refreshDebugValueTree()
{
    const auto state = audioProcessor.createParameterStateTree();
    if (auto xml = state.createXml())
    {
        debugValueTreeText.setText(xml->toString(), juce::dontSendNotification);
    }
}

void PX3SynthAudioProcessorEditor::refreshDebugSerializedState()
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

void PX3SynthAudioProcessorEditor::refreshDebugParameterInspector()
{
    setDebugTextStable(debugParameterInspectorText,
                       buildParameterInspectorText(),
                       true);
}

void PX3SynthAudioProcessorEditor::refreshDebugParameterControls()
{
    refreshAnalogEngineDebugControls();

    if (!debugParamControlsInitialized)
    {
        return;
    }

    for (auto& control : debugParamControls)
    {
        float actualValue = 0.0f;
        juce::String extra;

        if (control->key == "globalAmount")
        {
            actualValue = audioProcessor.debugGetVibeGlobalAmount();
            extra = " | Effective: " + juce::String(audioProcessor.debugGetVibeEffectiveAmount(), 5);
        }
        else if (control->key == "bypass")
        {
            actualValue = audioProcessor.debugGetVibeBypass() ? 1.0f : 0.0f;
        }
        else if (control->key == "seed")
        {
            actualValue = static_cast<float>(audioProcessor.debugGetVibeSeed());
        }
        else if (control->key == "fxSendGain")
        {
            actualValue = audioProcessor.getFxSendGainParam().get();
        }
        else if (control->key == "fxReturnGain")
        {
            actualValue = audioProcessor.getFxReturnGainParam().get();
        }
        else
        {
            actualValue = audioProcessor.debugGetVibeTuningValue(control->key);
        }

        control->suppressCallbacks = true;
        control->slider.setValue(actualValue, juce::dontSendNotification);
        control->suppressCallbacks = false;
        control->readback.setText("Requested: " + juce::String(control->lastRequested, 5)
                                  + " | Actual: " + juce::String(actualValue, 5)
                                  + extra,
                                  juce::dontSendNotification);
    }
}

void PX3SynthAudioProcessorEditor::refreshDebugEventLog()
{
    setDebugTextStable(debugEventLogText,
                       audioProcessor.debugGetEventLogText(),
                       true);
}

void PX3SynthAudioProcessorEditor::refreshDebugLfoState()
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
    const auto oscBusRms = audioProcessor.debugGetOscillatorBusRms();
    const auto dryBusRms = audioProcessor.debugGetDryBusRms();
    const auto fxBusRms = audioProcessor.debugGetFxBusRms();
    const auto masterBusRms = audioProcessor.debugGetMasterBusRms();
    const auto oscBusPeak = audioProcessor.debugGetOscillatorBusPeak();
    const auto dryBusPeak = audioProcessor.debugGetDryBusPeak();
    const auto fxBusPeak = audioProcessor.debugGetFxBusPeak();
    const auto masterBusPeak = audioProcessor.debugGetMasterBusPeak();
    const auto masterPreOutputPeak = audioProcessor.debugGetMasterPreOutputPeak();

    juce::String text;
    text << "Frequency: " << juce::String(frequencyHz, 4) << " Hz\n"
         << "Assignment: " << assignmentName << "\n"
         << "Assignment ID: " << assignmentId << "\n"
         << "Phase: " << juce::String(phase, 5) << "\n"
         << "LFO Value: " << juce::String(lfoValue, 5) << "\n"
         << "Base (norm): " << juce::String(baseNorm, 5) << "\n"
         << "Effective (norm): " << juce::String(effectiveNorm, 5) << "\n"
         << "Delta: " << juce::String(effectiveNorm - baseNorm, 5) << "\n\n"
         << "Bus RMS\n"
         << "Oscillator: " << juce::String(oscBusRms, 6) << "\n"
         << "Dry: " << juce::String(dryBusRms, 6) << "\n"
         << "FX: " << juce::String(fxBusRms, 6) << "\n"
         << "Master: " << juce::String(masterBusRms, 6) << "\n\n"
         << "Bus Peak\n"
         << "Oscillator: " << juce::String(oscBusPeak, 6) << "\n"
         << "Dry: " << juce::String(dryBusPeak, 6) << "\n"
         << "FX: " << juce::String(fxBusPeak, 6) << "\n"
         << "Master: " << juce::String(masterBusPeak, 6) << "\n"
         << "Master pre-output: " << juce::String(masterPreOutputPeak, 6);

    text << "\n\nLFO Sources\n";
    for (int i = 0; i < PX3SynthAudioProcessor::kLfoSourceCount; ++i)
    {
        text << "LFO " << juce::String(i + 1)
             << " value=" << juce::String(audioProcessor.debugGetLfoCurrentValue(i), 5)
             << " amount=" << juce::String(audioProcessor.getLfoAmountParam(i).get(), 4)
             << " assign=" << audioProcessor.getLfoAssignmentParameterId(i)
             << "\n";
    }

    setDebugTextStable(debugLfoText, text, true);
}

void PX3SynthAudioProcessorEditor::refreshDebugEnvelopeState()
{
    const auto attackSec = audioProcessor.getAttackParam().get();
    const auto decaySec = audioProcessor.getDecayParam().get();
    const auto sustainNorm = audioProcessor.getSustainParam().get();
    const auto releaseSec = audioProcessor.getReleaseParam().get();

    const auto attackText = attackSec < 1.0f ? juce::String(attackSec * 1000.0f, 1) + " ms" : juce::String(attackSec, 4) + " s";
    const auto decayText = decaySec < 1.0f ? juce::String(decaySec * 1000.0f, 1) + " ms" : juce::String(decaySec, 4) + " s";
    const auto releaseText = releaseSec < 1.0f ? juce::String(releaseSec * 1000.0f, 1) + " ms" : juce::String(releaseSec, 4) + " s";
     const auto ampEnabled = audioProcessor.getAmpEnvEnabledParam().get();

     const auto subEnabled = audioProcessor.getSubOscEnabledParam().get();
     const auto osc1Enabled = audioProcessor.getOscillatorEnabledParam(0).get();
     const auto osc2Enabled = audioProcessor.getOscillatorEnabledParam(1).get();
     const auto osc3Enabled = audioProcessor.getOscillatorEnabledParam(2).get();

     const auto subMixerLevel = audioProcessor.getMixerLevelParam(PX3SynthAudioProcessor::mixerSub).get();
     const auto osc1MixerLevel = audioProcessor.getMixerLevelParam(PX3SynthAudioProcessor::mixerOsc1).get();
     const auto osc2MixerLevel = audioProcessor.getMixerLevelParam(PX3SynthAudioProcessor::mixerOsc2).get();
     const auto osc3MixerLevel = audioProcessor.getMixerLevelParam(PX3SynthAudioProcessor::mixerOsc3).get();

     const auto subRms = audioProcessor.debugGetMixerSourceRms(PX3SynthAudioProcessor::mixerSub);
     const auto osc1Rms = audioProcessor.debugGetMixerSourceRms(PX3SynthAudioProcessor::mixerOsc1);
     const auto osc2Rms = audioProcessor.debugGetMixerSourceRms(PX3SynthAudioProcessor::mixerOsc2);
     const auto osc3Rms = audioProcessor.debugGetMixerSourceRms(PX3SynthAudioProcessor::mixerOsc3);
    const auto subPeak = audioProcessor.debugGetMixerSourcePeak(PX3SynthAudioProcessor::mixerSub);
    const auto osc1Peak = audioProcessor.debugGetMixerSourcePeak(PX3SynthAudioProcessor::mixerOsc1);
    const auto osc2Peak = audioProcessor.debugGetMixerSourcePeak(PX3SynthAudioProcessor::mixerOsc2);
    const auto osc3Peak = audioProcessor.debugGetMixerSourcePeak(PX3SynthAudioProcessor::mixerOsc3);
    const auto voicePeak = audioProcessor.debugGetVoicePeak();
    const auto voiceSubPeak = audioProcessor.debugGetVoiceSourcePeak(PX3SynthAudioProcessor::mixerSub);
    const auto voiceOsc1Peak = audioProcessor.debugGetVoiceSourcePeak(PX3SynthAudioProcessor::mixerOsc1);
    const auto voiceOsc2Peak = audioProcessor.debugGetVoiceSourcePeak(PX3SynthAudioProcessor::mixerOsc2);
    const auto voiceOsc3Peak = audioProcessor.debugGetVoiceSourcePeak(PX3SynthAudioProcessor::mixerOsc3);
    const auto activeVoiceCount = audioProcessor.debugGetActiveVoiceCount();
    const auto heldVoiceCount = audioProcessor.debugGetHeldVoiceCount();
    const auto releasingVoiceCount = audioProcessor.debugGetReleasingVoiceCount();
    const auto nearSilentReleaseVoiceCount = audioProcessor.debugGetNearSilentReleaseVoiceCount();
    const auto releaseEnergyEq = audioProcessor.debugGetReleaseEnergyEquivalent();
    const auto effectiveVoiceLoad = audioProcessor.debugGetEffectiveVoiceLoad();
    const auto polyGainTarget = audioProcessor.debugGetPolyphonyGainTarget();
    const auto polyGain = audioProcessor.debugGetPolyphonyGainApplied();
    const auto tailBypass = audioProcessor.debugGetPolyGainTailBypassActive();
    const auto releaseVoicesPruned = audioProcessor.debugGetReleaseVoicesPruned();
    const auto cpuLoadPercent = audioProcessor.debugGetInstanceCpuLoadPercent();
    const auto masterPreOutputPeak = audioProcessor.debugGetMasterPreOutputPeak();
    const auto prePolyPeak = audioProcessor.debugGetOscillatorBusPrePolyPeak();
    const auto prePolyClipSamples = audioProcessor.debugGetOscillatorBusPrePolyClipSamples();
    const auto masterClipSamples = audioProcessor.debugGetMasterClipSamples();
        const auto oscBusRms = audioProcessor.debugGetOscillatorBusRms();
        const auto dryBusRms = audioProcessor.debugGetDryBusRms();
        const auto fxBusRms = audioProcessor.debugGetFxBusRms();
        const auto masterBusRms = audioProcessor.debugGetMasterBusRms();
        const auto oscBusPeak = audioProcessor.debugGetOscillatorBusPeak();
        const auto dryBusPeak = audioProcessor.debugGetDryBusPeak();
        const auto fxBusPeak = audioProcessor.debugGetFxBusPeak();
        const auto masterBusPeak = audioProcessor.debugGetMasterBusPeak();

        const auto onsetGuardActive = attackSec < 0.02f;
        const auto fastAttackNorm = onsetGuardActive
                                 ? juce::jlimit(0.0f, 1.0f, (0.02f - juce::jmax(0.001f, attackSec)) / 0.019f)
                                 : 0.0f;
        const auto onsetGuardSamples = onsetGuardActive
                                    ? juce::jlimit(8,
                                                96,
                                                static_cast<int>(8.0f + 88.0f * fastAttackNorm))
                                    : 0;

    juce::String text;
        text << "COPY BLOCK (ALL KEY DIAGNOSTICS)\n"
            << "ampAttackSec=" << juce::String(attackSec, 6)
            << " ampDecaySec=" << juce::String(decaySec, 6)
            << " ampSustain=" << juce::String(sustainNorm, 6)
            << " ampReleaseSec=" << juce::String(releaseSec, 6)
            << " ampEnabled=" << juce::String(ampEnabled ? 1 : 0)
            << "\n"
            << "onsetGuardActive=" << juce::String(onsetGuardActive ? 1 : 0)
            << " onsetGuardSamples=" << juce::String(onsetGuardSamples)
            << "\n"
            << "activeVoices=" << juce::String(activeVoiceCount)
            << " heldVoices=" << juce::String(heldVoiceCount)
            << " releasingVoices=" << juce::String(releasingVoiceCount)
            << " nearSilentReleaseVoices=" << juce::String(nearSilentReleaseVoiceCount)
            << "\n"
            << "releaseEnergyEq=" << juce::String(releaseEnergyEq, 6)
            << " effectiveLoad=" << juce::String(effectiveVoiceLoad, 6)
            << "\n"
            << "polyGainTarget=" << juce::String(polyGainTarget, 6)
            << " polyGainApplied=" << juce::String(polyGain, 6)
            << " polyGainTailBypass=" << juce::String(tailBypass ? 1 : 0)
            << " releaseVoicesPruned=" << juce::String(releaseVoicesPruned)
            << " cpuLoadPct=" << juce::String(cpuLoadPercent, 2)
            << "\n"
            << "masterPreOutputPeak=" << juce::String(masterPreOutputPeak, 6)
            << " masterClipSamples=" << juce::String(masterClipSamples)
            << " oscPrePolyPeak=" << juce::String(prePolyPeak, 6)
            << " oscPrePolyClipSamples=" << juce::String(prePolyClipSamples)
            << "\n"
            << "busRmsOsc=" << juce::String(oscBusRms, 6)
            << " busRmsDry=" << juce::String(dryBusRms, 6)
            << " busRmsFx=" << juce::String(fxBusRms, 6)
            << " busRmsMaster=" << juce::String(masterBusRms, 6)
            << "\n"
            << "busPeakOsc=" << juce::String(oscBusPeak, 6)
            << " busPeakDry=" << juce::String(dryBusPeak, 6)
            << " busPeakFx=" << juce::String(fxBusPeak, 6)
            << " busPeakMaster=" << juce::String(masterBusPeak, 6)
            << "\n"
            << "voicePeak=" << juce::String(voicePeak, 6)
            << " voiceSub=" << juce::String(voiceSubPeak, 6)
            << " voiceOsc1=" << juce::String(voiceOsc1Peak, 6)
            << " voiceOsc2=" << juce::String(voiceOsc2Peak, 6)
            << " voiceOsc3=" << juce::String(voiceOsc3Peak, 6)
            << "\n\n";

    text << "Attack:  " << attackText << "\n"
         << "Decay:   " << decayText << "\n"
         << "Sustain: " << juce::String(sustainNorm, 4) << " (" << juce::String(sustainNorm * 100.0f, 1) << "%)\n"
            << "Release: " << releaseText << "\n"
            << "AMP Enabled: " << juce::String(ampEnabled ? "ON" : "OFF") << "\n\n"
            << "Graph:  /\\__====\\\n\n"
                << "Polyphony\n"
                << "activeVoices=" << juce::String(activeVoiceCount)
                << " heldVoices=" << juce::String(heldVoiceCount)
                << " releasingVoices=" << juce::String(releasingVoiceCount)
                << " nearSilentReleaseVoices=" << juce::String(nearSilentReleaseVoiceCount)
                << "\n"
                << "releaseEnergyEq=" << juce::String(releaseEnergyEq, 5)
                << " effectiveLoad=" << juce::String(effectiveVoiceLoad, 5)
                << "\n"
                << "polyGainTarget=" << juce::String(polyGainTarget, 5)
                << " polyGainApplied=" << juce::String(polyGain, 5)
                << "\n"
                << "polyGainTailBypass=" << juce::String(tailBypass ? "ON" : "OFF")
                << "\n"
                << "releaseVoicesPruned=" << juce::String(releaseVoicesPruned)
                << "\n"
                << "cpuLoad=" << juce::String(cpuLoadPercent, 2) << "%"
                << "\n"
                << "masterPreOutputPeak=" << juce::String(masterPreOutputPeak, 6)
                << " masterClipSamples=" << juce::String(masterClipSamples)
                << "\n"
                << "oscPrePolyPeak=" << juce::String(prePolyPeak, 6)
                << " oscPrePolyClipSamples=" << juce::String(prePolyClipSamples) << "\n\n"
                << "Per-Voice Peak\n"
                << "voicePeak=" << juce::String(voicePeak, 6)
                << " sub=" << juce::String(voiceSubPeak, 6)
                << " osc1=" << juce::String(voiceOsc1Peak, 6)
                << " osc2=" << juce::String(voiceOsc2Peak, 6)
                << " osc3=" << juce::String(voiceOsc3Peak, 6) << "\n\n"
                << "AMP Release Probe (post-AMP per-source dry RMS)\n"
                << "SUB  enabled=" << juce::String(subEnabled ? 1 : 0)
                << " mixLevel=" << juce::String(subMixerLevel, 4)
                << " rms=" << juce::String(subRms, 6)
                << " peak=" << juce::String(subPeak, 6) << "\n"
                << "OSC1 enabled=" << juce::String(osc1Enabled ? 1 : 0)
                << " mixLevel=" << juce::String(osc1MixerLevel, 4)
                << " rms=" << juce::String(osc1Rms, 6)
                << " peak=" << juce::String(osc1Peak, 6) << "\n"
                << "OSC2 enabled=" << juce::String(osc2Enabled ? 1 : 0)
                << " mixLevel=" << juce::String(osc2MixerLevel, 4)
                << " rms=" << juce::String(osc2Rms, 6)
                << " peak=" << juce::String(osc2Peak, 6) << "\n"
                << "OSC3 enabled=" << juce::String(osc3Enabled ? 1 : 0)
                << " mixLevel=" << juce::String(osc3MixerLevel, 4)
                << " rms=" << juce::String(osc3Rms, 6)
                << " peak=" << juce::String(osc3Peak, 6) << "\n\n"
            << "Mod Envelope Sources\n";

        for (int i = 0; i < PX3SynthAudioProcessor::kEnvelopeSourceCount; ++i)
        {
           const auto baseNorm = audioProcessor.debugGetEnvelopeDestinationBaseNormalized(i);
           const auto effectiveNorm = audioProcessor.debugGetEnvelopeDestinationEffectiveNormalized(i);
           const auto contributionNorm = audioProcessor.debugGetEnvelopeContributionNormalized(i);
           text << "ENV " << juce::String(i + 1)
               << " value=" << juce::String(audioProcessor.debugGetEnvelopeCurrentValue(i), 5)
               << " amount=" << juce::String(audioProcessor.getEnvelopeAmountParam(i).get(), 4)
               << " assign=" << audioProcessor.debugGetEnvelopeAssignmentName(i)
               << "\n"
               << "      contribution(norm)=" << juce::String(contributionNorm, 5)
               << " base(norm)=" << juce::String(baseNorm, 5)
               << " effective(norm)=" << juce::String(effectiveNorm, 5)
               << " delta(norm)=" << juce::String(effectiveNorm - baseNorm, 5)
               << "\n";
        }

    setDebugTextStable(debugEnvelopeText, text, true);
}

void PX3SynthAudioProcessorEditor::debugCaptureSnapshot(const juce::String& reason)
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

void PX3SynthAudioProcessorEditor::debugCompareWithSnapshot()
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

void PX3SynthAudioProcessorEditor::debugForceSerializationTest()
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
    const auto hasVibeAmount = xmlText.containsIgnoreCase("vibeAmount");
    const auto pass = hasModuleOrder && hasDelayTime && hasReverbAmount && hasVibeAmount;

    juce::String report;
    report << "FORCE STATE SERIALIZATION TEST\n";
    report << "Size: " << juce::String(static_cast<int>(block.getSize())) << "\n";
    report << "MODULE_ORDER present: " << (hasModuleOrder ? "YES" : "NO") << "\n";
    report << "delayTime present: " << (hasDelayTime ? "YES" : "NO") << "\n";
    report << "reverbAmount present: " << (hasReverbAmount ? "YES" : "NO") << "\n";
    report << "vibeAmount present: " << (hasVibeAmount ? "YES" : "NO") << "\n";
    report << "RESULT: " << (pass ? "PASS" : "FAIL");
    debugSnapshotText.setText(report, juce::dontSendNotification);
    audioProcessor.debugLogEvent("DEBUG_PANEL", "FORCE_SERIALIZATION_TEST", report.replaceCharacters("\n", " | "));
}

void PX3SynthAudioProcessorEditor::refreshDebugDumpPresetEnablement()
{
    // A dumped preset with no name or no author is one nobody can find or
    // credit later, so the button waits for both rather than inventing them.
    // The file chooser is the expensive, interrupting part of this action; the
    // check belongs before it, not after.
    const auto hasName = debugDumpPresetNameEditor.getText().trim().isNotEmpty();
    const auto hasAuthor = debugDumpPresetAuthorEditor.getText().trim().isNotEmpty();

    debugDumpPresetButton.setEnabled(hasName && hasAuthor);
    debugDumpPresetButton.setTooltip(hasName && hasAuthor
                                         ? juce::String()
                                         : juce::String("A preset name and an author are required"));
}

PresetManager::PresetMetadata PX3SynthAudioProcessorEditor::debugDumpPresetMetadata() const
{
    PresetManager::PresetMetadata metadata;
    metadata.name = debugDumpPresetNameEditor.getText().trim();
    metadata.author = debugDumpPresetAuthorEditor.getText().trim();

    // From the dropdown, which is filled from the categories the library
    // actually has. It used to inherit whatever preset happened to be loaded,
    // which meant a dump was filed under a category nobody chose.
    metadata.category = debugDumpPresetCategoryBox.getText().trim();
    if (metadata.category.isEmpty())
    {
        metadata.category = "EXPERIMENTAL";
    }

    metadata.description = hasCurrentPreset ? currentPreset.metadata.description : juce::String();
    return metadata;
}

void PX3SynthAudioProcessorEditor::debugDumpPresetToFile()
{
    // Both are required and the button is disabled without them, so there is
    // no fallback chain here any more: whatever is in the fields is what the
    // preset is called and who it is by.
    const auto suggestedName = debugDumpPresetNameEditor.getText().trim();
    const auto author = debugDumpPresetAuthorEditor.getText().trim();

    if (suggestedName.isEmpty() || author.isEmpty())
    {
        audioProcessor.debugLogEvent("DEBUG_PANEL", "DUMP_PRESET_BLOCKED",
                                     "name and author are both required");
        return;
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

                             const auto metadata = debugDumpPresetMetadata();

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

void PX3SynthAudioProcessorEditor::debugWriteDeterministicTestValues()
{
    for (auto& control : debugParamControls)
    {
        if (control->key == "globalAmount") control->slider.setValue(0.77, juce::sendNotificationSync);
        else if (control->key == "bypass") control->slider.setValue(0.0, juce::sendNotificationSync);
        else if (control->key == "seed") control->slider.setValue(4242.0, juce::sendNotificationSync);
        else if (control->key == "oscillatorDrift") control->slider.setValue(0.68, juce::sendNotificationSync);
        else if (control->key == "voiceVariation") control->slider.setValue(0.74, juce::sendNotificationSync);
        else if (control->key == "filterVariation") control->slider.setValue(0.52, juce::sendNotificationSync);
        else if (control->key == "saturation") control->slider.setValue(0.61, juce::sendNotificationSync);
        else if (control->key == "noise") control->slider.setValue(0.33, juce::sendNotificationSync);
        else if (control->key == "psuMovement") control->slider.setValue(0.57, juce::sendNotificationSync);
        else if (control->key == "vcaNonlinearity") control->slider.setValue(0.49, juce::sendNotificationSync);
        else if (control->key == "waveformAsymmetry") control->slider.setValue(0.44, juce::sendNotificationSync);
        else if (control->key == "temperatureDrift") control->slider.setValue(0.59, juce::sendNotificationSync);
        else if (control->key == "correlatedChaos") control->slider.setValue(0.72, juce::sendNotificationSync);
        else if (control->key == "fxSendGain") control->slider.setValue(0.81, juce::sendNotificationSync);
        else if (control->key == "fxReturnGain") control->slider.setValue(0.77, juce::sendNotificationSync);
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "WRITE_TEST_VALUES", "deterministic values applied");
    refreshDebugPanel(false);
}

void PX3SynthAudioProcessorEditor::debugRandomizeParameters()
{
    std::mt19937 rng(static_cast<uint32_t>(juce::Time::getMillisecondCounter()));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    std::uniform_int_distribution<int> seedDist(1, 65535);

    for (auto& control : debugParamControls)
    {
        if (control->key == "seed")
        {
            control->slider.setValue(static_cast<double>(seedDist(rng)), juce::sendNotificationSync);
        }
        else if (control->key == "bypass")
        {
            control->slider.setValue(dist(rng) > 0.5f ? 1.0 : 0.0, juce::sendNotificationSync);
        }
        else
        {
            control->slider.setValue(static_cast<double>(dist(rng)), juce::sendNotificationSync);
        }
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "RANDOMIZE_PARAMETERS", "all ranged parameters randomized");
    refreshDebugPanel(false);
}

void PX3SynthAudioProcessorEditor::debugResetParameters()
{
    for (auto& control : debugParamControls)
    {
        if (control->key == "globalAmount") control->slider.setValue(0.0, juce::sendNotificationSync);
        else if (control->key == "bypass") control->slider.setValue(0.0, juce::sendNotificationSync);
        else if (control->key == "seed") control->slider.setValue(1337.0, juce::sendNotificationSync);
        else if (control->key == "oscillatorDrift") control->slider.setValue(0.55, juce::sendNotificationSync);
        else if (control->key == "voiceVariation") control->slider.setValue(0.55, juce::sendNotificationSync);
        else if (control->key == "filterVariation") control->slider.setValue(0.45, juce::sendNotificationSync);
        else if (control->key == "saturation") control->slider.setValue(0.40, juce::sendNotificationSync);
        else if (control->key == "noise") control->slider.setValue(0.25, juce::sendNotificationSync);
        else if (control->key == "psuMovement") control->slider.setValue(0.38, juce::sendNotificationSync);
        else if (control->key == "vcaNonlinearity") control->slider.setValue(0.42, juce::sendNotificationSync);
        else if (control->key == "waveformAsymmetry") control->slider.setValue(0.32, juce::sendNotificationSync);
        else if (control->key == "temperatureDrift") control->slider.setValue(0.40, juce::sendNotificationSync);
        else if (control->key == "correlatedChaos") control->slider.setValue(0.50, juce::sendNotificationSync);
        else if (control->key == "fxSendGain") control->slider.setValue(1.0, juce::sendNotificationSync);
        else if (control->key == "fxReturnGain") control->slider.setValue(1.0, juce::sendNotificationSync);
    }

    audioProcessor.debugLogEvent("DEBUG_PANEL", "RESET_PARAMETERS", "all ranged parameters reset to defaults");
    refreshDebugPanel(false);
}

void PX3SynthAudioProcessorEditor::debugApplyModuleOrder(const px3::FxOrder& order,
                                                             const juce::String& reason,
                                                             int fromIndex,
                                                             int toIndex)
{
    applyFxChainOrder(order, "DEBUG_PANEL", reason, fromIndex, toIndex);
    refreshDebugPanel(false);
    repaint();
}

std::array<juce::String, px3::kFxStageCount> PX3SynthAudioProcessorEditor::readModuleOrderFromStateTree(const juce::ValueTree& state) const
{
    std::array<juce::String, px3::kFxStageCount> result {};
    for (int i = 0; i < px3::kFxStageCount; ++i)
    {
        result[static_cast<std::size_t>(i)] =
            px3::processor_internal::moduleIdForStage(px3::kDefaultFxOrder[static_cast<std::size_t>(i)]);
    }
    const auto moduleOrder = state.getChildWithName("MODULE_ORDER");
    if (!moduleOrder.isValid())
    {
        return result;
    }

    int write = 0;
    for (int i = 0; i < moduleOrder.getNumChildren() && write < px3::kFxStageCount; ++i)
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

juce::String PX3SynthAudioProcessorEditor::describeUiOrder() const
{
    juce::StringArray items;
    for (const auto sectionId : fxSectionOrder)
    {
        items.add(fxModuleIdFromSection(sectionId));
    }
    return items.joinIntoString(",");
}

juce::String PX3SynthAudioProcessorEditor::describeProcessorOrder() const
{
    return audioProcessor.debugDescribeOrder(audioProcessor.getFxProcessingOrder());
}

juce::String PX3SynthAudioProcessorEditor::describeStateTreeOrder() const
{
    const auto state = audioProcessor.createParameterStateTree();
    const auto order = readModuleOrderFromStateTree(state);
    return order[0] + "," + order[1] + "," + order[2] + "," + order[3];
}

juce::String PX3SynthAudioProcessorEditor::buildParameterInspectorText() const
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

juce::String PX3SynthAudioProcessorEditor::buildInstanceInfoText() const
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
    text << "Num Modules: 4\n";
    return text;
}

