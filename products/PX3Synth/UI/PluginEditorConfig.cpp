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

namespace
{
#if PX3_UI_DESIGNER
int sectionForLayoutRegion(const juce::String& id)
{
    if (id == "primary.osc" || id == "primary.filter" || id == "primary.amp"
        || id == "view.osc") { return kSectionOsc; }
    if (id == "view.mod") { return kSectionMod; }
    if (id == "view.amp") { return kSectionAmp; }
    if (id == "view.filter") { return kSectionFilter; }
    if (id == "view.fx") { return kSectionFx; }
    if (id == "view.mix") { return kSectionMix; }
    if (id == "view.settings") { return kSectionSettings; }
    return -1;
}
#endif

juce::String layoutRegionForSection(int section)
{
    switch (section)
    {
        case kSectionOsc: return "view.osc";
        case kSectionMod: return "view.mod";
        case kSectionAmp: return "view.amp";
        case kSectionFilter: return "view.filter";
        case kSectionFx: return "view.fx";
        case kSectionMix: return "view.mix";
        case kSectionSettings: return "view.settings";
        default: return {};
    }
}

juce::Colour sceneColour(const juce::String& value, juce::Colour fallback)
{
    const auto text = value.trim();
    if (! text.startsWithChar('#')) { return fallback; }
    const auto hex = text.substring(1).getHexValue32();
    if (text.length() == 7)
    {
        return juce::Colour::fromRGB(static_cast<juce::uint8>((hex >> 16) & 0xff),
                                     static_cast<juce::uint8>((hex >> 8) & 0xff),
                                     static_cast<juce::uint8>(hex & 0xff));
    }
    if (text.length() == 9)
    {
        return juce::Colour::fromRGBA(static_cast<juce::uint8>((hex >> 24) & 0xff),
                                      static_cast<juce::uint8>((hex >> 16) & 0xff),
                                      static_cast<juce::uint8>((hex >> 8) & 0xff),
                                      static_cast<juce::uint8>(hex & 0xff));
    }
    return fallback;
}
}

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

juce::File PX3SynthAudioProcessorEditor::resolveUILayoutFile() const
{
    const auto executableDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                  .getParentDirectory();

#if JUCE_DEBUG || PX3_UI_DESIGNER
    if (const auto envPath = juce::SystemStats::getEnvironmentVariable("PX3_INSTRUMENT_SCENE_PATH", {});
        envPath.isNotEmpty())
    {
        const juce::File envFile(envPath);
        return envFile;
    }

    const auto cwdCandidate = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("shared/UI/Style/InstrumentScene.json");
    if (cwdCandidate.existsAsFile())
    {
        return cwdCandidate;
    }

    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto sourceCandidate = probe.getChildFile("shared/UI/Style/InstrumentScene.json");
        if (sourceCandidate.existsAsFile())
        {
            return sourceCandidate;
        }
        if (probe.getParentDirectory() == probe) { break; }
    }
#endif

    for (auto probe = executableDir; probe.exists(); probe = probe.getParentDirectory())
    {
        const auto bundled = probe.getChildFile("Resources/InstrumentScene.json");
        if (bundled.existsAsFile())
        {
            return bundled;
        }
        if (probe.getParentDirectory() == probe) { break; }
    }

    return {};
}

void PX3SynthAudioProcessorEditor::loadUILayout()
{
    uiLayoutFile = resolveUILayoutFile();
    if (! uiLayoutFile.existsAsFile())
    {
        return;
    }

    juce::String error;
    if (! uiLayout.loadJson(uiLayoutFile.loadFileAsString(), error))
    {
        juce::Logger::writeToLog("[PX3 Scene] " + error);
    }
}

juce::Rectangle<int> PX3SynthAudioProcessorEditor::layoutBoundsForRegion(
    const juce::String& id,
    juce::Rectangle<int> fallback) const
{
    if (uiLayout.findNode(id) == nullptr)
    {
        return fallback;
    }

    const auto resolved = uiLayout.resolveBounds(id, panelViewportArea.toFloat());
    return resolved.isEmpty() ? fallback : resolved.toNearestInt();
}

bool PX3SynthAudioProcessorEditor::isPrimaryCoreComposite() const noexcept
{
    return selectedTopMenuSection == kSectionOsc
        && panelViewportArea.getWidth() >= 900
        && panelViewportArea.getHeight() >= 380;
}

void PX3SynthAudioProcessorEditor::applyUILayoutSectionOrder()
{
    if (topMenuBar == nullptr)
    {
        return;
    }

    std::array<int, 6> order { 0, 1, 2, 3, 4, 5 };
    std::stable_sort(order.begin(), order.end(), [this](int a, int b)
    {
        const auto* first = uiLayout.findNode(layoutRegionForSection(a));
        const auto* second = uiLayout.findNode(layoutRegionForSection(b));
        const auto firstOrder = first != nullptr ? first->order : a;
        const auto secondOrder = second != nullptr ? second->order : b;
        return firstOrder != secondOrder ? firstOrder < secondOrder : a < b;
    });
    topMenuBar->setSectionOrder(order);
}

void PX3SynthAudioProcessorEditor::applyInstrumentSceneStyles()
{
    const auto apply = [this](const juce::String& nodeId, auto* panel)
    {
        if (panel == nullptr) { return; }
        const auto* node = uiLayout.findNode(nodeId);
        const auto* style = node != nullptr ? uiLayout.findStyleToken(node->styleToken) : nullptr;
        if (style == nullptr) { return; }

        panel->setSceneStyle(sceneColour(style->background, juce::Colour::fromRGB(24, 27, 29)),
                             sceneColour(style->foreground, juce::Colour::fromRGB(230, 233, 231)),
                             sceneColour(style->accent, juce::Colour::fromRGB(104, 169, 200)),
                             style->borderRadius,
                             style->borderWidth);
    };

    apply("primary.osc", oscPanel.get());
    apply(isPrimaryCoreComposite() ? "primary.filter" : "view.filter", fltPanel.get());
    apply(isPrimaryCoreComposite() ? "primary.amp" : "view.amp", ampPanel.get());
}

#if PX3_UI_DESIGNER
void PX3SynthAudioProcessorEditor::openUILayoutDesigner()
{
    if (uiDesignerWindow == nullptr)
    {
        uiSelectionOverlay = std::make_unique<UILayoutSelectionOverlay>([this](juce::Rectangle<float> bounds)
        {
            if (const auto message = updateLayoutRegionBounds(selectedLayoutRegionId, bounds);
                message.isNotEmpty() && uiDesignerWindow != nullptr)
            {
                uiDesignerWindow->setStatus(message);
            }
        });
        uiSelectionOverlay->onEditBegin = [this]() { uiLayout.beginTransaction(); };
        uiSelectionOverlay->onEditEnd = [this]()
        {
            uiLayout.commitTransaction();
            if (uiDesignerWindow != nullptr)
            {
                uiDesignerWindow->setDocument(uiLayout);
            }
        };
        addAndMakeVisible(*uiSelectionOverlay);

        UILayoutDesignerWindow::Callbacks callbacks;
        for (const auto& entry : audioProcessor.getParameterCatalog().entries())
        {
            callbacks.bindingOptions.push_back({ entry.id, entry.name });
        }
        callbacks.selectionChanged = [this](const juce::String& id) { selectLayoutRegion(id); };
        callbacks.presentationChanged = [this](const juce::String& id,
                                               px3::ui::InstrumentSceneNodeKind kind,
                                               const juce::String& label,
                                               const juce::String& bindingId)
        {
            if (kind == px3::ui::InstrumentSceneNodeKind::parameterControl
                && audioProcessor.getParameterCatalog().find(bindingId) == nullptr)
            {
                return juce::String("Select a parameter from the catalog");
            }
            juce::String error;
            if (! uiLayout.setNodePresentation(id, kind, label, bindingId, error)) { return error; }
            resized();
            return juce::String("Updated presentation for ") + id;
        };
        callbacks.boundsChanged = [this](const juce::String& id, juce::Rectangle<float> bounds)
        {
            return updateLayoutRegionBounds(id, bounds);
        };
        callbacks.orderChanged = [this](const juce::String& id, int order)
        {
            return updateLayoutRegionOrder(id, order);
        };
        callbacks.styleChanged = [this](const juce::String& id, const juce::String& token)
        {
            return updateLayoutRegionStyle(id, token);
        };
        callbacks.styleTokenChanged = [this](const px3::ui::InstrumentSceneStyleToken& token, bool create)
        {
            juce::String error;
            const auto updated = create ? uiLayout.addStyleToken(token, error)
                                        : uiLayout.updateStyleToken(token, error);
            if (updated) { resized(); repaint(); }
            return error;
        };
        callbacks.layoutChanged = [this](const juce::String& id,
                                         px3::ui::InstrumentSceneLayoutMode mode)
        {
            return updateSceneLayoutMode(id, mode);
        };
        callbacks.parentChanged = [this](const juce::String& id, const juce::String& parentId)
        {
            return updateSceneParent(id, parentId);
        };
        callbacks.flowChanged = [this](const juce::String& id,
                                       float flexGrow,
                                       float spacing,
                                       int gridColumns)
        {
            return updateSceneFlow(id, flexGrow, spacing, gridColumns);
        };
        callbacks.sizeConstraintsChanged = [this](const juce::String& id,
                                                  juce::Point<float> minimumSize,
                                                  juce::Point<float> maximumSize)
        {
            juce::String error;
            if (! uiLayout.setSizeConstraints(id, minimumSize, maximumSize, error)) { return error; }
            resized();
            return "Updated size constraints for " + id;
        };
        callbacks.beginEdit = [this]() { uiLayout.beginTransaction(); };
        callbacks.visibilityChanged = [this](const juce::String& id, bool visible)
        {
            juce::String error;
            if (! uiLayout.setVisible(id, visible, error)) { return error; }
            resized();
            return juce::String(visible ? "Showing " : "Hiding ") + id;
        };
        callbacks.endEdit = [this]()
        {
            uiLayout.commitTransaction();
            if (uiDesignerWindow != nullptr)
            {
                uiDesignerWindow->setDocument(uiLayout);
            }
        };
        callbacks.save = [this]()
        {
            if (uiLayoutFile == juce::File())
            {
                uiDesignerWindow->setStatus("No writable layout file found");
                return;
            }
            const auto parent = uiLayoutFile.getParentDirectory();
            if (! parent.exists() && ! parent.createDirectory())
            {
                uiDesignerWindow->setStatus("Could not create layout directory");
                return;
            }
            if (! uiLayoutFile.replaceWithText(uiLayout.toJson()))
            {
                uiDesignerWindow->setStatus("Could not save layout file");
                return;
            }
            uiDesignerWindow->setStatus("Saved " + uiLayoutFile.getFileName());
        };
        callbacks.undo = [this]
        {
            const auto changed = uiLayout.undo();
            if (changed)
            {
                resized();
                if (uiDesignerWindow != nullptr) { uiDesignerWindow->setDocument(uiLayout); }
            }
            return changed;
        };
        callbacks.redo = [this]
        {
            const auto changed = uiLayout.redo();
            if (changed)
            {
                resized();
                if (uiDesignerWindow != nullptr) { uiDesignerWindow->setDocument(uiLayout); }
            }
            return changed;
        };
        callbacks.close = [this]() { closeUILayoutDesigner(); };

        uiDesignerWindow = std::make_unique<UILayoutDesignerWindow>(uiLayout, std::move(callbacks));
    }

    if (selectedLayoutRegionId.isEmpty())
    {
        selectedLayoutRegionId = selectedTopMenuSection == kSectionOsc
                                     ? "primary.osc"
                                     : layoutRegionForSection(selectedTopMenuSection);
    }
    uiDesignerWindow->setDocument(uiLayout);
    uiDesignerWindow->setSelectedRegion(selectedLayoutRegionId);
    uiDesignerWindow->setVisible(true);
    uiDesignerWindow->toFront(true);
    uiSelectionOverlay->setVisible(true);
    resized();
}

void PX3SynthAudioProcessorEditor::closeUILayoutDesigner()
{
    if (uiSelectionOverlay != nullptr)
    {
        uiSelectionOverlay->setVisible(false);
    }
    if (uiDesignerWindow != nullptr)
    {
        uiDesignerWindow->setVisible(false);
    }
}

void PX3SynthAudioProcessorEditor::selectLayoutRegion(const juce::String& id)
{
    selectedLayoutRegionId = id;
    const auto section = sectionForLayoutRegion(id);
    if (section >= 0 && section != selectedTopMenuSection)
    {
        selectedTopMenuSection = section;
        if (topMenuBar != nullptr)
        {
            topMenuBar->setSelectedSection(section);
        }
        updatePanelVisibility();
        resized();
    }
    if (uiDesignerWindow != nullptr)
    {
        uiDesignerWindow->setSelectedRegion(id);
    }
    applyInstrumentSceneStyles();
    refreshUILayoutSelection();
}

juce::String PX3SynthAudioProcessorEditor::updateLayoutRegionBounds(
    const juce::String& id,
    juce::Rectangle<float> bounds)
{
    juce::String error;
    if (uiLayout.findNode(id) == nullptr)
    {
        return "Unknown scene node: " + id;
    }
    else if (! uiLayout.setBounds(id, bounds, error))
    {
        return error;
    }

    resized();
    return "Previewing " + id;
}

juce::String PX3SynthAudioProcessorEditor::updateLayoutRegionOrder(const juce::String& id, int order)
{
    juce::String error;
    if (uiLayout.findNode(id) == nullptr)
    {
        return "Unknown scene node: " + id;
    }
    else if (! uiLayout.setOrder(id, order, error))
    {
        return error;
    }

    applyUILayoutSectionOrder();
    return "Updated order for " + id;
}

juce::String PX3SynthAudioProcessorEditor::updateLayoutRegionStyle(
    const juce::String& id,
    const juce::String& styleToken)
{
    juce::String error;
    if (! uiLayout.setNodeStyleToken(id, styleToken, error))
    {
        return error;
    }
    applyInstrumentSceneStyles();
    repaint();
    return "Applied " + styleToken + " to " + id;
}

juce::String PX3SynthAudioProcessorEditor::updateSceneLayoutMode(
    const juce::String& id,
    px3::ui::InstrumentSceneLayoutMode mode)
{
    juce::String error;
    if (! uiLayout.setLayoutMode(id, mode, error)) { return error; }
    resized();
    juce::ignoreUnused(mode);
    return "Updated layout mode for " + id;
}

juce::String PX3SynthAudioProcessorEditor::updateSceneParent(
    const juce::String& id,
    const juce::String& parentId)
{
    juce::String error;
    if (! uiLayout.setParentNode(id, parentId, error)) { return error; }
    resized();
    return "Reparented " + id;
}

juce::String PX3SynthAudioProcessorEditor::updateSceneFlow(const juce::String& id,
                                                           float flexGrow,
                                                           float spacing,
                                                           int gridColumns)
{
    juce::String error;
    if (! uiLayout.setFlowProperties(id, flexGrow, spacing, gridColumns, error)) { return error; }
    resized();
    return "Updated layout constraints for " + id;
}

void PX3SynthAudioProcessorEditor::refreshUILayoutSelection()
{
    if (uiSelectionOverlay == nullptr)
    {
        return;
    }

    const auto* region = uiLayout.findNode(selectedLayoutRegionId);
    const auto designerVisible = uiDesignerWindow != nullptr && uiDesignerWindow->isVisible();
    if (region == nullptr)
    {
        uiSelectionOverlay->setSelection(panelViewportArea, {}, false);
        return;
    }
    const auto parentBounds = region->parentId.isEmpty()
                                ? panelViewportArea.toFloat()
                                : uiLayout.resolveBounds(region->parentId, panelViewportArea.toFloat());
    if (parentBounds.isEmpty())
    {
        uiSelectionOverlay->setSelection(panelViewportArea, {}, false);
        return;
    }
    uiSelectionOverlay->setSelection(parentBounds.toNearestInt(), region->bounds, designerVisible);
}
#endif

void PX3SynthAudioProcessorEditor::applyUiConfig()
{
    if (topMenuBar != nullptr)
    {
        topMenuBar->setUIConfig(uiConfig);
    }
    applyUILayoutSectionOrder();
    applyInstrumentSceneStyles();
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
    }

    resized();
    repaint();
}
