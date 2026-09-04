#pragma once

#include "FxChain.h"
#include "MacroKnobLook.h"
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
#include "MacroStrip.h"
#include "MacroDepthPanel.h"
#include "KnobLookAndFeel.h"
#include "ParameterKnob.h"
#include "PianoKeyboard.h"
#include "PresetManager.h"
#include "PluginProcessor.h"
#include "ModPanel.h"
#include "AmpPanel.h"
#include "FltPanel.h"
#include "FxPanel.h"
#include "MixPanel.h"
#include "BusInsertOverlay.h"
#include "ModalBackdrop.h"
#include "OscPanel.h"
#include "GlobalSettings.h"
#include "SettingsPanel.h"
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
    // Escape leaves Select Mode without assigning anything.
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    // The shared knob, under the name this editor has always used - so every
    // reference below is unchanged and the drawing is the ecosystem's.
    using KnobLookAndFeel = px3::ui::KnobLookAndFeel;

    // The macro knobs are drawn by their own look, so they read as a different
    // kind of control from every knob inside the panels.
    px3::ui::MacroKnobLookAndFeel macroKnobLookAndFeel;

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

    // Covers the whole editor while the preset sheet is open, so nothing behind
    // it can be clicked. The sheet was only ever modal to look at:
    // paintOverChildren dimmed the UI while every knob, card, chip and key
    // underneath stayed live, so a click that missed the sheet edited the patch
    // you were browsing away from.
    //
    // Mouse events are forwarded to the editor rather than swallowed, because
    // the editor is where the sheet's own mouse behaviour already lives - the
    // panel does not intercept clicks on its own background, so its title-bar
    // drag and its click-outside-to-close both arrive as editor events. With
    // the scrim in the way those clicks would otherwise stop here.
    class ModalDismissScrim final : public juce::Component
    {
    public:
        explicit ModalDismissScrim(juce::Component& ownerIn) : owner(ownerIn) {}

        void mouseDown(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseDown); }
        void mouseDrag(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseDrag); }
        void mouseUp(const juce::MouseEvent& e) override { forward(e, &juce::Component::mouseUp); }

    private:
        void forward(const juce::MouseEvent& e, void (juce::Component::*handler)(const juce::MouseEvent&))
        {
            (owner.*handler)(e.getEventRelativeTo(&owner));
        }

        juce::Component& owner;
    };

    //==========================================================================
    // MIDI mapping Select Mode. See docs/midi-mapping-design.md.
    //
    // The selection is UI state and lives here; the mappings themselves live
    // on the processor, which is why they survive this window closing.
    //==========================================================================

    // Listens on every parameter knob so the editor's own mouse handling is
    // left alone. A shift-click reaches handleParameterKnobClick; everything
    // else falls through to the slider exactly as before.
    struct MidiSelectListener final : public juce::MouseListener
    {
        explicit MidiSelectListener(PX3SynthAudioProcessorEditor& ownerIn) : owner(ownerIn) {}
        void mouseDown(const juce::MouseEvent& event) override
        {
            owner.handleParameterKnobClick(event);
        }
        void mouseDoubleClick(const juce::MouseEvent& event) override
        {
            owner.handleParameterKnobDoubleClick(event);
        }
        PX3SynthAudioProcessorEditor& owner;
    };
    MidiSelectListener midiSelectListener { *this };

    //==========================================================================
    // Macro assignment. See docs/macro-system-design.md.
    //==========================================================================

    // A transparent sheet over the whole editor while assigning.
    //
    // A juce::MouseListener cannot consume an event, so without this every
    // assignment click would ALSO drag the knob it landed on - which is the
    // one thing §13 says must not happen. The sheet takes the click, finds the
    // knob underneath and toggles it, and the knob never sees the mouse.
    struct MacroAssignOverlay final : public juce::Component
    {
        explicit MacroAssignOverlay(PX3SynthAudioProcessorEditor& ownerIn) : owner(ownerIn)
        {
            setInterceptsMouseClicks(true, false);
            setWantsKeyboardFocus(true);
        }
        void mouseDown(const juce::MouseEvent& event) override
        {
            owner.handleMacroAssignClick(event.getEventRelativeTo(&owner).getPosition());
        }
        void paint(juce::Graphics&) override {}
        PX3SynthAudioProcessorEditor& owner;
    };

    std::unique_ptr<MacroStrip> macroStrip;
    std::unique_ptr<MacroAssignOverlay> macroAssignOverlay;
    std::unique_ptr<px3::ui::MacroDepthPanel> macroDepthPanel;
    ModalDismissScrim macroDepthScrim { *this };
    juce::Rectangle<int> macroStripArea;

    // The macro interactions are ONE state, not several flags.
    //
    // Assigning and editing depths are alternatives: entering either leaves
    // the other. Held as two booleans and two indices they would also spell
    // out combinations that mean nothing - assigning macro 1 while the depth
    // panel shows macro 2 - which every call site would then have to avoid by
    // hand. A state that cannot represent those is less to get wrong.
    enum class MacroUiMode { normal, assigning, depth };
    struct MacroUiState
    {
        MacroUiMode mode { MacroUiMode::normal };
        int macro { -1 };
    };
    MacroUiState macroUi;

    int assigningMacroIndex() const noexcept
    { return macroUi.mode == MacroUiMode::assigning ? macroUi.macro : -1; }
    int depthPanelMacroIndex() const noexcept
    { return macroUi.mode == MacroUiMode::depth ? macroUi.macro : -1; }

    void enterMacroAssignMode(int macroIndex);
    // THE way out of assignment mode. The macro knob, a background click and
    // Enter all call this one function, so the three cannot come to mean three
    // slightly different things.
    void finishMacroAssignEditing();
    void openMacroDepthPanel(int macroIndex);
    void closeMacroDepthPanel();
    // True if the click landed on something the user meant to operate rather
    // than on chrome. Used to decide whether a click finishes assignment mode.
    bool isAssignableTargetAt(juce::Point<int> positionInEditor) const;
    void layoutMacroDepthPanel();
    void handleMacroAssignClick(juce::Point<int> positionInEditor);
    juce::Slider* findParameterKnobAt(juce::Point<int> positionInEditor) const;

    juce::StringArray midiSelection;
    std::vector<juce::Component::SafePointer<juce::Slider>> midiKnobs;

    void handleParameterKnobClick(const juce::MouseEvent& event);
    void handleParameterKnobDoubleClick(const juce::MouseEvent& event);
    // Arms the macro this parameter ID belongs to, if it belongs to one.
    // Shared by the Cmd-click and double-click gestures so the two cannot
    // drift apart.
    bool tryEnterMacroAssignModeFor(const juce::String& parameterId);

public:
    // For the tests: what Select Mode currently holds, and the pass that
    // publishes it to the knobs. Read-only views of UI state - nothing here
    // is a second copy of anything.
    juce::StringArray debugMidiSelection() const { return midiSelection; }
    void debugRefreshMidiMappingUI() { refreshMidiMappingUI(); }
    juce::String debugKeyboardNotice() const { return pianoKeyboard.getNotice(); }
    PianoKeyboard::BannerFit debugKeyboardBannerFit(const juce::String& text)
    { return pianoKeyboard.debugBannerFit(text); }

    // For the tests: the macro strip and the assignment state.
    MacroStrip* debugMacroStrip() const { return macroStrip.get(); }
    bool debugUpdateNoticeVisible() const { return updateNotice.isVisible(); }
    juce::String debugUpdateNoticeText() const { return updateNotice.getText(); }
    void debugTimerTick() { timerCallback(); }
    void debugCloseMacroDepthPanel() { closeMacroDepthPanel(); }
    void debugOpenMacroDepthPanel(int macroIndex) { openMacroDepthPanel(macroIndex); }

    // The knob carrying a parameter ID, found by walking the editor - the same
    // way MIDI mapping finds them, so a knob this cannot find is a knob those
    // gestures cannot reach either.
    juce::Slider* debugFindKnobForParameter(const juce::String& parameterId)
    {
        juce::Slider* found = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (child == nullptr || found != nullptr) { continue; }
                if (auto* slider = dynamic_cast<juce::Slider*>(child))
                {
                    if (px3::ui::parameterIdOf(*slider) == parameterId) { found = slider; return; }
                }
                walk(*child);
            }
        };
        walk(*this);
        return found;
    }
    int debugAssigningMacro() const { return assigningMacroIndex(); }
    int debugDepthPanelMacro() const { return depthPanelMacroIndex(); }
    px3::ui::MacroDepthPanel* debugMacroDepthPanel() { return macroDepthPanel.get(); }
    void debugFinishMacroAssignEditing() { finishMacroAssignEditing(); }
    juce::Rectangle<int> debugMacroStripArea() const { return macroStripArea; }
    juce::Rectangle<int> debugMacroOverlayBounds() const
    { return macroAssignOverlay != nullptr ? macroAssignOverlay->getBounds() : juce::Rectangle<int>(); }
    juce::Rectangle<int> debugKeyboardBounds() const { return pianoKeyboard.getBounds(); }
    juce::Rectangle<int> debugTopMenuBounds() const { return topMenuStripArea; }
    juce::Rectangle<int> debugPanelArea() const { return panelViewportArea; }
    // Switch panel the way the top menu does, so a test exercises the real
    // path rather than poking the index.
    void debugSelectSection(int sectionIndex) { applyTopMenuSectionSelection(sectionIndex, false); }
    // What a click at this point would land on - the same hit test the overlay
    // uses, so a test can pick knobs that are actually reachable right now.
    juce::Slider* debugKnobAt(juce::Point<int> positionInEditor) const
    { return findParameterKnobAt(positionInEditor); }
    void debugEnterMacroAssignMode(int macroIndex) { enterMacroAssignMode(macroIndex); }
    void debugExitMacroAssignMode() { finishMacroAssignEditing(); }
    void debugMacroAssignClickOn(juce::Component& target)
    {
        handleMacroAssignClick(
            getLocalPoint(&target, target.getLocalBounds().getCentre()));
    }

    // How many knobs the editor has registered its listener on. The wiring
    // itself - addMouseListener - is what a test cannot drive through JUCE's
    // dispatch headlessly, so it is asserted by count instead.
    int debugRegisteredKnobCount() const { return static_cast<int>(midiKnobs.size()); }

    // The shared rotary look-and-feel, so a test can render one knob through
    // the same code every knob in the synth is drawn by.
    juce::LookAndFeel* debugKnobLookAndFeel() { return &knobLookAndFeel; }

    // The macro knobs' own look, so a test can render one through exactly what
    // draws them on screen.
    px3::ui::MacroKnobLookAndFeel* debugMacroKnobLookAndFeel() { return &macroKnobLookAndFeel; }

    // The preset dump fields in the debug panel, for the tests.
    // The debug panel is only wired up in a PX3_DEBUG_PANEL build, so its
    // controls are unconfigured in a normal one. The tests run the setup
    // themselves rather than being confined to a build that nobody runs the
    // suite in - which would leave this code with no coverage at all.
    void debugForceSetupPanel() { setupDebugPanel(); }

    juce::TextEditor& debugPresetNameField() { return debugDumpPresetNameEditor; }
    juce::TextEditor& debugPresetAuthorField() { return debugDumpPresetAuthorEditor; }
    juce::ComboBox& debugPresetCategoryField() { return debugDumpPresetCategoryBox; }
    juce::TextButton& debugPresetDumpButton() { return debugDumpPresetButton; }
    PresetManager::PresetMetadata debugPresetDumpMetadata() const { return debugDumpPresetMetadata(); }

    // The two components that are TOLD whether to animate, so a test can check
    // the preference reaches them through the editor rather than only that
    // their own setters work.
    PianoKeyboard& debugPianoKeyboard() { return pianoKeyboard; }
    PerformanceControls& debugPerformanceControls() { return performanceControls; }

    TopMenuBar* debugTopMenuBar() { return topMenuBar.get(); }
    SettingsPanel* debugSettingsPanel() { return settingsPanel.get(); }
    juce::Rectangle<int> debugPanelViewportArea() const { return panelViewportArea; }
    int debugSelectedSection() const { return selectedTopMenuSection; }

    // The same call JUCE's dispatch makes when a listener sees a click on a
    // knob. Constructing the event here is the one step the test cannot get
    // JUCE to do for it.
    void debugSimulateKnobClick(juce::Slider& slider, juce::ModifierKeys mods)
    {
        const auto at = slider.getLocalBounds().getCentre().toFloat();
        handleParameterKnobClick(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), at, mods,
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &slider, &slider,
            juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false));
    }

    // Shift is the common case and reads better at the call site. Kept as an
    // overload rather than the only form, because the modifier that opens the
    // depth panel could not be expressed before and so was never tested.
    void debugSimulateKnobClick(juce::Slider& slider, bool shiftDown)
    {
        debugSimulateKnobClick(slider, shiftDown
                                           ? juce::ModifierKeys(juce::ModifierKeys::shiftModifier)
                                           : juce::ModifierKeys());
    }

    // The command key, whichever key that is on this platform.
    void debugSimulateKnobCommandClick(juce::Slider& slider)
    {
        debugSimulateKnobClick(slider, juce::ModifierKeys(juce::ModifierKeys::commandModifier));
    }

    bool debugPressKey(const juce::KeyPress& key) { return keyPressed(key); }
    void debugClickEditorAtWithModifiers(juce::Point<int> position, juce::ModifierKeys mods)
    {
        const auto at = position.toFloat();
        mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                   at, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   this, this, juce::Time::getCurrentTime(), at,
                                   juce::Time::getCurrentTime(), 1, false));
    }
    void debugClickEditorAt(juce::Point<int> position)
    {
        const auto at = position.toFloat();
        mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                   at, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                   this, this, juce::Time::getCurrentTime(), at,
                                   juce::Time::getCurrentTime(), 1, false));
    }
    void debugMacroAssignClickAt(juce::Point<int> position) { handleMacroAssignClick(position); }

    // The same call JUCE's dispatch makes when a listener sees a double-click.
    void debugSimulateKnobDoubleClick(juce::Slider& slider)
    {
        const auto at = slider.getLocalBounds().getCentre().toFloat();
        handleParameterKnobDoubleClick(juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(),
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &slider, &slider,
            juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 2, false));
    }

private:
    void refreshMidiMappingUI();
    void endMidiSelectMode();
    bool isMidiSelectModeActive() const { return ! midiSelection.isEmpty(); }

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
    void buildChorusCard();
    void buildStereoSpreadCard();
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

    // The bus insert sheets. One EQ and one compressor between them, retargeted
    // to whichever bus asked - a third bus needs no new component here.
    void openBusInsert(int bus, bool wantsEq);
    void closeBusInsert();
    juce::Component* activeBusInsertSheet() const;
    void showPresetError(const juce::String& title, const juce::String& message);
    void savePreset(bool saveAs);
    void importPreset();
    void exportCurrentPreset();
    void showPresetMenu();
    void applyTopMenuSectionSelection(int sectionIndex, bool pushToProcessor);
    // Open SETTINGS, or close it and go back to where you were.
    void toggleSettingsView();
    // Pushes the animation preference down to the things that animate. One
    // call site for the flag, so the three of them cannot drift apart.
    void applyAnimationPreference();

    //---- update notice ----------------------------------------------------
    //
    // The gear glows while an update is waiting to be seen, and a line under
    // it says so once, on the first frame after one is found. Both are views
    // of UpdateService's state; neither keeps a copy of it.
    void refreshUpdateAffordances();
    void dismissUpdateNotice();

    juce::Label updateNotice;
    // Counts down the notice's own life. -1 when it is not showing.
    int updateNoticeFramesLeft { -1 };
    // So the notice appears once per window rather than every time a check
    // happens to land on the same answer.
    bool updateNoticeShown { false };

    // Told when the global animation preference moves, rather than asking every
    // tick. One notification per change beats thirty polls a second finding
    // nothing has happened.
    struct AnimationPreferenceListener final : public juce::ChangeListener
    {
        explicit AnimationPreferenceListener(PX3SynthAudioProcessorEditor& ownerIn)
            : owner(ownerIn) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            owner.applyAnimationPreference();
        }
        PX3SynthAudioProcessorEditor& owner;
    };

    AnimationPreferenceListener animationPreferenceListener { *this };

    // The update service publishes its state the same way. One listener rather
    // than polling: the gear lights up when a check finishes, whenever that is.
    struct UpdateStateListener final : public juce::ChangeListener
    {
        explicit UpdateStateListener(PX3SynthAudioProcessorEditor& ownerIn) : owner(ownerIn) {}
        void changeListenerCallback(juce::ChangeBroadcaster*) override
        {
            owner.refreshUpdateAffordances();
        }
        PX3SynthAudioProcessorEditor& owner;
    };

    UpdateStateListener updateStateListener { *this };
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

        // A control is one of three shapes. A header is a caption with no
        // control under it; a chooser uses the box instead of the slider.
        std::unique_ptr<juce::ComboBox> box;
        std::unique_ptr<juce::TextButton> button;
        bool isHeader { false };
    };

    // Builds one slider-shaped debug control. The panel had thirty lines of
    // boilerplate per control, which is fine for six and not for twenty.
    void buildAnalogEngineDebugControls();
    void refreshAnalogEngineDebugControls();

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
    void refreshDebugUpdateStatus();
    void refreshDebugLfoState();
    void refreshDebugEnvelopeState();
    void refreshDebugPerformanceOverlay();
    void debugCaptureSnapshot(const juce::String& reason);
    void debugCompareWithSnapshot();
    void debugForceSerializationTest();
    void debugDumpPresetToFile();
    // DUMP PRESET needs a name and an author before it can write anything
    // worth keeping, so it is disabled until it has both. Called whenever
    // either field changes.
    void refreshDebugDumpPresetEnablement();
    // The metadata the fields describe, as one value. Pulled out of the file
    // chooser's callback so it can be tested: the chooser is a modal, async
    // dialog and nothing headless can drive it, but the mapping from fields to
    // metadata is the part that can be got wrong.
    PresetManager::PresetMetadata debugDumpPresetMetadata() const;
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
    // Every oscillator source bypassed: the instrument cannot make a sound, so
    // the keyboard and the logo stop pretending it can.
    bool anyOscillatorEngaged { true };
    void refreshOscillatorEngagedState();

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
    std::array<juce::ComboBox, 3> oscWtTableBoxes;
    std::array<KnobLabel, 3> oscWtTableLabels;
    std::array<juce::Slider, 3> oscWtPositionKnobs;
    std::array<KnobLabel, 3> oscWtPositionLabels;
    std::array<juce::Label, 3> oscWtPositionValues;

    // What the graph is currently showing, so the display is rebuilt when the
    // table changes and not sixty times a second.
    std::array<juce::String, 3> shownWavetableNames;

    // The constructor, in eleven pieces - see PluginEditorBuild.cpp. Called in
    // this order and only from there; the order is a contract, not a style.
    void buildImagesAndMasks();
    void buildKeyboardCallbacks();
    void buildParameterKnobs();
    void buildEnvelopeAndLfoControls();
    void buildSelectors();
    void buildEffectControls();
    void buildPanels();
    void buildSettingsAndOverlays();
    void buildTopMenuBar();
    void buildPresetBar();
    void finishConstruction();

    void configureWavetableControls();
    void refreshWavetableDisplays();
    void refreshModulationRings();
    void importWavetableFile(int oscIndex, const juce::File& file);
    void rebuildWavetableMenu(int oscIndex);

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
    px3::ui::FxCardComponent* chorusCard { nullptr };
    px3::ui::FxCardComponent* spreadCard { nullptr };
    std::unique_ptr<MixPanel> mixPanel;
    std::unique_ptr<SettingsPanel> settingsPanel;
    std::unique_ptr<TopMenuBar> topMenuBar;

    juce::Slider vibeAmountKnob;
    juce::ComboBox vibeTypeBox;
    KnobLabel vibeAmountLabel;
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
    // Where the gear was pressed from, so closing SETTINGS returns there. Not
    // a fixed panel: SETTINGS is a detour from whatever you were doing.
    int sectionBeforeSettings { 0 };

    PresetBrowserPanelComponent presetBrowserPanel;
    juce::Label presetBrowserTitle;
    juce::TextEditor presetSearchEditor;
    juce::ComboBox presetScopeBox;
    juce::ComboBox presetCategoryBox;
    juce::ListBox presetListBox;
    juce::TextButton presetBrowserLoadButton;
    juce::TextButton presetBrowserCloseButton;
    juce::Label presetBrowserDetails;
    // Draws BOTH the keyboard's sparks and the wheels' sparkles, above both
    // components. They overlap each other, so z-order alone can never let both
    // spill over the other - whichever went in front hid the other's particles
    // behind its own opaque face. One transparent layer above the pair is the
    // only arrangement where neither loses, and it takes no mouse events at
    // all, so nothing underneath it changes behaviour.
    class SparkOverlay final : public juce::Component
    {
    public:
        SparkOverlay(PianoKeyboard& keysIn, PerformanceControls& wheelsIn)
            : keys(keysIn), wheels(wheelsIn)
        {
            setName("SparkOverlay");
            setInterceptsMouseClicks(false, false);
        }

        void paint(juce::Graphics& g) override
        {
            keys.paintSparksInto(g, keys.getPosition() - getPosition());
            wheels.paintSparklesInto(g, wheels.getPosition() - getPosition());
        }

        // Repaints only where particles actually are.
        //
        // This layer is transparent, so invalidating it redraws everything
        // beneath it as well - the mixer panel, the keys, the wheels. Doing
        // that for the whole strip measured 3.6 ms a frame, 22% of a 60 Hz
        // budget spent redrawing things that had not changed. A burst from one
        // key occupies a small fraction of the strip, and this repaints that
        // fraction.
        void repaintParticles()
        {
            const auto region = boundsOf(keys.sparkBounds(), keys.getPosition())
                                    .getUnion(boundsOf(wheels.sparkleBounds(), wheels.getPosition()));

            // The union with LAST frame's region, not just this one.
            //
            // Two things break without it. Particles move, so invalidating
            // only where they are now leaves the previous frame still drawn
            // where they were - a trail. And when the last particle dies the
            // current region is empty, so nothing would be invalidated at all
            // and the final frame would stay on screen for good.
            const auto invalid = region.getUnion(previousParticleRegion);
            previousParticleRegion = region;

            if (invalid.isEmpty())
            {
                return;
            }

            repaint(invalid.getIntersection(getLocalBounds()));
        }

        // Exposed so the invalidated area can be asserted rather than inferred
        // from what appears on screen.
        juce::Rectangle<int> lastParticleRegion() const { return previousParticleRegion; }

    private:
        // Translates a child's particle box into this overlay's coordinates,
        // returning an empty rectangle unchanged so an absent set of particles
        // does not drag the union to the origin.
        juce::Rectangle<int> boundsOf(juce::Rectangle<float> box, juce::Point<int> childOrigin) const
        {
            if (box.isEmpty())
            {
                return {};
            }

            return box.getSmallestIntegerContainer()
                .translated(childOrigin.getX() - getX(), childOrigin.getY() - getY());
        }


        PianoKeyboard& keys;
        PerformanceControls& wheels;
        juce::Rectangle<int> previousParticleRegion;
    };

    SparkOverlay sparkOverlay { pianoKeyboard, performanceControls };

    ModalDismissScrim presetBrowserScrim { *this };
    std::unique_ptr<px3::ui::BusEqOverlay> busEqOverlay;
    std::unique_ptr<px3::ui::BusCompOverlay> busCompOverlay;
    px3::ui::ModalScrim busInsertScrim { *this };
    juce::Image busInsertBackdropSnapshot;
    bool busInsertVisible { false };
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
    uint32_t debugPerformanceOverlayLastUpdateMs { 0 };
    // Host RSS when this editor opened, so the overlay can show the change
    // rather than only the absolute figure. See processResidentMemoryMb.
    double debugHostRssBaselineMb { 0.0 };
    // How far the wheels' sparkles may spill past their strip, on the three
    // sides that are not the shared headroom. Clamped to the window in
    // resized().
    int performanceSparkSpill { 112 };
    // The keyboard's own headroom above the keys, read from the config and
    // clamped to the panel area in resized().
    int keyboardSparkHeadroom { 112 };

    juce::Component debugPanel;

    // The sections are taller than the window on any laptop screen, so they
    // scroll rather than forcing the window to be dragged out to reach the last
    // of them. The title row and the action buttons stay put above it - they
    // are how the panel is driven, and scrolling them away to press a button
    // would be worse than the problem being solved.
    juce::Viewport debugSectionsViewport;
    juce::Component debugSectionsContent;
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
    juce::Label debugUpdateLabel;
    juce::Label debugSnapshotLabel;
    juce::Label debugLfoLabel;
    juce::Label debugLfoAssignLabel;
    juce::Label debugEnvelopeLabel;
    juce::Label debugPresetToolsLabel;
    juce::Label debugDumpPresetNameLabel;
    juce::Label debugDumpPresetAuthorLabel;
    juce::Label debugDumpPresetCategoryLabel;
    juce::TextEditor debugInstanceText;
    juce::TextEditor debugModuleOrderText;
    juce::TextEditor debugValueTreeText;
    juce::TextEditor debugSerializedText;
    juce::TextEditor debugParameterInspectorText;
    juce::TextEditor debugEventLogText;
    juce::TextEditor debugUpdateText;
    juce::TextEditor debugSnapshotText;
    juce::TextEditor debugLfoText;
    juce::TextEditor debugEnvelopeText;
    juce::TextEditor debugDumpPresetNameEditor;
    juce::TextEditor debugDumpPresetAuthorEditor;
    juce::ComboBox debugDumpPresetCategoryBox;
    juce::ComboBox debugLfoAssignBox;
    juce::TextButton debugDumpPresetButton;
    juce::Viewport debugParamViewport;
    juce::Component debugParamContent;
    std::vector<std::unique_ptr<DebugParamControl>> debugParamControls;

    // AnalogEngine gets its own list and viewport. Its constants are internal
    // tuning rather than parameters, so mixing them into the parameter panel
    // would blur exactly the distinction the engine depends on.
    juce::Viewport debugAnalogViewport;
    juce::Component debugAnalogContent;
    std::vector<std::unique_ptr<DebugParamControl>> debugAnalogControls;
    bool debugAnalogControlsInitialized { false };
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
