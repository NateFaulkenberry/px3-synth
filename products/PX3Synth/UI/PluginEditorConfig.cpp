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
    // Release builds never touch the disk for the layout: the shipped default
    // is compiled in (BinaryData::InstrumentScene_json). Designer builds edit
    // the source-tree file so a saved default is the one that gets committed.
#if PX3_UI_DESIGNER
    if (const auto envPath = juce::SystemStats::getEnvironmentVariable("PX3_INSTRUMENT_SCENE_PATH", {});
        envPath.isNotEmpty())
    {
        return juce::File(envPath);
    }

    const auto cwdCandidate = juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("shared/UI/Style/InstrumentScene.json");
    if (cwdCandidate.existsAsFile())
    {
        return cwdCandidate;
    }

    for (auto probe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
         probe.exists(); probe = probe.getParentDirectory())
    {
        const auto sourceCandidate = probe.getChildFile("shared/UI/Style/InstrumentScene.json");
        if (sourceCandidate.existsAsFile())
        {
            return sourceCandidate;
        }
        if (probe.getParentDirectory() == probe) { break; }
    }
#endif
    return {};
}

void PX3SynthAudioProcessorEditor::loadUILayout()
{
    juce::String error;
    const auto shipped = juce::String::fromUTF8(BinaryData::InstrumentScene_json,
                                                BinaryData::InstrumentScene_jsonSize);
    if (! uiLayout.loadJson(shipped, error))
    {
        // A broken shipped scene is a build defect; the unit tests load it.
        jassertfalse;
        juce::Logger::writeToLog("[PX3 Scene] shipped layout: " + error);
    }

    uiLayoutFile = resolveUILayoutFile();
    if (uiLayoutFile.existsAsFile() && ! uiLayout.loadJson(uiLayoutFile.loadFileAsString(), error))
    {
        juce::Logger::writeToLog("[PX3 Scene] " + uiLayoutFile.getFullPathName() + ": " + error);
    }
}

bool PX3SynthAudioProcessorEditor::isPrimaryCoreComposite() const noexcept
{
    return selectedTopMenuSection == kSectionOsc
        || selectedTopMenuSection == kSectionFilter
        || selectedTopMenuSection == kSectionAmp;
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
    apply("primary.filter", fltPanel.get());
    apply("primary.amp", ampPanel.get());
}

void PX3SynthAudioProcessorEditor::bindSceneComponents()
{
    using V = px3::ui::SceneBinding::Visibility;
    auto& b = sceneBinding;
    b.clear();
    if (topMenuBar != nullptr) { b.bind("header.menu", *topMenuBar); }
    b.bind("header.gain.knob", gainKnob);
    if (macroStrip != nullptr) { b.bind("macros", *macroStrip); }
    b.bind("primary.osc", oscPanelViewport);
    b.bind("mod.cards", modPanelViewport);
    if (modRoutingPanel != nullptr)
    {
        b.bind("mod.routing", *modRoutingPanel);
        b.bind("mod.routing.title", modRoutingPanel->getTitle());
        b.bind("mod.routing.patch", modRoutingPanel->getPatchView());
        b.bind("mod.routing.list", modRoutingPanel->getList());
    }
    if (modPatchBar != nullptr)
    {
        b.bind("patchbar", *modPatchBar);
        b.bind("patchbar.title", modPatchBar->getTitle());
        const char* jacks[] { "lfo1", "lfo2", "lfo3", "env1", "env2", "env3", "m1", "m2", "m3", "m4", "m5" };
        for (int source = 0; source < px3::ui::modrouting::kSourceCount; ++source)
        {
            b.bind(juce::String("patchbar.") + jacks[source], modPatchBar->getSocket(source));
        }
    }
    if (fxPanel != nullptr) { b.bind("view.fx", *fxPanel); }
    if (mixPanel != nullptr) { b.bind("view.mix", *mixPanel); }
    if (settingsPanel != nullptr) { b.bind("view.settings", *settingsPanel); }
    b.bind("keys.performance", performanceControls);
    b.bind("keys.keyboard", pianoKeyboard);

    if (oscPanel != nullptr)
    {
        const char* cards[] { "osc.sub", "osc.1", "osc.2", "osc.3" };
        for (int i = 0; i < 4; ++i)
        {
            if (auto* card = oscPanel->getCard(i)) { b.bind(cards[i], *card); }
        }

        // Every control inside the oscillator cards. Mode-dependent controls
        // follow their component's visibility, so a hidden knob's cell closes.
        const auto knob = [&b](const juce::String& cell, juce::Component& label, juce::Component& control,
                               juce::Component& value)
        {
            b.bind(cell + ".label", label, V::followsComponent);
            b.bind(cell + ".knob", control, V::followsComponent);
            b.bind(cell + ".value", value, V::followsComponent);
        };
        const auto stretch = [&b](const juce::String& cell, juce::Component& label, juce::Component& control)
        {
            b.bind(cell + ".label", label, V::followsComponent);
            b.bind(cell + ".box", control, V::followsComponent);
        };
        const auto tuning = [&knob](const juce::String& card, TuningControls& t)
        {
            knob(card + ".coarse", t.coarseLabel, t.coarseKnob, t.coarseValue);
            knob(card + ".semi", t.semitoneLabel, t.semitoneKnob, t.semitoneValue);
            knob(card + ".fine", t.fineLabel, t.fineKnob, t.fineValue);
        };

        if (auto* sub = oscPanel->getSubCard())
        {
            sub->setSceneManaged(true);
            stretch("osc.sub.wave", subOscWaveformLabel, subOscWaveformBox);
            tuning("osc.sub", subTuning);
            b.bind("osc.sub.graph", sub->getGraphSlot());
        }

        struct OscControls
        {
            juce::Component& power;
            juce::Component& modeLabel;
            juce::Component& mode;
            juce::Component& vowelLabel;
            juce::Component& vowel;
            std::array<juce::Component*, 3> macroLabels, macros, macroValues;
        };
        const std::array<OscControls, 3> oscs { {
            { osc1EnabledButton, osc1ModeLabel, osc1ModeBox, osc1VowelLabel, osc1VowelBox,
              { &osc1MacroALabel, &osc1MacroBLabel, &osc1MacroCLabel },
              { &osc1MacroAKnob, &osc1MacroBKnob, &osc1MacroCKnob },
              { &osc1MacroAValueLabel, &osc1MacroBValueLabel, &osc1MacroCValueLabel } },
            { osc2EnabledButton, osc2ModeLabel, osc2ModeBox, osc2VowelLabel, osc2VowelBox,
              { &osc2MacroALabel, &osc2MacroBLabel, &osc2MacroCLabel },
              { &osc2MacroAKnob, &osc2MacroBKnob, &osc2MacroCKnob },
              { &osc2MacroAValueLabel, &osc2MacroBValueLabel, &osc2MacroCValueLabel } },
            { osc3EnabledButton, osc3ModeLabel, osc3ModeBox, osc3VowelLabel, osc3VowelBox,
              { &osc3MacroALabel, &osc3MacroBLabel, &osc3MacroCLabel },
              { &osc3MacroAKnob, &osc3MacroBKnob, &osc3MacroCKnob },
              { &osc3MacroAValueLabel, &osc3MacroBValueLabel, &osc3MacroCValueLabel } },
        } };
        for (int i = 0; i < 3; ++i)
        {
            auto* card = oscPanel->getOscillatorCard(i);
            if (card == nullptr) { continue; }
            const auto& o = oscs[static_cast<std::size_t>(i)];
            const auto id = "osc." + juce::String(i + 1);
            card->setSceneManaged(true);
            stretch(id + ".mode", o.modeLabel, o.mode);
            stretch(id + ".vowel", o.vowelLabel, o.vowel);
            tuning(id, oscTuning[static_cast<std::size_t>(i)]);
            {
                auto& t = oscTuning[static_cast<std::size_t>(i)];
                knob(id + ".slop", t.slopLabel, t.slopKnob, t.slopValue);
            }
            stretch(id + ".table", oscWtTableLabels[static_cast<std::size_t>(i)], oscWtTableBoxes[static_cast<std::size_t>(i)]);
            knob(id + ".position", oscWtPositionLabels[static_cast<std::size_t>(i)],
                 oscWtPositionKnobs[static_cast<std::size_t>(i)], oscWtPositionValues[static_cast<std::size_t>(i)]);
            const char* macroNames[] { ".macroA", ".macroB", ".macroC" };
            for (int m = 0; m < 3; ++m)
            {
                knob(id + macroNames[m], *o.macroLabels[static_cast<std::size_t>(m)], *o.macros[static_cast<std::size_t>(m)],
                     *o.macroValues[static_cast<std::size_t>(m)]);
            }
            b.bind(id + ".graph", card->getWavetableGraph());
        }
    }
    if (fltPanel != nullptr)
    {
        b.bind("primary.filter", *fltPanel);
        b.bind("filter.routing.mode", fltPanel->getRoutingButton(), V::followsComponent);
        b.bind("filter.routing.label", fltPanel->getBalanceLabel(), V::followsComponent);
        b.bind("filter.routing.balance", fltPanel->getBalanceSlider(), V::followsComponent);
        for (int i = 0; i < 2; ++i)
        {
            if (auto* card = fltPanel->getFilterCard(i)) { b.bind("filter." + juce::String(i + 1), *card); }
        }
    }
    if (ampPanel != nullptr)
    {
        b.bind("primary.amp", *ampPanel);
        if (auto* card = ampPanel->getEnvelopeCard()) { b.bind("amp.envelope", *card); }
    }
}

void PX3SynthAudioProcessorEditor::sceneLayoutRequested()
{
    // A scene-managed component changed what it shows (a mode hid a knob).
    if (! sceneLayoutInProgress && oscPanel != nullptr && fltPanel != nullptr && ampPanel != nullptr)
    {
        applySceneLayout();
    }
}

void PX3SynthAudioProcessorEditor::applySceneLayout()
{
    if (sceneLayoutInProgress || uiLayout.rootIndex() < 0)
    {
        return;
    }
    const juce::ScopedValueSetter<bool> guard(sceneLayoutInProgress, true);

    // Runtime state the document cannot know: which section is selected.
    //
    // The section views overlap (views is an overlay) and stay laid out while
    // hidden - the owner hides the component, the scene keeps its box - so a
    // panel is already the right size when its tab is chosen.
    const auto section = selectedTopMenuSection;
    const auto voiceSection = section == kSectionOsc || section == kSectionFilter || section == kSectionAmp;
    const auto showMacros = section != kSectionSettings;
    uiLayout.setRuntimeHidden("macros", ! showMacros);
    if (macroStrip != nullptr) { macroStrip->setVisible(showMacros); }
    uiLayout.setRuntimeHidden("patchbar", ! showMacros);
    if (modPatchBar != nullptr) { modPatchBar->setVisible(showMacros); }

    // OSC, FILTER and AMP share one row. On OSC at a size that fits, all three
    // show (the composite); otherwise the selected one takes the row. Off the
    // voice sections the OSC panel keeps the whole (hidden) row.
    uiLayout.resolve(getLocalBounds().toFloat());
    panelViewportArea = px3::ui::snapToPixels(uiLayout.rectOf("views"));
    // VOICE is one page: OSC, FILTER and AMP side by side at every supported
    // size (the window's minimum is chosen so the strip fits). The AMP and
    // FILTER sections are reached through it.
    uiLayout.setRuntimeHidden("primary.osc", false);
    uiLayout.setRuntimeHidden("primary.filter", false);
    uiLayout.setRuntimeHidden("primary.amp", false);

    sceneBinding.apply(uiLayout, *this);

    // The rects the editor paints from.
    const auto rect = [this](const char* id) { return px3::ui::snapToPixels(uiLayout.rectOf(id)); };
    headerArea = rect("header");
    topMenuStripArea = headerArea;
    logoPanelArea = rect("header.logo");
    const auto logoClickInset = uiConfig != nullptr ? uiConfig->getInt("editor.layout.logoClickInsetRight", 18) : 18;
    logoClickArea = logoPanelArea.withTrimmedRight(juce::jlimit(0, logoPanelArea.getWidth() / 2, logoClickInset));
    topMenuGainArea = rect("header.gain");
    headerPlaceholderArea = rect("header.menu");
    controlsArea = rect("controls");
    macroStripArea = showMacros ? rect("macros") : juce::Rectangle<int>();
    performanceControlsArea = rect("keys.performance");
    gainLabel.setBounds({});

    if (topMenuBar != nullptr)
    {
        const auto menuOrigin = topMenuBar->getPosition();
        topMenuSectionButtonsArea = topMenuBar->getSectionButtonsArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuPresetClusterArea = topMenuBar->getPresetClusterArea().translated(menuOrigin.x, menuOrigin.y);
        topMenuMenuButtonArea = topMenuBar->getPresetMenuButtonBounds().translated(menuOrigin.x, menuOrigin.y);
        presetBarArea = topMenuPresetClusterArea;
    }

    // The OSC panel scrolls inside its viewport and is sized to it.
    oscPanelViewport.setScrollBarsShown(false, false);
    oscPanel->setSize(juce::jmax(1, oscPanelViewport.getMaximumVisibleWidth()),
                      juce::jmax(1, oscPanelViewport.getMaximumVisibleHeight()));
    // Still hand-laid inside the filter cards: see PX3_0.8.0_UI_FOUNDATION.md.
    fltPanel->layoutCardControls();

    // The spark overlay is a decoration over the keyboard row plus the room
    // the particles need; it takes no clicks.
    const auto keyboardRow = rect("keys");
    const auto headroom = juce::jmin(keyboardSparkHeadroom, controlsArea.getHeight());
    const auto spill = juce::jmax(0, performanceSparkSpill);
    sparkOverlay.setBounds(keyboardRow.expanded(spill, 0)
                               .withTop(keyboardRow.getY() - headroom)
                               .withBottom(keyboardRow.getBottom() + spill)
                               .getIntersection(getLocalBounds()));
    pianoKeyboard.toFront(false);
    performanceControls.toFront(false);
    sparkOverlay.toFront(false);
}

#if PX3_UI_DESIGNER
void PX3SynthAudioProcessorEditor::openUILayoutDesigner()
{
    if (layoutDesigner == nullptr)
    {
        px3::ui::LayoutDesigner::Host host;
        host.relayout = [this] { applySceneLayout(); applyInstrumentSceneStyles(); repaint(); };
        host.sourceFile = uiLayoutFile;
        host.defaultJson = juce::String::fromUTF8(BinaryData::InstrumentScene_json,
                                                  BinaryData::InstrumentScene_jsonSize);
        layoutDesigner = std::make_unique<px3::ui::LayoutDesigner>(*this, uiLayout, sceneBinding, std::move(host));
    }
    layoutDesigner->setActive(true);
}

void PX3SynthAudioProcessorEditor::closeUILayoutDesigner()
{
    if (layoutDesigner != nullptr)
    {
        layoutDesigner->setActive(false);
    }
}
#endif

void PX3SynthAudioProcessorEditor::applyUiConfig()
{
    std::function<void(juce::Component&)> invalidate = [&](juce::Component& component)
    {
        component.repaint();
        for (auto* child : component.getChildren()) { invalidate(*child); }
    };
    invalidate(*this);
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
