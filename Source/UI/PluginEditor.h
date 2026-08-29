#pragma once

#include "../DSP/FxChain.h"
#include "FxCardComponent.h"

#include <JuceHeader.h>

#include "BypassButton.h"
#include "MixerControls.h"
#include "ToggleChipButton.h"
#include "ChipLabel.h"

#include <array>
#include <map>
#include <vector>

#include "PerformanceControls.h"
#include "PianoKeyboard.h"
#include "PresetManager.h"
#include "PluginProcessor.h"
#include "ModPanel.h"
#include "AmpPanel.h"
#include "FltPanel.h"
#include "FxPanel.h"
#include "MixPanel.h"
#include "OscPanel.h"
#include "TopMenuBar.h"
#include "DelayComponent.h"
#include "UIConfigManager.h"

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

    // The shared chip rendering, under the name the editor already uses.
    using KnobLabel = px3::ui::ChipLabel;

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

    struct KnobBinding
    {
        juce::Slider* slider { nullptr };
        juce::Label* label { nullptr };
        juce::AudioParameterFloat* parameter { nullptr };
    };

    void configureKnob(KnobBinding& binding, const juce::String& labelText, juce::AudioParameterFloat& parameter);
    // Vibe's amount knob has no caption, so it configures the slider half only.
    void configureEffectKnob(juce::Slider& slider, juce::AudioParameterFloat& parameter);
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
    // Builds an FX card that owns its controls, attaches every parameter it
    // declares, and hands it to the panel. One per new-generation FX.
    void buildDoomCard();
    void buildLucyCard();
    void refreshLfoAssignmentUI();
    void refreshEnvelopeAssignmentUI();
    void refreshLfoFrequencyLabel();
    void refreshLfoUI();
    void refreshSubOscUI();
    void refreshEnvelopeGraphUI();
    void refreshAmpEnvelopeUI();
    void refreshFilterUI();
    void updatePanelVisibility();
    bool isPanelVisible(int sectionIndex) const;
    void layoutOscPanel();
    void layoutAmpPanel();
    void layoutFilterPanel();
    void layoutModPanel();
    void layoutFxPanel();
    void layoutMixPanel();
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
    void timerCallback() override;
    juce::File resolveUiConfigFile() const;
    void loadUiConfig(bool forceReload);
    void applyUiConfig();

    static juce::String fxModuleIdFromSection(int sectionId);

    // The chain's shape is the processor's, not the editor's: an FX section IS
    // an FX stage, so there is nothing here to keep in step with it.
    static constexpr int kFxSectionCount = px3::kFxStageCount;

    struct DebugSnapshot
    {
        juce::String timestamp;
        px3::FxOrder processorOrder { px3::kDefaultFxOrder };
        px3::FxOrder uiOrder { px3::kDefaultFxOrder };
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
    void refreshDebugPerformanceOverlay();
    void debugCaptureSnapshot(const juce::String& reason);
    void debugCompareWithSnapshot();
    void debugForceSerializationTest();
    void debugDumpPresetToFile();
    void debugWriteDeterministicTestValues();
    void debugRandomizeParameters();
    void debugResetParameters();
    void debugApplyModuleOrder(const px3::FxOrder& order,
                               const juce::String& reason,
                               int fromIndex = -1,
                               int toIndex = -1);
    std::array<juce::String, px3::kFxStageCount> readModuleOrderFromStateTree(const juce::ValueTree& state) const;
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
    double lastAnimationTickSeconds { 0.0 };
    // UIConfig hot-reload polls the filesystem. The editor timer runs at 30 Hz,
    // which is far more often than a developer edits the file, so the poll is
    // throttled independently of the animation tick.
    double lastUiConfigPollSeconds { 0.0 };

    juce::Rectangle<int> headerArea;
    juce::Rectangle<int> controlsArea;
    juce::Rectangle<int> panelViewportArea;
    juce::Rectangle<int> topMenuStripArea;
    juce::Rectangle<int> logoPanelArea;
    // Where a click actually opens the site. Narrower than the panel on the
    // right, so a near-miss aimed at the first section button does not open a
    // browser instead.
    juce::Rectangle<int> logoClickArea;
    juce::Rectangle<int> topMenuSectionButtonsArea;
    juce::Rectangle<int> topMenuPresetClusterArea;
    juce::Rectangle<int> topMenuMenuButtonArea;
    juce::Rectangle<int> topMenuGainArea;
    juce::Rectangle<int> headerPlaceholderArea;
    juce::Rectangle<int> presetBarArea;
    juce::Rectangle<int> robSectionArea;
    juce::Rectangle<int> midiStatusArea;
    juce::Rectangle<int> performanceControlsArea;
    // The one authoritative UI-side chain order. The signal-flow strip edits it,
    // the FX grid and the processor both follow it.
    px3::FxOrder fxSectionOrder { px3::kDefaultFxOrder };
    void applyFxChainOrder(const px3::FxOrder& order,
                           const juce::String& source,
                           const juce::String& reason,
                           int fromIndex,
                           int toIndex);
    bool logoClickArmed { false };
    juce::Point<int> logoMouseDownPoint;

    juce::Slider osc1MacroAKnob;
    juce::Slider osc1MacroBKnob;
    juce::Slider osc1MacroCKnob;
    juce::Slider osc2MacroAKnob;
    juce::Slider osc2MacroBKnob;
    juce::Slider osc2MacroCKnob;
    juce::Slider osc3MacroAKnob;
    juce::Slider osc3MacroBKnob;
    juce::Slider osc3MacroCKnob;
    juce::ComboBox osc1ModeBox;
    juce::ComboBox osc2ModeBox;
    juce::ComboBox osc3ModeBox;
    KnobLabel osc1ModeLabel;
    KnobLabel osc2ModeLabel;
    KnobLabel osc3ModeLabel;
    juce::ComboBox osc1VowelBox;
    juce::ComboBox osc2VowelBox;
    juce::ComboBox osc3VowelBox;
    KnobLabel osc1VowelLabel;
    KnobLabel osc2VowelLabel;
    KnobLabel osc3VowelLabel;
    juce::Slider cutoffKnob;
    juce::Slider resonanceKnob;
    juce::ComboBox filterTypeBox;
    juce::Slider cutoff2Knob;

    // Comb mode's controls, one set per filter. Declared as arrays because
    // there is nothing per-instance about them beyond which parameter they
    // attach to, and two hand-written copies of seven controls is how the rest
    // of this file grew names like oscSineKnob.
    std::array<juce::Slider, kFilterInstanceCount> combTuneKnobs;
    std::array<juce::Slider, kFilterInstanceCount> combDecayKnobs;
    std::array<juce::Slider, kFilterInstanceCount> combDampingKnobs;
    std::array<juce::Slider, kFilterInstanceCount> combDispersionKnobs;
    std::array<juce::Slider, kFilterInstanceCount> combDriveKnobs;
    std::array<juce::Slider, kFilterInstanceCount> combMixKnobs;
    std::array<px3::ui::ToggleChipButton, kFilterInstanceCount> combInvertButtons;
    std::array<KnobLabel, kFilterInstanceCount> combTuneLabels;
    std::array<KnobLabel, kFilterInstanceCount> combDecayLabels;
    std::array<KnobLabel, kFilterInstanceCount> combDampingLabels;
    std::array<KnobLabel, kFilterInstanceCount> combDispersionLabels;
    std::array<KnobLabel, kFilterInstanceCount> combDriveLabels;
    std::array<KnobLabel, kFilterInstanceCount> combMixLabels;
    juce::Slider resonance2Knob;
    juce::ComboBox filter2TypeBox;
    juce::Slider attackKnob;
    juce::Slider decayKnob;
    juce::Slider sustainKnob;
    juce::Slider releaseKnob;
    juce::Slider gainKnob;
    juce::Slider lfoFrequencyKnob;
    PanKnob lfoAmountKnob;
    PanKnob subOscPitchKnob;
    PanKnob osc1PitchKnob;
    PanKnob osc2PitchKnob;
    PanKnob osc3PitchKnob;
    juce::ComboBox lfoWaveformBox;
    juce::ComboBox subOscOctaveBox;
    juce::ComboBox subOscWaveformBox;
    juce::ComboBox lfoAssignBox;
    juce::ComboBox envAssignBox;
    px3::ui::BypassButton lfoBypassButton;
    px3::ui::BypassButton envBypassButton;
    px3::ui::BypassButton filter1EnabledButton;
    px3::ui::BypassButton filter2EnabledButton;
    px3::ui::BypassButton osc1EnabledButton;
    px3::ui::BypassButton osc2EnabledButton;
    px3::ui::BypassButton osc3EnabledButton;
    px3::ui::BypassButton subOscEnabledButton;

    KnobLabel osc1MacroALabel;
    KnobLabel osc1MacroBLabel;
    KnobLabel osc1MacroCLabel;
    KnobLabel osc2MacroALabel;
    KnobLabel osc2MacroBLabel;
    KnobLabel osc2MacroCLabel;
    KnobLabel osc3MacroALabel;
    KnobLabel osc3MacroBLabel;
    KnobLabel osc3MacroCLabel;
    KnobLabel cutoffLabel;
    KnobLabel resonanceLabel;
    KnobLabel filter1TypeLabel;
    KnobLabel cutoff2Label;
    KnobLabel resonance2Label;
    KnobLabel filter2TypeLabel;
    KnobLabel attackLabel;
    KnobLabel decayLabel;
    KnobLabel sustainLabel;
    KnobLabel releaseLabel;
    KnobLabel gainLabel;
    KnobLabel lfoFrequencyLabel;
    KnobLabel lfoAmountLabel;
    KnobLabel lfoWaveformLabel;
    KnobLabel subOscPitchLabel;
    KnobLabel osc1PitchLabel;
    KnobLabel osc2PitchLabel;
    KnobLabel osc3PitchLabel;
    juce::Label subOscPitchValueLabel;
    juce::Label osc1PitchValueLabel;
    // One readout per macro knob. Every other knob in the plugin shows the
    // value it is setting; the oscillator macros were the only ones that did
    // not, in any mode.
    juce::Label osc1MacroAValueLabel;
    juce::Label osc1MacroBValueLabel;
    juce::Label osc1MacroCValueLabel;
    juce::Label osc2MacroAValueLabel;
    juce::Label osc2MacroBValueLabel;
    juce::Label osc2MacroCValueLabel;
    juce::Label osc3MacroAValueLabel;
    juce::Label osc3MacroBValueLabel;
    juce::Label osc3MacroCValueLabel;
    juce::Label osc2PitchValueLabel;
    juce::Label osc3PitchValueLabel;
    KnobLabel subOscOctaveLabel;
    KnobLabel subOscWaveformLabel;
    KnobLabel envAssignLabel;
    juce::Label lfoFrequencyValueLabel;
    juce::Label lfoAmountValueLabel;
    juce::Label envAmountValueLabel;
    KnobLabel lfoAssignLabel;
    KnobLabel midiStatusLabel;
    std::unique_ptr<OscPanel> oscPanel;
    std::unique_ptr<ModPanel> modPanel;
    // The OSC panel is hosted in a viewport so panels.osc can declare a fixed
    // height and vertical scrolling. The viewport is always present; when the
    // panel is not scrolling it simply shows it at full size, which avoids
    // re-parenting the panel every time the config changes.
    // Space between a scrolling panel's content and its scrollbar. Without it
    // the cards sit flush against the bar, which reads as a rendering fault.
    static constexpr int kScrollBarGutter = 8;

    juce::Viewport oscPanelViewport;
    juce::Viewport modPanelViewport;
    std::unique_ptr<AmpPanel> ampPanel;
    std::unique_ptr<FltPanel> fltPanel;
    std::unique_ptr<FxPanel> fxPanel;
    // Not owned here - the panel takes them. Kept as raw pointers so the
    // refresh passes can reach their controls.
    px3::ui::FxCardComponent* doomCard { nullptr };
    px3::ui::FxCardComponent* lucyCard { nullptr };
    std::unique_ptr<MixPanel> mixPanel;
    std::unique_ptr<TopMenuBar> topMenuBar;

    juce::Slider vibeAmountKnob;
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
    px3::ui::BypassButton moodBypassButton;
    px3::ui::ToggleChipButton moodFreezeButton;
    juce::Slider moodMixKnob;
    KnobLabel moodMixLabel;
    juce::Slider moodClockKnob;
    KnobLabel moodClockLabel;
    juce::Slider moodWetTimeKnob;
    KnobLabel moodWetTimeLabel;
    juce::Slider moodWetModifyKnob;
    KnobLabel moodWetModifyLabel;
    juce::Slider moodLoopLengthKnob;
    KnobLabel moodLoopLengthLabel;
    juce::Slider moodLoopModifyKnob;
    KnobLabel moodLoopModifyLabel;
    juce::Slider moodFeedbackKnob;
    KnobLabel moodFeedbackLabel;
    juce::Slider moodSpreadKnob;
    KnobLabel moodSpreadLabel;
    juce::Slider moodDegradeKnob;
    KnobLabel moodDegradeLabel;
    juce::ComboBox moodRoutingBox;
    KnobLabel moodRoutingLabel;
    juce::ComboBox moodWetModeBox;
    KnobLabel moodWetModeLabel;
    juce::ComboBox moodLoopModeBox;
    KnobLabel moodLoopModeLabel;
    px3::ui::BypassButton robBypassButton;
    px3::ui::BypassButton delayBypassButton;
    px3::ui::BypassButton reverbBypassButton;

    PresetManager presetManager;
    std::vector<PresetManager::PresetRecord> presetFiltered;
    PresetManager::PresetRecord currentPreset;
    bool hasCurrentPreset { false };
    bool currentPresetDirty { false };
    juce::String loadedStateHash;
    int dirtyUpdateCounter { 0 };

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

    std::array<KnobBinding, 24> knobBindings {};
    int lastGranularModeIndex { -1 };
    int lastLfoAssignmentIndex { -1 };
    int lastEnvelopeAssignmentIndex { -1 };

    juce::Label debugPerformanceOverlayLabel;
    juce::Rectangle<int> debugPerformanceOverlayArea;
    uint32_t debugPerformanceOverlayLastUpdateMs { 0 };

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

    UIConfigManager uiConfigManager;
    std::shared_ptr<const UIConfig> uiConfig;
    uint32_t uiConfigLastErrorLogMs { 0 };
};
