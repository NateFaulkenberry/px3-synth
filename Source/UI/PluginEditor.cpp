#include "MacroLook.h"
#include "ParameterKnob.h"
#include "PluginEditor.h"
#include "../Update/UpdateService.h"
#include "KnobOverlays.h"
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
#include "EditorSections.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>


using namespace px3::ui;

namespace
{
static_assert(kSectionSettings == PX3SynthAudioProcessor::kTopMenuViewCount - 1,
              "The processor's view count and the editor's section indices must agree.");
static_assert(kSectionSettings == TopMenuBar::kSettingsSection,
              "The bar and the editor must agree on which index SETTINGS is.");

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
    px3::ui::attachParameterKnob(parameter, slider, sliderAttachments);
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
    // The constructor was 1,270 lines. These eleven calls are that function,
    // in the order it ran them, and the ORDER IS THE CONTRACT: two steps here
    // depend on an earlier one having happened, and both fail silently rather
    // than loudly. buildPanels() must precede the wavetable controls being
    // handed out, and finishConstruction() needs every panel to exist.
    // EditorOrder_* in TestsEditorLayout.cpp is what holds them.

    buildImagesAndMasks();
    buildKeyboardCallbacks();
    buildParameterKnobs();
    buildEnvelopeAndLfoControls();
    buildSelectors();
    buildEffectControls();
    buildPanels();
    buildSettingsAndOverlays();
    buildTopMenuBar();
    buildPresetBar();
    finishConstruction();
}

PX3SynthAudioProcessorEditor::~PX3SynthAudioProcessorEditor()
{
    stopTimer();

    // Off the global preference's list before anything else. The settings
    // service outlives every editor, so a listener left registered here is a
    // call into freed memory the next time any OTHER window changes the
    // setting - which is exactly the case closing one window and toggling from
    // another produces.
    px3::GlobalSettings::getInstance().removeChangeListener(&animationPreferenceListener);

    // The update service outlives every editor too.
    px3::update::UpdateService::getInstance().removeChangeListener(&updateStateListener);

    // Before anything else: the processor outlives us and would otherwise call
    // into a destroyed editor on the next CC. Select Mode is UI state, so it
    // is dropped with the window; the mappings themselves are not.
    audioProcessor.onMidiMappingAssigned = nullptr;
    audioProcessor.setMidiLearnTargets({});

    for (auto& knob : midiKnobs)
    {
        if (auto* slider = knob.getComponent())
        {
            slider->removeMouseListener(&midiSelectListener);
        }
    }
    midiKnobs.clear();

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


void PX3SynthAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();

    // The depth panel is checked first because its scrim is the thing that
    // routed this click here. A click inside the panel never arrives - the
    // panel is above the scrim and handles its own - so anything reaching
    // this point is outside it, and the click is spent on the dismissal
    // rather than passed through to whatever sits underneath.
    if (depthPanelMacroIndex() >= 0)
    {
        // A command-click on ANOTHER macro knob switches straight to it. The
        // scrim is what routed this click here, so without this the click is
        // spent closing the panel and the user has to click the second knob
        // again - which is a dismissal getting in the way of the gesture it
        // was meant to protect.
        if (event.mods.isCommandDown())
        {
            if (auto* slider = findParameterKnobAt(point))
            {
                const auto parameterId = px3::ui::parameterIdOf(*slider);

                for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
                {
                    if (parameterId == PX3SynthAudioProcessor::macroParameterId(macro))
                    {
                        if (macro != depthPanelMacroIndex()) { openMacroDepthPanel(macro); }
                        return;
                    }
                }
            }
        }

        closeMacroDepthPanel();
        return;
    }

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


void PX3SynthAudioProcessorEditor::toggleSettingsView()
{
    // A true toggle: the gear opens SETTINGS and closes it again. Closing
    // returns to the panel you came from rather than to a fixed one, because
    // SETTINGS is a detour - you were doing something before you opened it.
    if (selectedTopMenuSection == kSectionSettings)
    {
        applyTopMenuSectionSelection(sectionBeforeSettings, true);
        return;
    }

    sectionBeforeSettings = selectedTopMenuSection;
    applyTopMenuSectionSelection(kSectionSettings, true);
}

void PX3SynthAudioProcessorEditor::applyTopMenuSectionSelection(int sectionIndex, bool pushToProcessor)
{
    const auto clamped = juce::jlimit(0, kSectionSettings, sectionIndex);
    selectedTopMenuSection = clamped;

    if (topMenuBar != nullptr)
    {
        topMenuBar->setSelectedSection(clamped);
    }

    updatePanelVisibility();

    // And RE-LAY OUT, not just re-show. Which rows the window has depends on
    // the section: SETTINGS drops the macro strip and takes its width back for
    // the panel, and that decision lives in resized(). Without this, choosing
    // SETTINGS left the strip on screen and the panel at its narrower size
    // until something else happened to resize the window.
    resized();

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
    else if (clamped == kSectionSettings)
    {
        if (settingsPanel != nullptr) { settingsPanel->refreshFromParameters(); }
    }

    if (pushToProcessor)
    {
        audioProcessor.setTopMenuViewIndex(clamped, true);
    }

    // Opening SETTINGS is what counts as having seen an update, so the gear
    // stops glowing there rather than on any click anywhere.
    refreshUpdateAffordances();
}

void PX3SynthAudioProcessorEditor::refreshTopMenuSelectionFromProcessor()
{
    const auto processorIndex = audioProcessor.getTopMenuViewIndex();
    if (processorIndex != selectedTopMenuSection)
    {
        applyTopMenuSectionSelection(processorIndex, false);
    }
}
