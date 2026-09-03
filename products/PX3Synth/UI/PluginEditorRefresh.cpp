// Reading the processor and updating the controls to match, plus the timer
// that drives it.
//
// Split out of PluginEditor.cpp. These are member functions of the same class,
// so this needs no change to the header - PluginEditorLook.cpp and
// PluginEditorDebug.cpp work the same way.
//
// Everything here runs in one direction: processor state in, control state
// out. Nothing in this file decides anything or writes a parameter; when one
// of these appears to, it is handing a value to a control whose attachment
// does the writing. That is why the timerCallback belongs with them - it is
// the thing that calls them, thirty times a second, and has no logic of its
// own beyond deciding which of them are worth calling this tick.
//
// The wavetable menu and display refreshes are here for the same reason,
// despite being about wavetables rather than about refreshing in general:
// they are read-and-update in exactly this sense, and separating them would
// have split importWavetableFile from the menu it rebuilds.

#include "PluginEditor.h"
#include "UpdateService.h"
#include "EditorSections.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "PluginProcessorInternals.h"
#include "WavetableLibrary.h"
#include "WavetableImporter.h"
#include "WavetableFactory.h"

#include <algorithm>
#include <cmath>

#if JUCE_MAC
#include <mach/mach.h>
#endif

using namespace px3::ui;

namespace
{
// Factory tables take menu ids 1..N; user tables start here, so the two can
// never be confused by an id that happens to collide after the library grows.
constexpr int kUserWavetableMenuBase = 1000;
} // namespace

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

        // Through the shared knob path, not styled by hand. It is what applies
        // knobLookAndFeel - the brushed face, the ring, the pointer - so a knob
        // configured any other way is a plain JUCE rotary sitting among this
        // synth's own, which is exactly how it looked.
        configureEffectKnob(knob, oscWtPositionLabels[index], "POSITION",
                            audioProcessor.getOscillatorWtPositionParam(osc));
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

void PX3SynthAudioProcessorEditor::refreshModulationRings()
{
    // Every knob bound to a parameter, not just the wavetable scan. A knob whose
    // parameter has a modulation source pointed at it draws an arc from where
    // the parameter sits out to where the value actually is - so an LFO on the
    // filter cutoff or an envelope on pitch is visible on the control it is
    // moving, rather than only in its effect.
    //
    // The knob itself is never moved: it shows what the user set and what a DAW
    // would automate, and driving it would fight the parameter attachment and
    // write the modulation back into the parameter.
    for (auto& binding : knobBindings)
    {
        if (binding.slider == nullptr || binding.parameter == nullptr)
        {
            continue;
        }

        const auto modulated = audioProcessor.getModulatedNormalisedValue(*binding.parameter);
        const auto shown = static_cast<double>(
            binding.slider->getProperties().getWithDefault("modulatedPos", -1.0));

        // Only when it has actually moved, and only by enough to see. A repaint
        // per knob per frame for a value that has not changed is how a UI ends
        // up costing more than the synth.
        if (std::abs(shown - static_cast<double>(modulated)) > 0.002)
        {
            binding.slider->getProperties().set("modulatedPos", static_cast<double>(modulated));
            binding.slider->repaint();
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
        modPanel->refreshLfoFromParameters();
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
    // The update notice shows itself out. Counted in frames on the tick that
    // is already running rather than on a timer of its own.
    if (updateNoticeFramesLeft > 0)
    {
        if (--updateNoticeFramesLeft == 0) { dismissUpdateNotice(); }
    }

    loadUiConfig(false);
    refreshWavetableDisplays();
    refreshModulationRings();
    refreshMidiMappingUI();

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

    if (px3::GlobalSettings::getInstance().areAnimationsEnabled()
        && anyOscillatorEngaged
        && (logoVibrationIntensity > 0.001f || anyKeyDown))
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
