#pragma once

#include <JuceHeader.h>

#include <array>
#include <map>
#include <vector>

#include "PerformanceControls.h"
#include "PianoKeyboard.h"
#include "PresetManager.h"
#include "PluginProcessor.h"
#include "GenericEnvelopeComponent.h"
#include "FilterResponseComponent.h"
#include "GenericLfoComponent.h"
#include "OscillatorDisplayComponent.h"

/**
 * Main JUCE editor for P(X3).
 *
 * This class owns UI presentation and user interaction only. It never becomes
 * the authoritative source of synth state; parameters and state live in the
 * processor and are mirrored here through parameter attachments and periodic
 * UI refresh.
 *
 * Simplified flow:
 *
 *   User/Host Automation -> AudioParameters (processor) -> UI attachments
 *   UI gesture -> AudioParameters (processor) -> DSP reads in processBlock
 *
 * The debug console is intentionally hosted here because it is a developer UI
 * surface, but it queries/acts on processor-owned state.
 */
class PX3SynthAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer,
                                              private juce::ListBoxModel
{
public:
    explicit PX3SynthAudioProcessorEditor(PX3SynthAudioProcessor&);
    ~PX3SynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    class KnobLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawRotarySlider(juce::Graphics& g,
                              int x,
                              int y,
                              int width,
                              int height,
                              float sliderPos,
                              float rotaryStartAngle,
                              float rotaryEndAngle,
                              juce::Slider& slider) override;
    };

    class KnobLabel final : public juce::Label
    {
    public:
        void paint(juce::Graphics& g) override;
    };

    class PresetBrowserPanelComponent final : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override
        {
            const auto panel = getLocalBounds().toFloat();
            g.setColour(juce::Colour::fromRGBA(24, 24, 24, 246));
            g.fillRoundedRectangle(panel, 10.0f);

            g.setColour(juce::Colour::fromRGBA(255, 255, 255, 18));
            g.fillRoundedRectangle(panel.withTrimmedBottom(panel.getHeight() * 0.58f), 10.0f);

            g.setColour(juce::Colour::fromRGBA(138, 188, 255, 180));
            g.drawRoundedRectangle(panel, 10.0f, 1.0f);
        }
    };

    class SectionPanelComponent final : public juce::Component
    {
    public:
        void setHeader(juce::String titleIn, juce::Colour accentIn)
        {
            title = std::move(titleIn);
            accent = accentIn;
            repaint();
        }

        void paint(juce::Graphics& g) override
        {
            const auto area = getLocalBounds().toFloat().reduced(2.0f);
            g.setColour(accent.withAlpha(0.14f));
            g.fillRoundedRectangle(area, 10.0f);

            g.setColour(accent.withAlpha(0.10f));
            g.fillRoundedRectangle(area.withTrimmedBottom(area.getHeight() * 0.5f), 10.0f);

            g.setColour(accent.withAlpha(0.75f));
            g.drawRoundedRectangle(area, 10.0f, 1.0f);

            g.setColour(accent.brighter(0.30f));
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawText(title, getLocalBounds().removeFromTop(24), juce::Justification::centred);
        }

    private:
        juce::String title { "" };
        juce::Colour accent { juce::Colour::fromRGB(100, 100, 100) };
    };

    struct KnobBinding
    {
        juce::Slider* slider { nullptr };
        juce::Label* label { nullptr };
        juce::AudioParameterFloat* parameter { nullptr };
    };

    void configureKnob(KnobBinding& binding, const juce::String& labelText, juce::AudioParameterFloat& parameter);
    void configureEffectKnob(juce::Slider& slider,
                             KnobLabel& label,
                             const juce::String& labelText,
                             juce::AudioParameterFloat& parameter);
    void attachSlider(juce::RangedAudioParameter& parameter, juce::Slider& slider);
    void attachComboBox(juce::RangedAudioParameter& parameter, juce::ComboBox& comboBox);
    void attachButton(juce::RangedAudioParameter& parameter, juce::Button& button);
    void refreshAnyKeyDownState();
    void refreshOscillatorModeUI();
    void refreshGranularModeUI();
    void refreshFxBypassUI();
    void refreshLfoAssignmentUI();
    void refreshLfoFrequencyLabel();
    void refreshLfoUI();
    void refreshEnvelopeGraphUI();
    void refreshFilterResponseUI();
    void updatePanelVisibility();
    bool isPanelVisible(int sectionIndex) const;
    void layoutOscPanel();
    void layoutFilterPanel();
    void layoutEnvelopePanel();
    void layoutFxPanel();
    void layoutMixPanel();
    void updateFxSectionTargets(const juce::Rectangle<int>& topArea, int topGap);
    void layoutFxSectionsFromCurrentAreas();
    void animateFxSections();
    int indexForFxSection(int sectionId) const;
    int fxSectionAtPoint(juce::Point<int> point) const;
    void moveFxSectionToSlot(int sectionId, int slotIndex);
    void commitFxOrderToProcessor(const juce::String& source = "USER",
                                  const juce::String& reason = "UI_COMMIT",
                                  int fromIndex = -1,
                                  int toIndex = -1);
    void rebuildPresetFilteredList();
    void refreshPresetNameDisplay();
    void applyPresetRecord(const PresetManager::PresetRecord& record);
    void openPresetBrowser();
    void closePresetBrowser();
    void showPresetError(const juce::String& title, const juce::String& message);
    void savePreset(bool saveAs);
    void importPreset();
    void exportCurrentPreset();
    void showPresetMenu();
    void configureTopMenuSectionButton(juce::TextButton& button, const juce::String& text, int sectionIndex);
    void applyTopMenuSectionSelection(int sectionIndex, bool pushToProcessor);
    void refreshTopMenuSelectionFromProcessor();
    void updatePresetDirtyState();
    juce::String computeCurrentStateHash() const;
    int getNumRows() override;
    void paintListBoxItem(int rowNumber,
                          juce::Graphics& g,
                          int width,
                          int height,
                          bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    static juce::String noteNameForMidi(int midiNote);
    void timerCallback() override;

    static juce::String fxModuleIdFromSection(int sectionId);
    static int fxSectionFromModuleId(const juce::String& moduleId);

    struct DebugSnapshot
    {
        juce::String timestamp;
        std::array<int, 3> processorOrder { { 0, 1, 2 } };
        std::array<int, 3> uiOrder { { 0, 1, 2 } };
        juce::String stateXml;
        juce::String serializedXml;
        int serializedBytes { 0 };
        uint32_t generation { 0 };
        uint32_t hash { 0 };
        std::map<juce::String, float> normalizedValues;
    };

    struct DebugParamControl
    {
        juce::String key;
        juce::Label label;
        juce::Slider slider;
        juce::Label readback;
        juce::RangedAudioParameter* parameter { nullptr };
        float lastRequested { 0.0f };
        bool suppressCallbacks { false };
    };

    void setupDebugPanel();
    void openDebugWindow();
    void closeDebugWindow();
    void toggleDebugWindow();
    void layoutDebugPanel(const juce::Rectangle<int>& bounds);
    void refreshDebugPanel(bool includeHeavySections = false);
    void refreshDebugModuleState();
    void refreshDebugValueTree();
    void refreshDebugSerializedState();
    void refreshDebugParameterInspector();
    void refreshDebugParameterControls();
    void refreshDebugEventLog();
    void refreshDebugLfoState();
    void refreshDebugEnvelopeState();
    void debugCaptureSnapshot(const juce::String& reason);
    void debugCompareWithSnapshot();
    void debugForceSerializationTest();
    void debugDumpPresetToFile();
    void debugWriteDeterministicTestValues();
    void debugRandomizeParameters();
    void debugResetParameters();
    void debugApplyModuleOrder(const std::array<int, 3>& order,
                               const juce::String& reason,
                               int fromIndex = -1,
                               int toIndex = -1);
    std::array<juce::String, 3> readModuleOrderFromStateTree(const juce::ValueTree& state) const;
    juce::String describeUiOrder() const;
    juce::String describeProcessorOrder() const;
    juce::String describeStateTreeOrder() const;
    juce::String buildParameterInspectorText() const;
    juce::String buildInstanceInfoText() const;

    PX3SynthAudioProcessor& audioProcessor;
    KnobLookAndFeel knobLookAndFeel;
    juce::TooltipWindow tooltipWindow;
    PerformanceControls performanceControls;
    PianoKeyboard pianoKeyboard;
    PX3SynthAudioProcessor::MidiStatus midiStatus;

    juce::Image backgroundImage;
    juce::Image logoFrame;
    juce::Image logoGlitchMaskR;
    juce::Image logoGlitchMaskG;
    juce::Image logoGlitchMaskB;
    bool anyKeyDown { false };
    float logoVibrationPhase { 0.0f };
    float logoVibrationIntensity { 0.0f };

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> controlsArea;
    juce::Rectangle<int> panelViewportArea;
    juce::Rectangle<int> topMenuStripArea;
    juce::Rectangle<int> logoPanelArea;
    juce::Rectangle<int> topMenuSectionButtonsArea;
    juce::Rectangle<int> topMenuPresetClusterArea;
    juce::Rectangle<int> topMenuGainArea;
    juce::Rectangle<int> headerPlaceholderArea;
    juce::Rectangle<int> presetBarArea;
    juce::Rectangle<int> robSectionArea;
    juce::Rectangle<int> isaacSectionArea;
    juce::Rectangle<int> reverbSectionArea;
    juce::Rectangle<int> topSpareSectionArea;
    juce::Rectangle<int> midiStatusArea;
    juce::Rectangle<int> performanceControlsArea;
    std::array<juce::Rectangle<int>, 3> fxSectionSlots {};
    std::array<juce::Rectangle<float>, 3> fxSectionCurrentAreas {};
    std::array<juce::Rectangle<float>, 3> fxSectionTargetAreas {};
    std::array<int, 3> fxSectionOrder { { 0, 1, 2 } };
    bool fxSectionsInitialized { false };
    int draggingFxSection { -1 };
    float draggingSectionOffsetX { 0.0f };
    bool logoClickArmed { false };
    juce::Point<int> logoMouseDownPoint;
    int pressedFxSection { -1 };
    juce::Point<int> fxDragStartPoint;
    bool fxDragHasMoved { false };

    juce::Slider oscSineKnob;
    juce::Slider oscSawKnob;
    juce::Slider oscSquareKnob;
    juce::ComboBox oscModeBox;
    KnobLabel oscModeLabel;
    juce::ComboBox oscVowelBox;
    KnobLabel oscVowelLabel;
    juce::Slider cutoffKnob;
    juce::Slider resonanceKnob;
    juce::ComboBox filterTypeBox;
    juce::Slider attackKnob;
    juce::Slider decayKnob;
    juce::Slider sustainKnob;
    juce::Slider releaseKnob;
    juce::Slider gainKnob;
    juce::Slider lfoFrequencyKnob;
    juce::ComboBox lfoWaveformBox;
    juce::ComboBox lfoAssignBox;

    KnobLabel oscSineLabel;
    KnobLabel oscSawLabel;
    KnobLabel oscSquareLabel;
    KnobLabel cutoffLabel;
    KnobLabel resonanceLabel;
    KnobLabel filterTypeLabel;
    KnobLabel attackLabel;
    KnobLabel decayLabel;
    KnobLabel sustainLabel;
    KnobLabel releaseLabel;
    KnobLabel gainLabel;
    KnobLabel lfoFrequencyLabel;
    KnobLabel lfoWaveformLabel;
    juce::Label lfoFrequencyValueLabel;
    KnobLabel lfoAssignLabel;
    KnobLabel midiStatusLabel;
    SectionPanelComponent oscPanel;
    SectionPanelComponent envPanel;
    SectionPanelComponent fltPanel;
    SectionPanelComponent fxPanel;
    SectionPanelComponent mixPanel;
    std::unique_ptr<OscillatorDisplayComponent> oscillatorDisplayComponent;
    std::unique_ptr<GenericLfoComponent> lfoComponent;
    std::unique_ptr<GenericEnvelopeComponent> envelopeGraph;
    std::unique_ptr<FilterResponseComponent> filterResponseComponent;

    juce::Slider vibeAmountKnob;
    KnobLabel vibeAmountLabel;
    juce::ComboBox vibeTypeBox;
    KnobLabel vibeTypeLabel;
    juce::Slider isaacTextureKnob;
    KnobLabel isaacTextureLabel;
    juce::ComboBox delayAlgoBox;
    KnobLabel delayAlgoLabel;
    juce::ComboBox granularModeBox;
    KnobLabel granularModeLabel;
    juce::Slider delayTimeKnob;
    KnobLabel delayTimeLabel;
    juce::Slider delayFeedbackKnob;
    KnobLabel delayFeedbackLabel;
    juce::ComboBox granularSyncBox;
    KnobLabel granularSyncLabel;
    juce::Slider reverbKnob;
    KnobLabel reverbLabel;
    juce::ComboBox reverbTypeBox;
    KnobLabel reverbTypeLabel;
    juce::ToggleButton robBypassButton;
    juce::ToggleButton delayBypassButton;
    juce::ToggleButton reverbBypassButton;

    PresetManager presetManager;
    std::vector<PresetManager::PresetRecord> presetFiltered;
    PresetManager::PresetRecord currentPreset;
    bool hasCurrentPreset { false };
    bool currentPresetDirty { false };
    juce::String loadedStateHash;
    int dirtyUpdateCounter { 0 };

    juce::TextButton presetPrevButton;
    juce::TextButton presetNameButton;
    juce::TextButton presetNextButton;
    juce::TextButton presetMenuButton;
    juce::TextButton topMenuOscButton;
    juce::TextButton topMenuEnvButton;
    juce::TextButton topMenuFltButton;
    juce::TextButton topMenuFxButton;
    juce::TextButton topMenuMixButton;
    std::array<juce::TextButton*, 5> topMenuSectionButtons {
        { &topMenuOscButton, &topMenuEnvButton, &topMenuFltButton, &topMenuFxButton, &topMenuMixButton }
    };
    int selectedTopMenuSection { 0 };

    PresetBrowserPanelComponent presetBrowserPanel;
    juce::Label presetBrowserTitle;
    juce::TextEditor presetSearchEditor;
    juce::ComboBox presetScopeBox;
    juce::ComboBox presetCategoryBox;
    juce::ListBox presetListBox;
    juce::TextButton presetBrowserLoadButton;
    juce::TextButton presetBrowserCloseButton;
    juce::Label presetBrowserDetails;
    bool presetBrowserVisible { false };
    bool presetBrowserDragging { false };
    juce::Point<int> presetBrowserDragOffset;
    juce::Image presetBrowserBackdropSnapshot;

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboBoxAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    std::array<KnobBinding, 11> knobBindings {};
    int lastGranularModeIndex { -1 };
    int lastLfoAssignmentIndex { -1 };

    juce::Component debugPanel;
    juce::Label debugPanelTitle;
    juce::TextButton debugPanelCloseButton;
    juce::TextButton debugClearLogButton;
    juce::TextButton debugCopyLogButton;
    juce::TextButton debugSerializeButton;
    juce::TextButton debugRoundTripButton;
    juce::TextButton debugForceSerializeTestButton;
    juce::TextButton debugRestoreLastSerializedButton;
    juce::TextButton debugSnapshotButton;
    juce::TextButton debugCompareSnapshotButton;
    juce::TextButton debugResetOrderButton;
    juce::TextButton debugOrderAButton;
    juce::TextButton debugOrderBButton;
    juce::TextButton debugOrderCButton;
    juce::TextButton debugInvalidOrderButton;
    juce::TextButton debugRandomizeParamsButton;
    juce::TextButton debugResetParamsButton;
    juce::TextButton debugWriteTestValuesButton;
    juce::Label debugInstanceLabel;
    juce::Label debugModuleOrderLabel;
    juce::Label debugValueTreeLabel;
    juce::Label debugSerializedLabel;
    juce::Label debugParameterLabel;
    juce::Label debugBackendControlLabel;
    juce::Label debugEventLogLabel;
    juce::Label debugSnapshotLabel;
    juce::Label debugLfoLabel;
    juce::Label debugLfoAssignLabel;
    juce::Label debugEnvelopeLabel;
    juce::Label debugPresetToolsLabel;
    juce::Label debugDumpPresetNameLabel;
    juce::TextEditor debugInstanceText;
    juce::TextEditor debugModuleOrderText;
    juce::TextEditor debugValueTreeText;
    juce::TextEditor debugSerializedText;
    juce::TextEditor debugParameterInspectorText;
    juce::TextEditor debugEventLogText;
    juce::TextEditor debugSnapshotText;
    juce::TextEditor debugLfoText;
    juce::TextEditor debugEnvelopeText;
    juce::TextEditor debugDumpPresetNameEditor;
    juce::ComboBox debugLfoAssignBox;
    juce::TextButton debugDumpPresetButton;
    juce::Viewport debugParamViewport;
    juce::Component debugParamContent;
    std::vector<std::unique_ptr<DebugParamControl>> debugParamControls;
    std::unique_ptr<juce::DocumentWindow> debugWindow;
    juce::Rectangle<int> debugWindowBounds { 100, 80, 1240, 780 };
    juce::Rectangle<int> debugLastPanelLayoutBounds;
    int debugRefreshTickCounter { 0 };
    bool debugPanelVisible { false };
    bool debugParamControlsInitialized { false };
    bool debugLfoAssignSuppressCallbacks { false };
    bool debugHasSnapshot { false };
    juce::String debugEditorCreatedTime;
    DebugSnapshot debugLastSnapshot;
};
