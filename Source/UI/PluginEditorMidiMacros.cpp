// Select Mode: the two assignment gestures, MIDI learn and Macro assign.
//
// Split out of PluginEditor.cpp, which had grown to 4,400 lines. These are
// member functions of the same class, so this needs no change to the header -
// PluginEditorLook.cpp and PluginEditorDebug.cpp work the same way.
//
// One contiguous block: shift-click to map a CC, cmd-click to assign a Macro,
// the hit test both use, and the refresh that redraws every knob's ring and
// label afterwards. See docs/midi-mapping-design.md and
// docs/macro-system-design.md.

#include "PluginEditor.h"
#include "MacroLook.h"
#include "EditorSections.h"
#include "../Update/UpdateService.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "../DSP/PluginProcessorInternals.h"

#include <algorithm>
#include <cmath>

//==============================================================================
// MIDI mapping Select Mode. See docs/midi-mapping-design.md.
//==============================================================================

using namespace px3::ui;

void PX3SynthAudioProcessorEditor::handleParameterKnobClick(const juce::MouseEvent& event)
{
    auto* slider = dynamic_cast<juce::Slider*>(event.eventComponent);
    if (slider == nullptr) { return; }

    const auto parameterId = px3::ui::parameterIdOf(*slider);
    if (parameterId.isEmpty()) { return; }

    // Cmd on a MACRO knob opens its depth panel. isCommandDown is already the
    // platform's own command key - Cmd on macOS, Ctrl on Windows and Linux -
    // so this is one gesture rather than a hardcoded Mac one.
    //
    // Assignment mode is NOT this gesture any more; it is the double click,
    // which is unchanged. Checked before Shift so the two modifiers stay
    // separate gestures, and only on a macro knob so Cmd is free elsewhere.
    if (event.mods.isCommandDown())
    {
        for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
        {
            if (parameterId == PX3SynthAudioProcessor::macroParameterId(macro))
            {
                openMacroDepthPanel(macro);
                return;
            }
        }
        return;
    }

    // Shift only, for MIDI Learn. Every other click reaches the slider
    // untouched, so ordinary knob dragging is exactly what it was.
    if (! event.mods.isShiftDown()) { return; }

    if (midiSelection.contains(parameterId))
    {
        // Clicking a selected knob takes it back out. Emptying the set leaves
        // Select Mode, which is how a user backs out of the whole gesture
        // without reaching for Escape.
        midiSelection.removeString(parameterId);
    }
    else
    {
        // Selecting a MAPPED knob drops its assignment there and then. It is
        // destructive by design - it is the only gesture that ends with a
        // parameter unmapped - but not silent: the CC label leaves the knob in
        // the same moment, so the user sees the cost of the click as they make
        // it.
        audioProcessor.clearMidiMappingForParameter(parameterId);
        midiSelection.add(parameterId);
    }

    finishMacroAssignEditing();
    audioProcessor.setMidiLearnTargets(midiSelection);
    refreshMidiMappingUI();
}

void PX3SynthAudioProcessorEditor::endMidiSelectMode()
{
    if (midiSelection.isEmpty()) { return; }

    midiSelection.clear();
    audioProcessor.setMidiLearnTargets({});
    refreshMidiMappingUI();
}

bool PX3SynthAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // The brief asks for Return and keypad Enter separately. JUCE does not
    // expose them separately - it has returnKey and no keypad equivalent, and
    // the platform layers fold the keypad key into it - so one test covers
    // both rather than one covering half.
    const auto isEnter = key == juce::KeyPress::returnKey;

    if (assigningMacroIndex() >= 0 && (key == juce::KeyPress::escapeKey || isEnter))
    {
        // Assignments already clicked stay: each one committed as it was made,
        // so there is no pending set to roll back and Escape and Enter agree.
        // The event is consumed either way, so Enter cannot also reach a
        // button that happens to hold focus behind the overlay.
        finishMacroAssignEditing();
        return true;
    }

    if (depthPanelMacroIndex() >= 0 && (key == juce::KeyPress::escapeKey || isEnter))
    {
        closeMacroDepthPanel();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && isMidiSelectModeActive())
    {
        endMidiSelectMode();
        return true;
    }

    return false;
}

void PX3SynthAudioProcessorEditor::refreshMidiMappingUI()
{
    // Walks the tree rather than holding a list built at construction: panels
    // and overlays create knobs after the editor exists, and a list would only
    // be right until the next one appeared.
    //
    // Registration is idempotent - a listener already added is not added again
    // - so this doubles as the discovery pass.
    std::vector<juce::Component::SafePointer<juce::Slider>> found;

    std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child == nullptr) { continue; }

            if (auto* slider = dynamic_cast<juce::Slider*>(child))
            {
                if (px3::ui::isParameterKnob(*slider))
                {
                    found.push_back(slider);
                }
            }

            walk(*child);
        }
    };
    walk(*this);

    for (auto& knob : found)
    {
        auto* slider = knob.getComponent();
        if (slider == nullptr) { continue; }

        const auto alreadyKnown = std::any_of(midiKnobs.begin(), midiKnobs.end(),
                                              [slider](const auto& known)
                                              { return known.getComponent() == slider; });

        if (! alreadyKnown)
        {
            slider->addMouseListener(&midiSelectListener, false);
            midiKnobs.push_back(knob);
        }

        const auto parameterId = px3::ui::parameterIdOf(*slider);
        const auto cc = audioProcessor.getMidiCcForParameter(parameterId);
        const auto selected = midiSelection.contains(parameterId);

        // Which macros drive this knob, and whether it can be clicked right
        // now. The look-and-feel draws both; the editor only decides them.
        auto macroMask = audioProcessor.getMacroMaskForParameter(parameterId);
        auto assignable = false;

        if (assigningMacroIndex() >= 0)
        {
            auto isMacroKnob = false;
            for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
            {
                isMacroKnob = isMacroKnob
                              || parameterId == PX3SynthAudioProcessor::macroParameterId(macro);
            }
            assignable = ! isMacroKnob;
        }

        const auto shownMask = static_cast<int>(
            slider->getProperties().getWithDefault(px3::knob_properties::macroMask, 0));
        const auto shownAssignable = static_cast<bool>(
            slider->getProperties().getWithDefault(px3::knob_properties::macroAssignable, false));

        if (shownMask != macroMask || shownAssignable != assignable)
        {
            slider->getProperties().set(px3::knob_properties::macroMask, macroMask);
            slider->getProperties().set(px3::knob_properties::macroAssignable, assignable);
            slider->repaint();
        }

        // The modulation ring, on EVERY parameter knob rather than the two
        // dozen that happen to be registered as knobBindings. A macro can be
        // assigned to any knob in the synth, so any knob has to be able to
        // show that something is moving it - the AMP ENV knobs had no ring at
        // all, which made a macro assigned to them look like it did nothing.
        if (auto* parameter = audioProcessor.findRangedParameterById(parameterId))
        {
            const auto modulated = audioProcessor.getModulatedNormalisedValue(*parameter);
            const auto shown = static_cast<double>(
                slider->getProperties().getWithDefault("modulatedPos", -1.0));

            if (std::abs(shown - static_cast<double>(modulated)) > 0.002)
            {
                slider->getProperties().set("modulatedPos", static_cast<double>(modulated));
                slider->repaint();
            }
        }

        // Only on a change: a repaint per knob per frame for a picture that
        // has not moved is how a UI ends up costing more than the synth.
        const auto shownCc = static_cast<int>(
            slider->getProperties().getWithDefault(px3::knob_properties::midiCc, -1));
        const auto shownSelected = static_cast<bool>(
            slider->getProperties().getWithDefault(px3::knob_properties::midiSelected, false));

        if (shownCc != cc || shownSelected != selected)
        {
            slider->getProperties().set(px3::knob_properties::midiCc, cc);
            slider->getProperties().set(px3::knob_properties::midiSelected, selected);
            slider->repaint();
        }
    }

    // The depth panel is a view of the routing, so it follows it: an
    // assignment made or removed while it is open, or a preset load that
    // replaces the whole list, arrives here like everything else.
    if (macroDepthPanel != nullptr && depthPanelMacroIndex() >= 0)
    {
        macroDepthPanel->refreshFromProcessor();
        layoutMacroDepthPanel();
    }

    // One notice, decided in one place. Setting it in enterMacroAssignMode and
    // then calling this was two owners for one string, and this one won.
    if (assigningMacroIndex() >= 0)
    {
        pianoKeyboard.setNotice("Click on knobs to assign them to "
                                + PX3SynthAudioProcessor::macroDisplayName(assigningMacroIndex())
                                + ". Hit Enter to confirm.");
    }
    else if (isMidiSelectModeActive())
    {
        pianoKeyboard.setNotice("Select knobs, then move a MIDI control to assign");
    }
    else
    {
        pianoKeyboard.setNotice({});
    }
}

//==============================================================================
// Macro assignment. See docs/macro-system-design.md.
//==============================================================================

bool PX3SynthAudioProcessorEditor::tryEnterMacroAssignModeFor(const juce::String& parameterId)
{
    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
    {
        if (parameterId == PX3SynthAudioProcessor::macroParameterId(macro))
        {
            enterMacroAssignMode(macro);
            return true;
        }
    }

    return false;
}

// Double-clicking a macro knob does what Cmd-clicking it does. Cmd is the
// documented gesture and stays so; this is the one that gets found without
// reading anything, and it costs nothing because macro knobs deliberately
// carry no double-click-to-default value for it to fight with.
//
// Only reached when NOT already assigning: the overlay covers every knob once
// a macro is armed, so exiting the mode by clicking the same knob still goes
// through the overlay, unchanged.
void PX3SynthAudioProcessorEditor::handleParameterKnobDoubleClick(const juce::MouseEvent& event)
{
    auto* slider = dynamic_cast<juce::Slider*>(event.eventComponent);
    if (slider == nullptr) { return; }

    const auto parameterId = px3::ui::parameterIdOf(*slider);
    if (parameterId.isEmpty()) { return; }

    tryEnterMacroAssignModeFor(parameterId);
}

void PX3SynthAudioProcessorEditor::enterMacroAssignMode(int macroIndex)
{
    if (! juce::isPositiveAndBelow(macroIndex, PX3SynthAudioProcessor::kMacroCount)) { return; }

    // Only one learning mode at a time. A MIDI selection in progress is
    // dropped rather than left armed behind this one, or the next CC would
    // assign parameters the user has stopped thinking about.
    endMidiSelectMode();

    // The two macro states are alternatives. Opening one closes the other, so
    // there is never a depth panel for one macro over an assignment session
    // for another.
    closeMacroDepthPanel();

    macroUi = { MacroUiMode::assigning, macroIndex };

    if (macroStrip != nullptr) { macroStrip->setAssigningMacro(macroIndex); }
    if (macroAssignOverlay != nullptr)
    {
        macroAssignOverlay->setBounds(macroStripArea.getUnion(panelViewportArea));
        macroAssignOverlay->setVisible(true);
        macroAssignOverlay->toFront(true);
    }

    refreshMidiMappingUI();
}

void PX3SynthAudioProcessorEditor::refreshUpdateAffordances()
{
    using namespace px3::update;

    const auto& service = UpdateService::getInstance();
    const auto waiting = service.getState() == UpdateState::updateAvailable;

    // The gear glows while an update is unseen. Opening SETTINGS is what counts
    // as seeing it, so the glow stops there rather than on any click.
    const auto seen = selectedTopMenuSection == kSectionSettings;

    if (topMenuBar != nullptr)
    {
        topMenuBar->setUpdateAvailable(waiting && ! seen);
    }

    if (seen) { dismissUpdateNotice(); }

    // The line under the bar, once per window. It says what the glow cannot -
    // which product, and that there IS a new version - and then gets out of
    // the way on its own.
    if (waiting && ! seen && ! updateNoticeShown)
    {
        const auto release = service.getAvailableRelease();
        const auto product = service.getProduct();
        const auto name = product.displayName.isNotEmpty() ? product.displayName
                                                           : juce::String("PX3 Synth");

        updateNotice.setText("A new version of " + name + " is available!"
                                 + (release.isPreRelease ? "  (pre-release)" : juce::String()),
                             juce::dontSendNotification);
        updateNotice.setVisible(true);
        updateNotice.toFront(false);
        // 20 seconds at the editor's 30 Hz tick.
        updateNoticeFramesLeft = 30 * 20;
        updateNoticeShown = true;
        resized();
    }
}

void PX3SynthAudioProcessorEditor::dismissUpdateNotice()
{
    if (updateNoticeFramesLeft < 0 && ! updateNotice.isVisible()) { return; }

    updateNoticeFramesLeft = -1;
    updateNotice.setVisible(false);
}

void PX3SynthAudioProcessorEditor::finishMacroAssignEditing()
{
    if (assigningMacroIndex() < 0) { return; }

    // "Commit" is an assertion here rather than a flush. Each assignment is
    // written to the processor at the moment it is clicked, so there is no
    // pending set that could be lost - which is exactly why the macro knob,
    // a background click and Enter can all end the session identically. If a
    // staged edit is ever introduced, this is the one place that has to learn
    // to flush it.
    macroUi = {};

    if (macroStrip != nullptr) { macroStrip->setAssigningMacro(-1); }
    if (macroAssignOverlay != nullptr) { macroAssignOverlay->setVisible(false); }

    refreshMidiMappingUI();
}

void PX3SynthAudioProcessorEditor::openMacroDepthPanel(int macroIndex)
{
    if (! juce::isPositiveAndBelow(macroIndex, PX3SynthAudioProcessor::kMacroCount)) { return; }
    if (macroDepthPanel == nullptr) { return; }

    // Assigning and editing depths are alternatives, and entering either ends
    // the other - through the one commit path, so a session finished this way
    // is finished the same as one ended with Enter.
    finishMacroAssignEditing();
    endMidiSelectMode();

    macroUi = { MacroUiMode::depth, macroIndex };

    macroDepthPanel->setUIConfig(uiConfig);
    macroDepthPanel->setAccentColour(px3::ui::macroAccentColour(uiConfig.get()));
    macroDepthPanel->setMacro(macroIndex);
    layoutMacroDepthPanel();

    // The scrim goes on first so the panel sits above it, and it is what turns
    // a click anywhere else into a dismissal WITHOUT that click also reaching
    // the control underneath.
    macroDepthScrim.setBounds(getLocalBounds());
    macroDepthScrim.setVisible(true);
    macroDepthScrim.toFront(false);
    macroDepthPanel->setVisible(true);
    macroDepthPanel->toFront(true);
}

void PX3SynthAudioProcessorEditor::closeMacroDepthPanel()
{
    if (depthPanelMacroIndex() < 0) { return; }

    macroUi = {};

    if (macroDepthPanel != nullptr) { macroDepthPanel->setVisible(false); }
    macroDepthScrim.setVisible(false);
}

void PX3SynthAudioProcessorEditor::layoutMacroDepthPanel()
{
    if (macroDepthPanel == nullptr || macroStrip == nullptr) { return; }

    const auto macro = depthPanelMacroIndex();
    if (macro < 0) { return; }

    // Beside the knob it belongs to, in the space the panels occupy. The
    // anchor is the knob's own centre, so the panel opens next to what was
    // clicked rather than in a fixed corner.
    const auto& knob = macroStrip->knob(macro);
    const auto anchor = getLocalPoint(&knob, knob.getLocalBounds().getCentre());

    auto available = panelViewportArea;
    if (available.isEmpty()) { available = getLocalBounds().reduced(20); }

    macroDepthPanel->setBounds(macroDepthPanel->preferredBoundsWithin(available, anchor));

    // The pointer aims at the knob in the PANEL's coordinates, which is why it
    // is set after the bounds: the panel may have been pushed up or down to fit
    // on screen, and the arrow has to follow it rather than the other way round.
    macroDepthPanel->setPointerTargetY(anchor.getY() - macroDepthPanel->getY());
}

bool PX3SynthAudioProcessorEditor::isAssignableTargetAt(juce::Point<int> positionInEditor) const
{
    // A knob carrying a parameter ID is something the user meant to operate.
    // Everything else under the overlay - card faces, panel backgrounds, the
    // gaps between things - is chrome, and clicking it ends the session.
    return findParameterKnobAt(positionInEditor) != nullptr;
}

juce::Slider* PX3SynthAudioProcessorEditor::findParameterKnobAt(juce::Point<int> positionInEditor) const
{
    // Deepest hit wins, so a knob inside a panel inside a viewport is found
    // rather than the container around it.
    juce::Slider* found = nullptr;

    std::function<void(const juce::Component&)> walk = [&](const juce::Component& parent)
    {
        for (auto* child : parent.getChildren())
        {
            if (child == nullptr || ! child->isVisible()) { continue; }
            if (child == macroAssignOverlay.get()) { continue; }

            const auto local = child->getLocalPoint(this, positionInEditor);
            if (! child->getLocalBounds().contains(local)) { continue; }

            if (auto* slider = dynamic_cast<juce::Slider*>(child))
            {
                if (px3::ui::isParameterKnob(*slider)) { found = slider; }
            }

            walk(*child);
        }
    };
    walk(*this);

    return found;
}

void PX3SynthAudioProcessorEditor::handleMacroAssignClick(juce::Point<int> positionInEditor)
{
    const auto assigning = assigningMacroIndex();
    if (! juce::isPositiveAndBelow(assigning, PX3SynthAudioProcessor::kMacroCount))
    {
        return;
    }

    auto* slider = findParameterKnobAt(positionInEditor);
    if (slider == nullptr)
    {
        // Background rather than a control: the session is over. This used to
        // hold the mode open, which meant the only ways out were the macro
        // knob and Escape - and nothing on screen said so.
        //
        // It is deliberately narrow: only a click that hits NO assignable knob
        // ends the session, so clicking parameter after parameter keeps
        // assigning, which is the whole point of the mode.
        finishMacroAssignEditing();
        return;
    }

    const auto parameterId = px3::ui::parameterIdOf(*slider);

    // Clicking a macro knob leaves the mode. That is the documented exit, and
    // it is why the overlay swallows the click instead of letting it through:
    // exiting must not also nudge the macro's value.
    for (int macro = 0; macro < PX3SynthAudioProcessor::kMacroCount; ++macro)
    {
        if (parameterId == PX3SynthAudioProcessor::macroParameterId(macro))
        {
            if (macro == assigning) { finishMacroAssignEditing(); }
            else                         { enterMacroAssignMode(macro); }
            return;
        }
    }

    audioProcessor.toggleMacroDestination(assigning, parameterId);
    refreshMidiMappingUI();
}
