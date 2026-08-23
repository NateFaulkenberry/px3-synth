#include "PluginEditor.h"

#include "PX3Version.h"

#include <algorithm>
#include <functional>
#include <random>

namespace
{
constexpr int kFxSectionDrive = 0;
constexpr int kFxSectionDelay = 1;
constexpr int kFxSectionReverb = 2;

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

