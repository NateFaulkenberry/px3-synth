// UIConfig.json: finding it, loading it, and applying it to the controls.
//
// Split out of PluginEditor.cpp. These are member functions of the same class,
// so this needs no change to the header - PluginEditorLook.cpp and
// PluginEditorDebug.cpp work the same way.
//
// Three methods that run in sequence and are called from nowhere else:
// resolve a path, read the file, push its values into the editor. Every
// property this reads corresponds to real behaviour - the file has no
// placeholder keys - so a change here is a change on screen.

#include "PluginEditor.h"
#include "EditorSections.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "PluginProcessorInternals.h"

#include <algorithm>
#include <cmath>

using namespace px3::ui;

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

    const auto cwdCandidate = juce::File::getCurrentWorkingDirectory().getChildFile("shared/UI/Style/UIConfig.json");
    if (cwdCandidate.existsAsFile())
    {
        return cwdCandidate;
    }

    // In debug builds, prefer source-tree config even when a bundled copy exists.
    auto probe = executableDir;
    for (int i = 0; i < 10; ++i)
    {
        const auto sourceCandidate = probe.getChildFile("shared/UI/Style/UIConfig.json");
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
    // Resolving the path probes several candidate locations and the reload check
    // stats the file, so an unthrottled call from the 30 Hz timer meant hundreds
    // of filesystem operations per second per open editor. A hot-reload only has
    // to feel immediate to a human editing the file.
    if (!forceReload)
    {
        constexpr double pollIntervalSeconds = 0.5;
        const auto nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (lastUiConfigPollSeconds > 0.0 && nowSeconds - lastUiConfigPollSeconds < pollIntervalSeconds)
        {
            return;
        }
        lastUiConfigPollSeconds = nowSeconds;
    }

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
    {
        // How far above the keys the sparks are allowed to travel. The keyboard
        // component is grown upward by this much and draws the keys at the
        // bottom of itself; the headroom is transparent and passes clicks
        // through. 0 restores the old behaviour, where sparks were clipped at
        // the top edge of the keys.
        // Sized from the spark physics, not from taste: the burst runs at 60 Hz
        // with a starting speed of up to 8.4 px/frame decaying by 0.93 each
        // frame, over a lifetime of up to 0.45 s. That integrates to about
        // 102 px of travel, which is why the first value of 46 still clipped.
        keyboardSparkHeadroom = uiConfig != nullptr ? uiConfig->getInt("keyboard.sparkHeadroom", 112) : 112;
        // The wheels throw the same sparks, so they get the same room - and
        // more of it, because theirs go out in every direction. resized()
        // turns this into the four margins, clamped to the window.
        // The instrument and the wheels, both fully styled from config.
        pianoKeyboard.setStyle(PianoKeyboard::Style::fromConfig(uiConfig.get(), "keyboard"));
        performanceControls.setStyle(PerformanceControls::Style::fromConfig(uiConfig.get(), "performance"));

        performanceSparkSpill = uiConfig != nullptr
                                    ? uiConfig->getInt("keyboard.wheelSparkSpill", keyboardSparkHeadroom)
                                    : keyboardSparkHeadroom;
        resized();
    }
    {
        // The warning shown when every oscillator source is bypassed. Insets
        // parsed by the same helper the cards use, so padding, paddingTop and
        // the rest mean here what they mean everywhere else.
        PianoKeyboard::WarningStyle warning;
        if (uiConfig != nullptr)
        {
            const juce::String path { "keyboard.warning" };

            const auto readInsets = [this](const juce::String& base, px3::ui::Insets fallback)
            {
                auto result = px3::ui::Insets::parse(uiConfig->getValue(base), fallback);
                const auto side = [&](const char* suffix, float& target)
                {
                    if (const auto v = uiConfig->getValue(base + suffix); ! v.isVoid())
                    {
                        target = static_cast<float>(v);
                    }
                };
                side("Top", result.top);
                side("Right", result.right);
                side("Bottom", result.bottom);
                side("Left", result.left);
                return result;
            };

            // The wording is NOT read from here. Copy belongs with copy, and
            // this file is styling - a string sitting among colours and insets
            // is the one property a translator would need and the last place
            // they would look. It stays compiled in until there is a config
            // that is actually about text.
            warning.background = uiConfig->getColour(path + ".background", warning.background);
            warning.border = uiConfig->getColour(path + ".border.color", warning.border);
            warning.borderWidth = uiConfig->getFloat(path + ".border.width", warning.borderWidth);
            warning.cornerRadius = uiConfig->getFloat(path + ".border.radius", warning.cornerRadius);
            warning.textColour = uiConfig->getColour(path + ".textColour", warning.textColour);
            warning.fontSize = uiConfig->getFloat(path + ".fontSize", warning.fontSize);
            warning.padding = readInsets(path + ".padding", warning.padding);
            warning.margin = readInsets(path + ".margin", warning.margin);

            const auto align = uiConfig->getString(path + ".align", "center");
            warning.alignment = align.equalsIgnoreCase("left")  ? juce::Justification::left
                              : align.equalsIgnoreCase("right") ? juce::Justification::right
                                                                : juce::Justification::centred;
        }
        pianoKeyboard.setWarningStyle(warning);
    }

    if (fxPanel != nullptr)
    {
        fxPanel->setUIConfig(uiConfig);
    }

    if (macroStrip != nullptr)
    {
        macroStrip->setUIConfig(uiConfig);
    }
    if (modPanel != nullptr)
    {
        modPanel->setUIConfig(uiConfig);
    }
    if (ampPanel != nullptr)
    {
        ampPanel->setUIConfig(uiConfig);
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
    if (settingsPanel != nullptr)
    {
        settingsPanel->setUIConfig(uiConfig);
    }
    for (auto* sheet : { static_cast<px3::ui::BusInsertOverlay*>(busEqOverlay.get()),
                         static_cast<px3::ui::BusInsertOverlay*>(busCompOverlay.get()) })
    {
        if (sheet != nullptr)
        {
            sheet->setUIConfig(uiConfig);
        }
    }

    if (uiConfig != nullptr)
    {
        const auto comboStyle = uiConfig->getObject("styles.combos.default");
        uiConfig->applyComboStyle(comboStyle, lfoWaveformBox);
        uiConfig->applyComboStyle(comboStyle, lfoAssignBox);
        uiConfig->applyComboStyle(comboStyle, envAssignBox);
        uiConfig->applyComboStyle(comboStyle, subOscOctaveBox);
        uiConfig->applyComboStyle(comboStyle, subOscWaveformBox);
        uiConfig->applyComboStyle(comboStyle, vibeTypeBox);
        uiConfig->applyComboStyle(comboStyle, filterTypeBox);
        uiConfig->applyComboStyle(comboStyle, filter2TypeBox);
        uiConfig->applyComboStyle(comboStyle, osc1ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc2ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc3ModeBox);
        uiConfig->applyComboStyle(comboStyle, osc1VowelBox);
        uiConfig->applyComboStyle(comboStyle, osc2VowelBox);
        uiConfig->applyComboStyle(comboStyle, osc3VowelBox);
        uiConfig->applyComboStyle(comboStyle, delayAlgoBox);
        uiConfig->applyComboStyle(comboStyle, granularSyncBox);
        uiConfig->applyComboStyle(comboStyle, granularModeBox);
        uiConfig->applyComboStyle(comboStyle, moodRoutingBox);
        uiConfig->applyComboStyle(comboStyle, moodWetModeBox);
        uiConfig->applyComboStyle(comboStyle, moodLoopModeBox);
        uiConfig->applyComboStyle(comboStyle, reverbTypeBox);
    }

    resized();
    repaint();
}
