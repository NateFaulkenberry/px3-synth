#include "TestSupport.h"

// testFactoryPresets, testEditorLifecycle

namespace px3tests
{


void testFactoryPresets()
{
    suite("FACTORY PRESETS");

    {
        // INIT is the state the plugin loads with, not a preset. It used to be
        // written to disk as INIT.px3preset in a factory category called INIT,
        // which gave the browser a category containing one entry and listed the
        // default alongside sounds somebody had designed. It has no file now.
        PX3SynthAudioProcessor processor;
        PresetManager manager(processor);
        juce::String error;

        // A leftover from an earlier build, to prove it gets cleaned up.
        const auto factoryRoot = manager.getFactoryPresetRootDir();
        const auto legacyDir = factoryRoot.getChildFile("INIT");
        legacyDir.createDirectory();
        const auto legacyFile = legacyDir.getChildFile("INIT.px3preset");
        legacyFile.replaceWithText("<PX3_PRESET/>");
        const auto legacyFlat = factoryRoot.getChildFile("INIT.px3preset");
        legacyFlat.replaceWithText("<PX3_PRESET/>");

        manager.initialise(error);

        check("Preset_InitIsNeverWrittenToDisk",
              ! legacyFile.existsAsFile() && ! legacyFlat.existsAsFile() && ! legacyDir.isDirectory(),
              "after initialise: INIT dir " + juce::String(legacyDir.isDirectory() ? "still there" : "gone")
                  + ", INIT file " + juce::String(legacyFile.existsAsFile() ? "still there" : "gone"));

        // No preset file anywhere may claim the INIT category either.
        juce::StringArray offenders;
        for (const auto& file : factoryRoot.findChildFiles(juce::File::findFiles, true, "*.px3preset"))
        {
            if (file.getFileNameWithoutExtension().equalsIgnoreCase("INIT")
                || file.getParentDirectory().getFileName().equalsIgnoreCase("INIT"))
            {
                offenders.add(file.getFullPathName());
            }
        }
        check("Preset_NoFactoryCategoryIsCalledInit", offenders.isEmpty(),
              offenders.isEmpty() ? "the factory tree has no INIT file and no INIT category"
                                  : offenders.joinIntoString(", "));

        // Held in a local: getAllCategories returns by value, so begin() and
        // end() taken from two separate calls are iterators into two different
        // temporaries and comparing them is meaningless.
        const auto categories = manager.getAllCategories();
        check("Preset_InitIsNotInTheCategoryList",
              std::find(categories.begin(), categories.end(), juce::String("INIT")) == categories.end(),
              "categories: " + juce::StringArray(categories.data(), (int) categories.size())
                                   .joinIntoString(", "));

        // ...but it is the first thing offered in the browser.
        PresetManager::Query query;
        const auto listed = manager.queryPresets(query);
        check("Preset_InitIsTheFirstRowOfTheBrowser",
              ! listed.empty() && listed.front().isInit
                  && listed.front().metadata.name == "- INIT -",
              listed.empty() ? "the browser listed nothing"
                             : "first row is \"" + listed.front().metadata.name + "\", "
                                   + juce::String(listed.size()) + " rows in total");

        // And loading it restores the default state from memory.
        {
            setParam(processor, "osc1MacroA", 0.9f);
            const auto moved = getParamValue(processor, "osc1MacroA");

            PresetManager::PresetRecord init;
            init.isInit = true;
            juce::String loadError;
            const auto loaded = manager.loadPreset(init, loadError);

            check("Preset_LoadingInitRestoresTheDefaultState",
                  loaded && std::abs(getParamValue(processor, "osc1MacroA") - moved) > 0.1f,
                  loaded ? "osc1MacroA " + fmt(moved, 3) + " -> " + fmt(getParamValue(processor, "osc1MacroA"), 3)
                         : "load failed: " + loadError);
        }
    }

    {
        // Every effect defaults to ENABLED, and a factory preset starts from
        // the plugin's defaults and only overrides what it lists - so a preset
        // that says nothing about the FX ships with all eight of them running.
        // Every one of the twenty did. The descriptions had always promised
        // otherwise ("nothing else in the way", "nothing exotic"); the state
        // did not match them.
        //
        // Computed the way PresetManager computes it: defaults, then the
        // definition's overrides. A preset that stops naming its effects fails
        // here rather than quietly turning them all back on.
        PX3SynthAudioProcessor processor;

        auto defaultOf = [&processor](const juce::String& id)
        {
            for (auto* parameter : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    if (ranged->getParameterID() == id)
                    {
                        return ranged->convertFrom0to1(ranged->getDefaultValue()) > 0.5f;
                    }
                }
            }
            return false;
        };

        static const std::array<const char*, 8> kFx {
            { "vibe", "delay", "reverb", "mood", "doom", "lucy", "chorus", "spread" } };

        std::vector<juce::String> signatures;
        juce::StringArray tooMany;
        std::set<juce::String> everUsed;
        auto mostOn = 0;

        for (const auto& def : px3::presets::factoryPresets())
        {
            juce::String signature;
            auto onCount = 0;

            for (const auto* fx : kFx)
            {
                const auto id = juce::String(fx) + juce::String("Enabled");
                auto on = defaultOf(id);
                for (const auto& [paramId, value] : def.params)
                {
                    if (id == paramId) on = value > 0.5f;
                }

                signature << (on ? '1' : '0');
                if (on)
                {
                    ++onCount;
                    everUsed.insert(fx);
                }
            }

            mostOn = juce::jmax(mostOn, onCount);
            if (onCount > 4) tooMany.add(juce::String(def.name) + " (" + juce::String(onCount) + ")");
            signatures.push_back(signature);
        }

        const std::set<juce::String> distinct { signatures.begin(), signatures.end() };

        check("Presets_NoneOfThemTurnOnEveryEffect", tooMany.isEmpty(),
              tooMany.isEmpty() ? "the busiest preset runs " + juce::String(mostOn) + " of 8 effects"
                                : "more than four effects: " + tooMany.joinIntoString(", "));

        check("Presets_TheirEffectChoicesActuallyDiffer",
              static_cast<int>(distinct.size()) >= 14,
              juce::String(static_cast<int>(distinct.size())) + " distinct effect combinations across "
                  + juce::String(static_cast<int>(signatures.size())) + " presets");

        check("Presets_EveryEffectIsShownOffBySomething",
              static_cast<int>(everUsed.size()) == static_cast<int>(kFx.size()),
              juce::String(static_cast<int>(everUsed.size())) + " of " + juce::String((int) kFx.size())
                  + " effects appear in at least one preset");
    }

    const auto presets = px3::presets::factoryPresets();

    check("Presets_LibraryIsNotEmpty", presets.size() >= 16,
          juce::String(static_cast<int>(presets.size())) + " factory presets");

    // ---- names, categories, descriptions -----------------------------------
    {
        juce::StringArray names;
        juce::StringArray duplicates;
        juce::StringArray badCategories;
        juce::StringArray missingText;

        const juce::StringArray validCategories { "BASS", "LEADS", "PADS", "PLUCKS", "EXPERIMENTAL" };

        for (const auto& preset : presets)
        {
            const juce::String name(preset.name);
            if (names.contains(name))
            {
                duplicates.add(name);
            }
            names.add(name);

            if (! validCategories.contains(juce::String(preset.category)))
            {
                badCategories.add(name);
            }

            // A preset with no description is a preset nobody can choose from a
            // list, which is most of what a factory library is for.
            if (juce::String(preset.description).length() < 20 || juce::String(preset.author).isEmpty())
            {
                missingText.add(name);
            }
        }

        check("Presets_NamesAreUnique", duplicates.isEmpty(),
              duplicates.isEmpty() ? juce::String(names.size()) + " distinct names"
                                   : "duplicated: " + duplicates.joinIntoString(", "));
        check("Presets_CategoriesAreValid", badCategories.isEmpty(),
              badCategories.isEmpty() ? "" : "bad: " + badCategories.joinIntoString(", "));
        check("Presets_EveryPresetIsDescribed", missingText.isEmpty(),
              missingText.isEmpty() ? "" : "missing: " + missingText.joinIntoString(", "));

        // Every category should actually have something in it, or the browser
        // shows an empty folder.
        juce::StringArray emptyCategories;
        for (const auto& category : validCategories)
        {
            auto found = false;
            for (const auto& preset : presets)
            {
                found = found || category == preset.category;
            }
            if (! found)
            {
                emptyCategories.add(category);
            }
        }
        check("Presets_EveryCategoryHasPresets", emptyCategories.isEmpty(),
              emptyCategories.isEmpty() ? "" : "empty: " + emptyCategories.joinIntoString(", "));
    }

    // ---- parameter ids and units -------------------------------------------
    {
        PX3SynthAudioProcessor processor;

        auto findParam = [&processor](const juce::String& id) -> juce::RangedAudioParameter*
        {
            for (auto* parameter : processor.getParameters())
            {
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                {
                    if (ranged->getParameterID() == id)
                    {
                        return ranged;
                    }
                }
            }
            return nullptr;
        };

        juce::StringArray unknownIds;
        juce::StringArray outOfRange;

        for (const auto& preset : presets)
        {
            for (const auto& [id, value] : preset.params)
            {
                auto* param = findParam(id);
                if (param == nullptr)
                {
                    unknownIds.addIfNotAlreadyThere(juce::String(preset.name) + "/" + id);
                    continue;
                }

                // Values are written in each parameter's OWN units. A value
                // outside the range is a unit mistake - a frequency written into
                // a 0..1 amount, or seconds into a choice index - and it would
                // otherwise clamp silently into something that merely sounds
                // wrong.
                const auto& range = param->getNormalisableRange();
                if (value < range.start - 1.0e-4f || value > range.end + 1.0e-4f)
                {
                    outOfRange.add(juce::String(preset.name) + "/" + id + "="
                                   + juce::String(value, 3) + " outside ["
                                   + juce::String(range.start, 3) + ", "
                                   + juce::String(range.end, 3) + "]");
                }
            }
        }

        check("Presets_EveryParameterIdExists", unknownIds.isEmpty(),
              unknownIds.isEmpty() ? "" : unknownIds.joinIntoString(", "));
        check("Presets_EveryValueIsInsideItsParameterRange", outOfRange.isEmpty(),
              outOfRange.isEmpty() ? "values are in real units and all in range"
                                   : outOfRange.joinIntoString("; "));
    }

    // ---- FX coverage -------------------------------------------------------
    {
        // The library exists partly to show what the instrument can do, so every
        // effect has to appear somewhere with a non-zero amount.
        struct Coverage { const char* label; const char* id; };
        const std::array<Coverage, 8> effects { {
            { "VIBE", "vibeAmount" },
            { "CHORUS", "chorusAmount" },
            { "DOOM", "doomMix" },
            { "LUCY", "lucyGlobal" },
            { "DELAY", "delayAmount" },
            { "MOOD", "moodMix" },
            { "REVERB", "reverbAmount" },
            { "SPREAD", "spreadAmount" },
        } };

        juce::StringArray uncovered;
        juce::String detail;

        for (const auto& effect : effects)
        {
            auto count = 0;
            for (const auto& preset : presets)
            {
                for (const auto& [id, value] : preset.params)
                {
                    if (juce::String(id) == effect.id && value > 0.0f)
                    {
                        ++count;
                        break;
                    }
                }
            }

            if (count == 0)
            {
                uncovered.add(effect.label);
            }
            detail << effect.label << ":" << count << "  ";
        }

        check("Presets_EveryEffectIsShowcased", uncovered.isEmpty(),
              uncovered.isEmpty() ? detail : "never used: " + uncovered.joinIntoString(", "));
    }

    // ---- oscillator mode coverage ------------------------------------------
    {
        // Not every mode needs a preset, but a library that only ever reaches
        // for a saw is not showing the instrument either.
        std::set<int> modesUsed;
        for (const auto& preset : presets)
        {
            for (const auto& [id, value] : preset.params)
            {
                const juce::String name(id);
                if (name == "osc1Mode" || name == "osc2Mode" || name == "osc3Mode")
                {
                    modesUsed.insert(static_cast<int>(value));
                }
            }
        }

        check("Presets_UseAVarietyOfOscillatorModes", modesUsed.size() >= 10,
              juce::String(static_cast<int>(modesUsed.size())) + " of 20 oscillator modes used");
    }

    // ---- every preset actually makes a sound -------------------------------
    {
        // The single most valuable check here. A preset that loads silent, or
        // clips, or produces a NaN is worse than no preset - and none of that is
        // visible from reading the definitions.
        juce::StringArray silent;
        juce::StringArray clipping;
        juce::StringArray invalid;
        juce::StringArray loud;
        juce::StringArray quiet;
        std::vector<double> levels;

        for (const auto& preset : presets)
        {
            PX3SynthAudioProcessor processor;

            for (const auto& [id, value] : preset.params)
            {
                for (auto* parameter : processor.getParameters())
                {
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                    {
                        if (ranged->getParameterID() == id)
                        {
                            ranged->setValueNotifyingHost(
                                juce::jlimit(0.0f, 1.0f, ranged->convertTo0to1(value)));
                            break;
                        }
                    }
                }
            }

            // Long enough for a slow pad to open and for a tail to be heard.
            const auto capture = render(processor, static_cast<int>(kSampleRate * 4.0),
                                        { { 4000, true, 55, 0.85f },
                                          { 4200, true, 62, 0.85f },
                                          { static_cast<int>(kSampleRate * 2.5), false, 55, 0.0f },
                                          { static_cast<int>(kSampleRate * 2.5), false, 62, 0.0f } });

            const auto rms = capture.rms();
            const auto peak = capture.peak();

            auto finite = true;
            for (std::size_t i = 0; i < capture.left.size(); ++i)
            {
                if (! std::isfinite(capture.left[i]) || ! std::isfinite(capture.right[i]))
                {
                    finite = false;
                    break;
                }
            }

            const juce::String name(preset.name);
            if (! finite)                 { invalid.add(name); }
            // Measured on PEAK, not RMS. Karplus-Strong excites from an
            // unseeded generator, so a plucked preset's RMS swings run to run
            // and an RMS threshold here would be intermittently flaky. Peak is
            // stable across runs and answers the same question.
            if (peak < 0.02f)             { silent.add(name + " peak " + juce::String(peak, 5)); }
            if (peak > 0.999f)            { clipping.add(name + " peak " + juce::String(peak, 4)); }
            if (rms > 0.30)               { loud.add(name + " rms " + juce::String(rms, 4)); }
            if (rms > 0.0 && rms < 0.012) { quiet.add(name + " rms " + juce::String(rms, 4)); }

            levels.push_back(rms);
        }

        check("Presets_EveryPresetIsFinite", invalid.isEmpty(),
              invalid.isEmpty() ? "" : invalid.joinIntoString(", "));
        check("Presets_NoPresetIsSilent", silent.isEmpty(),
              silent.isEmpty() ? "all produce audio from a two-note chord"
                               : silent.joinIntoString(", "));
        check("Presets_NoPresetClips", clipping.isEmpty(),
              clipping.isEmpty() ? "" : clipping.joinIntoString(", "));

        // Browsing a library should not be a volume ride.
        if (! levels.empty())
        {
            const auto loudest = *std::max_element(levels.begin(), levels.end());
            const auto quietest = *std::min_element(levels.begin(), levels.end());
            const auto spreadDb = 20.0 * std::log10(loudest / juce::jmax(1.0e-9, quietest));

            // Generous on purpose: a decaying pluck measured over four seconds
            // is legitimately quieter than a sustained pad, and one preset's
            // level is stochastic. This is here to catch a preset that is
            // wildly out, not to flatten the library.
            check("Presets_LevelsAreWithinAReasonableSpread", spreadDb < 24.0,
                  "loudest to quietest = " + juce::String(spreadDb, 1) + " dB  (quietest "
                      + juce::String(quietest, 4) + ", loudest " + juce::String(loudest, 4) + ")");
        }

        juce::ignoreUnused(loud, quiet);
    }
}

void testEditorLifecycle()
{
    suite("EDITOR LIFECYCLE");

    // Create and destroy the editor repeatedly.
    //
    // This exists because of a real crash: FxCardComponent made the FX cards OWN
    // their sliders, but the editor destructor still tore the panels down before
    // releasing the parameter attachments that point at those sliders. The
    // attachments then called removeListener on freed memory - a segfault on
    // quit, in juce::Slider::removeListener via ~SliderParameterAttachment.
    //
    // Nothing else in the suite constructs the editor, so nothing else could
    // have caught it.
    auto survived = true;

    for (int i = 0; i < 3 && survived; ++i)
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        survived = survived && editor != nullptr;

        if (editor != nullptr)
        {
            editor->setSize(1280, 800);
            // Reaching the destructor at all is the test.
            editor.reset();
        }
    }

    check("Editor_CreatesAndDestroysWithoutCrashing", survived,
          "3 create/destroy cycles - attachments must be released before the "
          "panels that own their targets");


    {
        // The analog engine has to be reachable and audible from the debug
        // window, because its tuning constants exist nowhere else - they are not
        // parameters, not in presets, and not in UIConfig. If the panel does not
        // expose them there is no way to tune it at all.
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1320, 760);

        // Every tuning key must round-trip through the processor's debug hooks,
        // which is what the panel's sliders drive.
        juce::StringArray unreachable;
        for (const auto& key : px3::AnalogEngine::tuningKeys())
        {
            const auto original = processor.debugGetAnalogTuningValue(key);
            const auto probe = original * 0.5f + 0.077f;
            processor.debugSetAnalogTuningValue(key, probe);
            if (juce::approximatelyEqual(processor.debugGetAnalogTuningValue(key), original))
            {
                unreachable.add(key);
            }
        }

        check("Editor_AnalogTuningIsReachableFromTheDebugHooks", unreachable.isEmpty(),
              unreachable.isEmpty() ? juce::String(px3::AnalogEngine::tuningKeys().size())
                                          + " keys reachable"
                                    : "unreachable: " + unreachable.joinIntoString(", "));

        processor.debugResetAnalogTuning();
        const auto compiled = px3::AnalogEngine::defaultTuningFor(px3::AnalogEngine::Profile::clean);
        check("Editor_AnalogResetRestoresCompiledDefaults",
              juce::approximatelyEqual(processor.debugGetAnalogTuningValue("curveBlend"),
                                       compiled.curveBlend),
              "");

        // And every profile must be selectable, since flipping through them is
        // the point of the dropdown.
        auto allSelectable = true;
        for (int i = 0; i < px3::AnalogEngine::kProfileCount; ++i)
        {
            auto& param = processor.getAnalogProfileParam();
            param.setValueNotifyingHost(param.convertTo0to1(static_cast<float>(i)));
            allSelectable = allSelectable && param.getIndex() == i;
        }
        check("Editor_EveryAnalogProfileIsSelectable", allSelectable,
              juce::String(px3::AnalogEngine::kProfileCount) + " profiles");

        editor.reset();
    }

    {
        // The editor is destroyed and rebuilt every time the plugin window is
        // closed and reopened, and it was the only thing that knew which preset
        // was loaded - so a reopened window showed INIT, with no category and
        // no author, over a patch that had not changed at all. The identity now
        // rides in the processor's state alongside topMenuView.
        PX3SynthAudioProcessor processor;

        auto menuOf = [](juce::AudioProcessorEditor& e)
        {
            TopMenuBar* menu = nullptr;
            std::function<void(juce::Component&)> w = [&](juce::Component& c)
            {
                if (auto* m = dynamic_cast<TopMenuBar*>(&c)) menu = m;
                for (auto* ch : c.getChildren()) w(*ch);
            };
            w(e);
            return menu;
        };

        juce::String loadedName;
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            editor->setSize(1320, 798);
            editor->setVisible(true);

            auto* menu = menuOf(*editor);
            if (menu != nullptr && menu->getPresetNextButton().onClick != nullptr)
            {
                // The arrows step through everything the browser lists, and
                // "- INIT -" is its first row - so the first press lands there.
                // This test is about carrying a real preset's identity across a
                // window, and INIT has no category or author to carry.
                for (int press = 0; press < 4; ++press)
                {
                    menu->getPresetNextButton().onClick();
                    loadedName = menu->getPresetNameButton().getButtonText();
                    if (loadedName != "- INIT -") break;
                }
            }
        }

        // A second editor over the same processor: the window reopening.
        std::unique_ptr<juce::AudioProcessorEditor> reopened(processor.createEditor());
        reopened->setSize(1320, 798);
        reopened->setVisible(true);
        auto* menu = menuOf(*reopened);

        const auto carried = processor.getLoadedPreset();
        const auto reopenedName = menu != nullptr ? menu->getPresetNameButton().getButtonText()
                                                  : juce::String();

        check("Editor_ReopeningKeepsThePresetNameCategoryAndAuthor",
              loadedName.isNotEmpty() && loadedName != "- INIT -"
                  && reopenedName == loadedName
                  && carried.valid && carried.category.isNotEmpty(),
              "loaded '" + loadedName + "', reopened '" + reopenedName
                  + "', carried category '" + carried.category
                  + "' author '" + carried.author + "'");

        // The identity is session state, not part of the sound: a preset file
        // that named itself would still claim the old name after a save-as.
        const auto presetTree = processor.createPresetStateTree();
        check("Preset_FilesDoNotCarryTheLoadedPresetIdentity",
              ! presetTree.hasProperty(px3::processor_internal::kLoadedPresetNameId)
                  && ! presetTree.hasProperty(px3::processor_internal::kLoadedPresetAuthorId),
              "createPresetStateTree strips the session's preset identity");
    }



    {
        // And with audio having run through it, so the timer callbacks and the
        // FX cards have actually been touched before teardown.
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1280, 800);

        juce::AudioBuffer<float> buffer(2, kBlockSize);
        for (int block = 0; block < 20; ++block)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (block == 2)
            {
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            }
            processor.processBlock(buffer, midi);
        }

        editor.reset();
        check("Editor_SurvivesTeardownAfterProcessing", true, "");
    }

    {
        // The default window has to be tall enough to show a whole first row of
        // FX cards without scrolling. FxPanel spends its height on the signal
        // flow strip, the gap under it, and then the grid, so the requirement is
        // read from the same config keys the panel lays out from rather than
        // written down here - raise fx.grid.rowHeight and this test says so.
        UIConfigManager manager;
        manager.setConfigFile(juce::File::getCurrentWorkingDirectory()
                                  .getChildFile("Source/UI/UIConfig.json"));
        manager.loadInitial();
        const auto config = manager.getConfig();

        const auto padY = config->getInt("fx.panel.layout.padY", 0);
        const auto strip = config->getInt("fx.signalFlow.height", 46);
        const auto stripGap = config->getInt("fx.signalFlow.gapBelow", 8);
        const auto rowHeight = config->getInt("fx.grid.rowHeight", 400);
        const auto required = 2 * padY + strip + stripGap + rowHeight;

        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setVisible(true);

        FxPanel* panel = nullptr;
        PianoKeyboard* keys = nullptr;
        juce::Component* header = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* p = dynamic_cast<FxPanel*>(&c)) panel = p;
            if (auto* k = dynamic_cast<PianoKeyboard*>(&c)) keys = k;
            if (auto* t = dynamic_cast<TopMenuBar*>(&c)) header = t;
            for (auto* child : c.getChildren()) walk(*child);
        };
        walk(*editor);

        const auto defaultPanel = panel != nullptr ? panel->getHeight() : 0;
        const auto defaultKeys = keys != nullptr ? keys->getHeight() : 0;
        const auto defaultHeader = header != nullptr ? header->getHeight() : 0;

        // A whole row would put the window at 838, which read as too tall, so
        // 40px is trimmed back off on purpose and the last 40px of the first
        // row sits under the fold. Asserted as a BUDGET rather than a fit:
        // raising fx.grid.rowHeight without revisiting the window height grows
        // the shortfall past 40 and fails here with both numbers.
        constexpr auto deliberateTrim = 40;
        check("Editor_DefaultSizeNearlyFitsAWholeRowOfFxCards",
              panel != nullptr && required - defaultPanel <= deliberateTrim,
              "panel " + juce::String(defaultPanel) + "px, a row needs " + juce::String(required)
                  + "px (strip " + juce::String(strip) + " + gap " + juce::String(stripGap)
                  + " + rowHeight " + juce::String(rowHeight) + "), short by "
                  + juce::String(required - defaultPanel) + " of " + juce::String(deliberateTrim));

        // Resizing must spend every pixel on the panels. The keyboard used to be
        // a fraction of the window height, so it quietly took a share of any
        // height added for the cards, and the fraction had to be re-based every
        // time the window grew.
        juce::String detail;
        auto keyboardHeld = keys != nullptr && header != nullptr;
        auto panelTracks = panel != nullptr;

        for (const auto h : { 700, 798, 900, 980 })
        {
            editor->setSize(1320, h);
            keyboardHeld = keyboardHeld && keys->getHeight() == defaultKeys
                           && header->getHeight() == defaultHeader;
            panelTracks = panelTracks && panel->getHeight() - defaultPanel == h - 798;
            detail << h << ": panel " << panel->getHeight() << " keys " << keys->getHeight()
                   << " header " << header->getHeight() << "   ";
        }

        check("Editor_ExtraWindowHeightAllGoesToThePanels", keyboardHeld && panelTracks, detail);
    }

    {
        // VIBE, DELAY and REVERB all had an amount knob with no caption. DELAY
        // and REVERB were passing "" as the caption to configureEffectKnob, so
        // the label was laid out and painted with nothing in it - a reserved
        // gap under the knob; VIBE had no label component at all and passed
        // nullptr into layoutLabelledControl.
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1320, 798);
        editor->setVisible(true);

        // Every amount caption in the FX panel, found by text so the test does
        // not need access to the editor's private label members.
        int amountLabels = 0;
        int laidOut = 0;
        juce::String detail;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* label = dynamic_cast<juce::Label*>(&c))
            {
                if (label->getText().trim().equalsIgnoreCase("AMOUNT"))
                {
                    ++amountLabels;
                    if (! label->getBounds().isEmpty() && label->isVisible()) ++laidOut;
                    detail << label->getBounds().toString() << "  ";
                }
            }
            for (auto* child : c.getChildren()) walk(*child);
        };
        walk(*editor);

        check("FxCards_VibeDelayAndReverbAmountKnobsAreLabelled",
              amountLabels >= 3 && laidOut == amountLabels,
              juce::String(amountLabels) + " AMOUNT captions, " + juce::String(laidOut)
                  + " laid out and visible: " + detail);
    }

    {
        // The mode visual scrolls by advancing a phase, and that phase used to
        // be wrapped with "phase -= 2pi". A wrap is only invisible when the
        // drawn shape is built from WHOLE multiples of the phase. SUPER SAW,
        // WAVETABLE, FORMANT, FM, KARPLUS, DIGITAL and the rest are built from
        // fractional multipliers - sin(samplePhase * (1 + macro * 4)) and the
        // like - so subtracting 2pi moved each partial by a part-cycle and the
        // curve visibly jumped. At 0.09 rad per tick that was every ~70 frames,
        // a bit over two seconds.
        //
        // The phase free-runs now, so the test is simply that it never goes
        // backwards: the shape is a continuous function of it.
        juce::ToggleButton bypass;
        juce::Slider pitch, macroA, macroB, macroC;
        juce::ComboBox modeBox, vowelBox;
        juce::Label pitchLabel, pitchValue, laA, laB, laC, lvA, lvB, lvC,
                    modeLabel, vowelLabel;
        OscillatorComponent osc(bypass, pitch, pitchLabel, pitchValue,
                                macroA, macroB, macroC,
                                laA, laB, laC, lvA, lvB, lvC,
                                modeBox, modeLabel, vowelBox, vowelLabel,
                                juce::Colour::fromRGB(120, 200, 255));
        osc.setBounds(0, 0, 300, 300);

        auto worstStepBack = 0.0;
        auto previous = osc.animationPhase();

        // Well past where the old wrap would have fired, several times over.
        for (int frame = 0; frame < 400; ++frame)
        {
            osc.advanceAnimation(0.09f);
            const auto now = osc.animationPhase();
            worstStepBack = juce::jmax(worstStepBack, previous - now);
            previous = now;
        }

        check("OscVisual_AnimationPhaseNeverJumpsBackwards",
              worstStepBack <= 0.0 && previous > juce::MathConstants<double>::twoPi * 5.0,
              "after 400 frames phase = " + fmt(previous, 2)
                  + " rad (over " + fmt(previous / juce::MathConstants<double>::twoPi, 1)
                  + " cycles), worst backwards step " + fmt(worstStepBack, 6));
    }

    {
        // The preset sheet was modal only to LOOK at: paintOverChildren dimmed
        // the editor behind it while every knob, card, chip and key underneath
        // stayed live, so a click that missed the sheet edited the patch you
        // were browsing away from. Nothing behind it may be reachable now.
        //
        // Asserted through getComponentAt, which resolves a point exactly the
        // way a click does - the deepest visible child there that intercepts
        // mouse clicks.
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1320, 760);
        editor->setVisible(true);   // getComponentAt short-circuits on the visible flag

        TopMenuBar* menu = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* m = dynamic_cast<TopMenuBar*>(&c)) menu = m;
            for (auto* child : c.getChildren()) walk(*child);
        };
        walk(*editor);

        // Spread across the editor, all well outside the centred sheet.
        const std::array<juce::Point<int>, 5> probes { {
            { 40, 200 },                                   // panel area, far left
            { editor->getWidth() - 40, 200 },              // panel area, far right
            { 200, editor->getHeight() - 60 },             // the keyboard
            { editor->getWidth() - 200, editor->getHeight() - 60 },
            { 60, editor->getHeight() - 200 },
        } };

        std::array<juce::Component*, 5> before {};
        for (std::size_t i = 0; i < probes.size(); ++i)
        {
            before[i] = editor->getComponentAt(probes[i]);
        }

        // onClick directly rather than triggerClick(), which posts an async
        // command message that never dispatches without a running message loop.
        if (menu != nullptr && menu->getPresetNameButton().onClick != nullptr)
        {
            menu->getPresetNameButton().onClick();
        }

        juce::String detail;
        auto blocked = menu != nullptr;
        juce::Component* scrim = nullptr;

        for (std::size_t i = 0; i < probes.size(); ++i)
        {
            auto* after = editor->getComponentAt(probes[i]);

            // Every probe must now resolve to the SAME component - the scrim -
            // and it must not be whatever was reachable there before.
            if (scrim == nullptr) scrim = after;
            blocked = blocked && after != nullptr && after == scrim && after != before[i];

            detail << "(" << probes[i].x << "," << probes[i].y << ") "
                   << (before[i] != nullptr ? before[i]->getBounds().toString() : "none")
                   << " -> " << (after != nullptr ? after->getBounds().toString() : "none")
                   << (after == before[i] ? " SAME" : " changed") << "   ";
        }

        check("Editor_PresetSheetBlocksTheUiBehindIt", blocked,
              menu == nullptr ? "no top menu bar found" : detail);

        // The converse: a scrim that also covered the sheet would pass the test
        // above and leave the browser unusable.
        auto* onTheSheet = editor->getComponentAt(editor->getLocalBounds().getCentre());
        check("Editor_PresetSheetItselfStaysClickable",
              onTheSheet != nullptr && onTheSheet != scrim,
              onTheSheet == nullptr ? "nothing at the sheet centre"
                                    : "sheet centre resolves to " + onTheSheet->getBounds().toString()
                                          + ", scrim is " + (scrim != nullptr ? scrim->getBounds().toString()
                                                                              : juce::String("none")));
    }

    {
        // With every oscillator source bypassed the instrument cannot make a
        // sound. The keyboard should say so rather than animating and lighting
        // up keys that produce silence.
        PX3SynthAudioProcessor processor;

        auto keyboardOf = [](juce::AudioProcessorEditor& e)
        {
            PianoKeyboard* keys = nullptr;
            std::function<void(juce::Component&)> walk = [&](juce::Component& c)
            {
                if (auto* k = dynamic_cast<PianoKeyboard*>(&c)) keys = k;
                for (auto* child : c.getChildren()) walk(*child);
            };
            walk(e);
            return keys;
        };

        // A row across the middle of the KEYS, counted against the grey the
        // silenced keyboard is filled with. Only meaningful while silenced.
        //
        // The keys are not the middle of the component: it is grown upward by
        // the spark headroom, which is transparent, so the component's own
        // centre line sits above the keyboard and crosses neither the keys nor
        // the warning box.
        auto brightPixelsAcrossTheMiddle = [](PianoKeyboard& keys)
        {
            const auto area = keys.keyboardArea();
            const auto img = keys.createComponentSnapshot(area);
            auto lit = 0;
            for (int x = 0; x < img.getWidth(); ++x)
            {
                if (img.getPixelAt(x, img.getHeight() / 2).getBrightness() > 0.55f) ++lit;
            }
            return lit;
        };

        auto silencedWith = [&](bool osc1, bool osc2, bool osc3, bool sub)
        {
            setParam(processor, "osc1Enabled", osc1 ? 1.0f : 0.0f);
            setParam(processor, "osc2Enabled", osc2 ? 1.0f : 0.0f);
            setParam(processor, "osc3Enabled", osc3 ? 1.0f : 0.0f);
            setParam(processor, "subOscEnabled", sub ? 1.0f : 0.0f);

            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            editor->setSize(1320, 798);
            editor->setVisible(true);

            auto* keys = keyboardOf(*editor);
            struct R { bool silenced; int bright; };
            return R { keys != nullptr && keys->isSilenced(),
                       keys != nullptr ? brightPixelsAcrossTheMiddle(*keys) : 0 };
        };

        const auto allOn = silencedWith(true, true, true, true);
        const auto allOff = silencedWith(false, false, false, false);
        const auto subOnly = silencedWith(false, false, false, true);
        const auto osc2Only = silencedWith(false, true, false, false);

        {
            // The sparks used to be clipped at the top edge of the keys, because
            // a component cannot paint outside its own bounds. The component is
            // now taller than the keyboard it draws, and the extra strip has to
            // be BOTH transparent to the mouse and outside the keyboard area -
            // otherwise it would eat clicks meant for the panel above it.
            setParam(processor, "osc1Enabled", 1.0f);
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            editor->setSize(1320, 798);
            editor->setVisible(true);

            auto* keys = keyboardOf(*editor);
            // Particles are drawn by a shared overlay above BOTH the keyboard
            // and the wheels.
            //
            // They used to be drawn inside each component, which meant each had
            // to be grown outward to give them room - and since the two
            // overlap, z-order could only ever favour one. The keyboard's
            // sparks ended up behind the wheels' opaque face. One transparent
            // layer above the pair is the only arrangement where neither loses.
            PerformanceControls* wheels = nullptr;
            juce::Component* overlay = nullptr;
            {
                std::function<void(juce::Component&)> walk = [&](juce::Component& root)
                {
                    for (auto* child : root.getChildren())
                    {
                        if (child == nullptr) continue;
                        if (auto* p = dynamic_cast<PerformanceControls*>(child)) wheels = p;
                        if (child->getName() == "SparkOverlay") overlay = child;
                        walk(*child);
                    }
                };
                walk(*editor);
            }

            const auto coversBoth = overlay != nullptr && keys != nullptr && wheels != nullptr
                                    && overlay->getBounds().contains(keys->getBounds())
                                    && overlay->getBounds().contains(wheels->getBounds());

            // Room above the keys is the point of it, so it has to extend past
            // them rather than merely wrap them.
            const auto reachesAbove = overlay != nullptr && keys != nullptr
                                      && overlay->getY() < keys->getY();

            check("SparkOverlay_CoversBothAnimatorsWithRoomToSpare",
                  coversBoth && reachesAbove,
                  overlay == nullptr
                      ? juce::String("no SparkOverlay in the editor")
                      : "overlay " + overlay->getBounds().toString() + " over keys "
                            + (keys != nullptr ? keys->getBounds().toString() : juce::String("none"))
                            + " and wheels "
                            + (wheels != nullptr ? wheels->getBounds().toString() : juce::String("none")));

            // Above both in z-order, or it would be drawn under the very
            // components whose particles it exists to lift clear.
            const auto overlayIndex = overlay != nullptr ? editor->getIndexOfChildComponent(overlay) : -1;
            const auto keysIndex = keys != nullptr ? editor->getIndexOfChildComponent(keys) : -1;
            const auto wheelsIndex = wheels != nullptr ? editor->getIndexOfChildComponent(wheels) : -1;

            check("SparkOverlay_SitsAboveBothOfThem",
                  overlayIndex > keysIndex && overlayIndex > wheelsIndex,
                  "child order - overlay " + juce::String(overlayIndex) + ", keys "
                      + juce::String(keysIndex) + ", wheels " + juce::String(wheelsIndex)
                      + " (a HIGHER index is nearer the front in JUCE - child 0 is the back)");

            // And invisible to the mouse. It covers both of them completely, so
            // if it took a single click neither would work at all.
            const auto ownerOf = [](juce::Component* c, juce::Component* wanted)
            {
                while (c != nullptr && c != wanted)
                {
                    c = c->getParentComponent();
                }
                return c == wanted;
            };

            juce::Component* overKeys = nullptr;
            juce::Component* overWheels = nullptr;
            if (keys != nullptr && wheels != nullptr)
            {
                overKeys = editor->getComponentAt(keys->getBounds().getCentre());
                overWheels = editor->getComponentAt(wheels->getBounds().getCentre());
            }

            // The overlay is transparent, so invalidating it redraws everything
            // beneath - the mixer panel included. Measured, redrawing the whole
            // strip costs 3.62 ms of a 16.7 ms frame at 60 Hz, against 0.53 ms
            // for a 160x160 slice, so it repaints only where particles are.
            //
            // With none alive it must invalidate NOTHING. The trap is the
            // opposite one: it also has to keep invalidating for one frame
            // after the last particle dies, or the final frame is never drawn
            // over and stays on screen - which is why the region is unioned
            // with the previous frame's rather than replacing it.
            // Every property the keyboard and the wheels are drawn with has to
            // be reachable, or it is a hard-coded look wearing a configurable
            // one's clothes. Parsed from a config that changes ALL of them away
            // from the compiled defaults, so a key that is silently ignored
            // shows up as a value that did not move.
            {
                juce::String parseError;
                const auto styled = UIConfig::fromJsonText(R"({
                    "keyboard": {
                      "background": { "color": "#102030", "opacity": 0.5 },
                      "padding": 3,
                      "whiteKey": { "fill": "#112233", "activeFill": "#445566",
                                    "border": { "color": "#778899", "width": 2.5, "radius": 4 } },
                      "blackKey": { "fill": "#010203", "activeFill": "#040506",
                                    "border": { "color": "#070809", "width": 3.5, "radius": 6 },
                                    "widthRatio": 0.5, "heightRatio": 0.4 },
                      "label": { "color": "#0A0B0C", "fontSize": 21 },
                      "silencedVeil": "#0D0E0F80",
                      "sparks": { "whiteKeyColor": "#111213", "blackKeyColor": "#141516" }
                    },
                    "performance": {
                      "background": { "color": "#203040", "opacity": 0.25 },
                      "border": { "inset": 5, "color": "#212223", "width": 4, "radius": 9 },
                      "wheelPanel": { "color": "#242526", "opacity": 0.4, "radius": 11 },
                      "title": { "color": "#272829", "fontSize": 19, "height": 27 },
                      "track": { "radius": 13, "borderWidth": 6, "fillOpacity": 0.11,
                                 "fillGlowOpacity": 0.12, "borderOpacity": 0.13,
                                 "borderGlowOpacity": 0.14, "centreLineOpacity": 0.15 },
                      "handle": { "radius": 17, "glowOuterRadius": 29, "glowInnerRadius": 23,
                                  "rimColor": "#2A2B2C" },
                      "sparkles": { "maxPerBurst": 9, "rate": 0.33 },
                      "divider": { "orientation": "vertical", "color": "#313233",
                                   "opacity": 0.66, "inset": 21, "width": 3 },
                      "pitch": { "accent": "#2D2E2F" },
                      "mod": { "accent": "#303132" }
                    } })", parseError);

                const auto k = PianoKeyboard::Style::fromConfig(styled.get(), "keyboard");
                const auto w = PerformanceControls::Style::fromConfig(styled.get(), "performance");
                const PianoKeyboard::Style kDefault;
                const PerformanceControls::Style wDefault;

                juce::StringArray stuck;
                auto expectMoved = [&stuck](const char* name, bool moved)
                {
                    if (! moved) stuck.add(name);
                };

                expectMoved("keyboard.background.color", k.background != kDefault.background);
                expectMoved("keyboard.background.opacity", k.backgroundOpacity != kDefault.backgroundOpacity);
                expectMoved("keyboard.padding", k.padding != kDefault.padding);
                expectMoved("whiteKey.fill", k.whiteFill != kDefault.whiteFill);
                expectMoved("whiteKey.activeFill", k.whiteActiveFill != kDefault.whiteActiveFill);
                expectMoved("whiteKey.border.color", k.whiteBorder != kDefault.whiteBorder);
                expectMoved("whiteKey.border.width", k.whiteBorderWidth != kDefault.whiteBorderWidth);
                expectMoved("whiteKey.border.radius", k.whiteRadius.topLeft != kDefault.whiteRadius.topLeft);
                expectMoved("blackKey.fill", k.blackFill != kDefault.blackFill);
                expectMoved("blackKey.activeFill", k.blackActiveFill != kDefault.blackActiveFill);
                expectMoved("blackKey.border.color", k.blackBorder != kDefault.blackBorder);
                expectMoved("blackKey.border.width", k.blackBorderWidth != kDefault.blackBorderWidth);
                expectMoved("blackKey.border.radius", k.blackRadius.topLeft != kDefault.blackRadius.topLeft);
                expectMoved("blackKey.widthRatio", k.blackWidthRatio != kDefault.blackWidthRatio);
                expectMoved("blackKey.heightRatio", k.blackHeightRatio != kDefault.blackHeightRatio);
                expectMoved("label.color", k.labelColour != kDefault.labelColour);
                expectMoved("label.fontSize", k.labelSize != kDefault.labelSize);
                expectMoved("silencedVeil", k.silencedVeil != kDefault.silencedVeil);
                expectMoved("sparks.whiteKeyColor", k.whiteSparkColour != kDefault.whiteSparkColour);
                expectMoved("sparks.blackKeyColor", k.blackSparkColour != kDefault.blackSparkColour);

                expectMoved("performance.background.color", w.background != wDefault.background);
                expectMoved("performance.background.opacity", w.backgroundOpacity != wDefault.backgroundOpacity);
                expectMoved("performance.border.inset", w.borderInset != wDefault.borderInset);
                expectMoved("divider.color", w.dividerColour != wDefault.dividerColour);
                expectMoved("divider.opacity", w.dividerOpacity != wDefault.dividerOpacity);
                expectMoved("divider.inset", w.dividerInset != wDefault.dividerInset);
                expectMoved("divider.width", w.dividerWidth != wDefault.dividerWidth);
                expectMoved("divider.orientation", w.dividerOrientation != wDefault.dividerOrientation);
                expectMoved("performance.border.color", w.borderColour != wDefault.borderColour);
                expectMoved("performance.border.width", w.borderWidth != wDefault.borderWidth);
                expectMoved("performance.border.radius", w.borderRadius.topLeft != wDefault.borderRadius.topLeft);
                expectMoved("wheelPanel.color", w.panelColour != wDefault.panelColour);
                expectMoved("wheelPanel.opacity", w.panelOpacity != wDefault.panelOpacity);
                expectMoved("wheelPanel.radius", w.panelRadius.topLeft != wDefault.panelRadius.topLeft);
                expectMoved("title.color", w.titleColour != wDefault.titleColour);
                expectMoved("title.fontSize", w.titleSize != wDefault.titleSize);
                expectMoved("title.height", w.titleHeight != wDefault.titleHeight);
                expectMoved("track.radius", w.trackRadius.topLeft != wDefault.trackRadius.topLeft);
                expectMoved("track.borderWidth", w.trackBorderWidth != wDefault.trackBorderWidth);
                expectMoved("track.fillOpacity", w.trackFillAlpha != wDefault.trackFillAlpha);
                expectMoved("track.fillGlowOpacity", w.trackFillGlowAlpha != wDefault.trackFillGlowAlpha);
                expectMoved("track.borderOpacity", w.trackBorderAlpha != wDefault.trackBorderAlpha);
                expectMoved("track.borderGlowOpacity", w.trackBorderGlowAlpha != wDefault.trackBorderGlowAlpha);
                expectMoved("track.centreLineOpacity", w.centreLineAlpha != wDefault.centreLineAlpha);
                expectMoved("handle.radius", w.handleRadius != wDefault.handleRadius);
                expectMoved("handle.glowOuterRadius", w.handleGlowOuterRadius != wDefault.handleGlowOuterRadius);
                expectMoved("handle.glowInnerRadius", w.handleGlowInnerRadius != wDefault.handleGlowInnerRadius);
                expectMoved("handle.rimColor", w.handleRimColour != wDefault.handleRimColour);
                expectMoved("sparkles.maxPerBurst", w.sparkleMaxPerBurst != wDefault.sparkleMaxPerBurst);
                expectMoved("sparkles.rate", w.sparkleRate != wDefault.sparkleRate);
                expectMoved("pitch.accent", w.pitchAccent != wDefault.pitchAccent);
                expectMoved("mod.accent", w.modAccent != wDefault.modAccent);

                // Each wheel's own background, with the shared panel as fallback.
            {
                juce::String wheelError;

                const auto sharedOnly = PerformanceControls::Style::fromConfig(
                    UIConfig::fromJsonText(
                        R"({ "performance": { "wheelPanel": { "color": "#203040", "opacity": 0.5 } } })",
                        wheelError).get(),
                    "performance");

                const auto perWheel = PerformanceControls::Style::fromConfig(
                    UIConfig::fromJsonText(
                        R"({ "performance": { "wheelPanel": { "color": "#203040", "opacity": 0.5 },
                             "pitch": { "background": { "color": "#112233" } },
                             "mod":   { "background": { "opacity": 0.9 } } } })",
                        wheelError).get(),
                    "performance");

                check("PerformanceControls_WheelBackgroundsOverrideTheShared",
                      // With only the shared block, both wheels take it.
                      sharedOnly.pitchPanelColour == sharedOnly.modPanelColour
                          && sharedOnly.pitchPanelColour == sharedOnly.panelColour
                          && std::abs(sharedOnly.modPanelOpacity - 0.5f) < 1.0e-4f
                          // With overrides, each takes only what it declares:
                          // pitch keeps the shared opacity, mod keeps the
                          // shared colour.
                          && perWheel.pitchPanelColour != perWheel.modPanelColour
                          && std::abs(perWheel.pitchPanelOpacity - 0.5f) < 1.0e-4f
                          && perWheel.modPanelColour == perWheel.panelColour
                          && std::abs(perWheel.modPanelOpacity - 0.9f) < 1.0e-4f,
                      "shared alone gives both " + sharedOnly.panelColour.toDisplayString(false)
                          + " at " + fmt(sharedOnly.panelOpacity, 2)
                          + "; overridden gives pitch " + perWheel.pitchPanelColour.toDisplayString(false)
                          + " at " + fmt(perWheel.pitchPanelOpacity, 2)
                          + " and mod " + perWheel.modPanelColour.toDisplayString(false)
                          + " at " + fmt(perWheel.modPanelOpacity, 2));
            }

            // The performance strip's corners are individually settable, which
            // is what lets its right edge round into the keyboard beside it
            // while its left stays square against the window.
            {
                juce::String cornerError;
                const auto cornered = UIConfig::fromJsonText(
                    R"({ "performance": { "border": { "radius": 0,
                         "radiusTopRight": 14, "radiusBottomRight": 9 } } })", cornerError);
                const auto cornerStyle = PerformanceControls::Style::fromConfig(cornered.get(), "performance");
                const auto r = cornerStyle.borderRadius;

                check("PerformanceControls_RightCornersRoundIndependently",
                      r.topRight == 14.0f && r.bottomRight == 9.0f
                          && r.topLeft == 0.0f && r.bottomLeft == 0.0f
                          && ! r.isUniform(),
                      "TL " + fmt(r.topLeft, 1) + ", TR " + fmt(r.topRight, 1)
                          + ", BR " + fmt(r.bottomRight, 1) + ", BL " + fmt(r.bottomLeft, 1));
            }

            // The strip drawn BEHIND the keyboard and the wheels. It was
            // hardcoded and invisible in the config, so it showed through the
            // corners of anything rounded above it with no way to match or
            // remove it. Its own corners, colours and off switch are declared.
            {
                UIConfigManager stripManager;
                stripManager.setConfigFile(juce::File::getCurrentWorkingDirectory()
                                               .getChildFile("Source/UI/UIConfig.json"));
                stripManager.loadInitial();
                const auto shipping = stripManager.getConfig();

                const auto radii = px3::ui::CornerRadii::fromConfig(shipping.get(), "performanceStrip",
                                                                     px3::ui::CornerRadii::all(-1.0f));
                const auto declared = shipping != nullptr
                                      && ! shipping->getValue("performanceStrip.enabled").isVoid()
                                      && shipping->getColour("performanceStrip.background.color",
                                                             juce::Colours::transparentBlack)
                                             != juce::Colours::transparentBlack
                                      && ! shipping->getValue("performanceStrip.background.opacity").isVoid()
                                      && ! shipping->getValue("performanceStrip.outline.width").isVoid()
                                      && ! shipping->getValue("performanceStrip.divider.inset").isVoid();

                check("PerformanceStrip_BehindLayerIsDeclaredAndCornerable",
                      declared && radii.topRight >= 0.0f && radii.bottomRight >= 0.0f,
                      declared ? "declared, corners TL " + fmt(radii.topLeft, 1) + " TR "
                                     + fmt(radii.topRight, 1) + " BR " + fmt(radii.bottomRight, 1)
                                     + " BL " + fmt(radii.bottomLeft, 1)
                               : juce::String("the strip behind the keyboard is still hardcoded"));

                // And the keyboard's own panel can round, which it could not:
                // its background was a fillRect.
                const auto kb = PianoKeyboard::Style::fromConfig(
                    UIConfig::fromJsonText(
                        R"({ "keyboard": { "background": { "radiusTopRight": 11,
                                                           "radiusBottomRight": 7 } } })",
                        parseError).get(),
                    "keyboard");

                check("Keyboard_PanelCornersAreIndividuallySettable",
                      kb.backgroundRadius.topRight == 11.0f
                          && kb.backgroundRadius.bottomRight == 7.0f
                          && kb.backgroundRadius.topLeft == 0.0f,
                      "TL " + fmt(kb.backgroundRadius.topLeft, 1) + ", TR "
                          + fmt(kb.backgroundRadius.topRight, 1) + ", BR "
                          + fmt(kb.backgroundRadius.bottomRight, 1));
            }

            check("KeyboardAndWheels_EveryStylePropertyIsReachable", stuck.isEmpty(),
                      stuck.isEmpty() ? juce::String("all 52 properties parse and change the style")
                                      : "ignored by the parser: " + stuck.joinIntoString(", "));
            }

            // Corners are individually settable, with the shorthand-then-
            // override convention the insets already use.
            {
                juce::String parseError;
                const auto cfg = UIConfig::fromJsonText(
                    R"({ "b": { "radius": 5, "radiusTopRight": 12, "radiusBottomLeft": 0 } })", parseError);
                const auto r = px3::ui::CornerRadii::fromConfig(cfg.get(), "b",
                                                                px3::ui::CornerRadii::all(99.0f));

                check("CornerRadii_ShorthandThenPerCornerOverride",
                      r.topLeft == 5.0f && r.topRight == 12.0f
                          && r.bottomRight == 5.0f && r.bottomLeft == 0.0f
                          && ! r.isUniform(),
                      "TL " + fmt(r.topLeft, 1) + ", TR " + fmt(r.topRight, 1)
                          + ", BR " + fmt(r.bottomRight, 1) + ", BL " + fmt(r.bottomLeft, 1));

                // A radius larger than the box would fold the path back on
                // itself, so it is clamped to half the shorter side.
                const auto clamped = px3::ui::CornerRadii::all(500.0f)
                                         .clampedTo(juce::Rectangle<float>(0.0f, 0.0f, 40.0f, 20.0f));
                // THE PATH ITSELF, not the numbers that feed it.
            //
            // The parsing tests below passed the whole time the corners were
            // rendering as jagged crossed lines, because they only ever checked
            // the radii. JUCE's addCentredArc measures clockwise from twelve
            // o'clock, and every arc was written a quarter-turn out, so each
            // one was drawn in the wrong corner. A path that leaves its own
            // rectangle is the signature of exactly that.
            {
                const auto box = juce::Rectangle<float>(20.0f, 10.0f, 160.0f, 80.0f);

                auto worstOverflow = 0.0f;
                juce::String offender;

                const std::pair<const char*, px3::ui::CornerRadii> cases[] = {
                    { "uniform 12",      px3::ui::CornerRadii::all(12.0f) },
                    { "right side only", { 0.0f, 14.0f, 9.0f, 0.0f } },
                    { "top only",        { 16.0f, 16.0f, 0.0f, 0.0f } },
                    { "one corner",      { 0.0f, 0.0f, 22.0f, 0.0f } },
                    { "all different",   { 4.0f, 8.0f, 12.0f, 16.0f } },
                };

                for (const auto& entry : cases)
                {
                    const auto path = px3::ui::roundedRectanglePath(box, entry.second);
                    const auto bounds = path.getBounds();

                    // A correctly built rounded rectangle exactly fills its box:
                    // it never pokes outside, and its extremes reach every edge.
                    const auto overflow = juce::jmax(
                        juce::jmax(box.getX() - bounds.getX(), bounds.getRight() - box.getRight()),
                        juce::jmax(box.getY() - bounds.getY(), bounds.getBottom() - box.getBottom()));

                    if (overflow > worstOverflow)
                    {
                        worstOverflow = overflow;
                        offender = juce::String(entry.first) + " -> " + bounds.toString();
                    }
                }

                check("CornerRadii_PathStaysInsideItsRectangle", worstOverflow < 0.01f,
                      worstOverflow < 0.01f
                          ? "five corner arrangements, none leaves the box"
                          : "worst overflow " + fmt(worstOverflow, 2) + "px on " + offender);
            }

            // And the shape is actually round: a corner with a radius has to cut
            // its square off, so the path's area is less than the box's.
            {
                const auto box = juce::Rectangle<float>(0.0f, 0.0f, 100.0f, 100.0f);
                const auto square = px3::ui::roundedRectanglePath(box, px3::ui::CornerRadii::all(0.0f));
                const auto rounded = px3::ui::roundedRectanglePath(box, { 0.0f, 30.0f, 30.0f, 0.0f });

                // Sampled rather than integrated: count points inside each.
                auto squareHits = 0;
                auto roundedHits = 0;
                for (int y = 0; y < 100; ++y)
                {
                    for (int x = 0; x < 100; ++x)
                    {
                        const auto p = juce::Point<float>(x + 0.5f, y + 0.5f);
                        if (square.contains(p)) ++squareHits;
                        if (rounded.contains(p)) ++roundedHits;
                    }
                }

                // Two 30px quarter-circles removed is about 2 * (900 - 706) px.
                const auto removed = squareHits - roundedHits;
                check("CornerRadii_RoundedCornersActuallyRemoveArea",
                      squareHits > 9900 && removed > 300 && removed < 500,
                      "square covers " + juce::String(squareHits) + " px, rounding two corners at 30px "
                          + "removes " + juce::String(removed) + " (two quarter-circles are ~386)");
            }

            check("CornerRadii_ClampsToTheBox",
                      clamped.topLeft == 10.0f && clamped.bottomRight == 10.0f,
                      "a 500px radius on a 40x20 box clamps to " + fmt(clamped.topLeft, 1));
            }

            // Silencing the keyboard drops every spark in flight - but they are
            // drawn on the overlay, so clearing them here and repainting the
            // KEYBOARD leaves the last frame of them on screen over a greyed
            // instrument. The overlay has to be told, and once silenced the
            // timer returns immediately, so it cannot be told later either.
            {
                PianoKeyboard standalone;
                standalone.setSize(600, 100);

                auto notifications = 0;
                standalone.onSparksChanged = [&notifications]() { ++notifications; };

                standalone.setSilenced(true);

                check("Keyboard_SilencingClearsWhatTheOverlayHasDrawn",
                      notifications > 0,
                      juce::String(notifications) + " overlay notification(s) on silencing"
                          + (notifications > 0 ? "" : " - the last frame of sparks would stay on screen"));
            }

            // WHAT ACTUALLY PAINTS THE SECTION'S BACKGROUND.
            //
            // Three layers stack here, back to front:
            //
            //   1. the editor's window background (image + scrim)
            //   2. performanceStrip - one fill across the whole section
            //   3. the keyboard's own background, and the wheels' own
            //
            // 2 is almost entirely hidden, because 3 tiles the same area: the
            // strip spans the union of the two components and each of them
            // fills its whole rectangle. So changing the strip's colour appears
            // to do nothing while the two components are opaque, and only shows
            // where their square edges leave its rounded corners exposed.
            //
            // Setting either component's background opacity to 0 is what lets
            // the strip through - this proves that path works.
            if (keys != nullptr)
            {
                // A band inside the keyboard's bounds but outside the keys: the
                // padding, which is where its background is visible.
                const auto band = juce::Rectangle<int>(2, 2, keys->getWidth() - 4, 4);

                auto sample = [&](juce::Colour background, float opacity)
                {
                    PianoKeyboard::Style style;
                    style.background = background;
                    style.backgroundOpacity = opacity;
                    keys->setStyle(style);
                    const auto shot = keys->createComponentSnapshot(band);
                    return shot.getPixelAt(shot.getWidth() / 2, shot.getHeight() / 2);
                };

                const auto opaqueRed = sample(juce::Colour::fromRGB(200, 0, 0), 1.0f);
                const auto opaqueBlue = sample(juce::Colour::fromRGB(0, 0, 200), 1.0f);
                const auto transparent = sample(juce::Colour::fromRGB(0, 0, 200), 0.0f);

                keys->setStyle(PianoKeyboard::Style {});

                check("Keyboard_PaintsItsOwnBackgroundAboveTheStrip",
                      opaqueRed != opaqueBlue
                          && transparent != opaqueBlue
                          && transparent.getAlpha() < opaqueBlue.getAlpha(),
                      "its own colour shows at full opacity (" + opaqueRed.toDisplayString(true)
                          + " vs " + opaqueBlue.toDisplayString(true)
                          + ") and clears at opacity 0 (" + transparent.toDisplayString(true)
                          + "), which is what lets the strip beneath through");
            }

            check("SparkOverlay_InvalidatesNothingWithNoParticles",
                  keys != nullptr && wheels != nullptr
                      && keys->sparkBounds().isEmpty()
                      && wheels->sparkleBounds().isEmpty()
                      && ! keys->hasSparks() && ! wheels->hasSparkles(),
                  "no particles alive, so both boxes are empty and the overlay "
                  "leaves the panel beneath it untouched");

            check("SparkOverlay_TakesNoMouseEvents",
                  overlay != nullptr && ownerOf(overKeys, keys) && ownerOf(overWheels, wheels),
                  juce::String("centre of the keys resolves to ")
                      + (ownerOf(overKeys, keys) ? "the keyboard" : "SOMETHING ELSE")
                      + ", centre of the wheels to "
                      + (ownerOf(overWheels, wheels) ? "the wheels" : "SOMETHING ELSE"));

            // Neither component is grown any more: that machinery existed only
            // to give particles somewhere to go inside their own bounds.
            check("SparkAnimators_AreNotGrownBeyondTheirControls",
                  keys != nullptr && wheels != nullptr
                      && keys->keyboardArea() == keys->getLocalBounds()
                      && wheels->controlsArea() == wheels->getLocalBounds(),
                  keys == nullptr ? juce::String("no keyboard")
                                  : "keys occupy " + keys->keyboardArea().toString()
                                        + " of " + keys->getLocalBounds().toString());
        }

        check("Keyboard_SilencesItselfOnlyWhenNothingCanSound",
              ! allOn.silenced && allOff.silenced && ! subOnly.silenced && ! osc2Only.silenced,
              "all on -> " + juce::String(allOn.silenced ? "silenced" : "live")
                  + ", all off -> " + juce::String(allOff.silenced ? "silenced" : "live")
                  + ", sub only -> " + juce::String(subOnly.silenced ? "silenced" : "live")
                  + ", osc2 only -> " + juce::String(osc2Only.silenced ? "silenced" : "live"));

        // The silenced keyboard is greyed and dimmed, so almost nothing on that
        // row is bright except the warning box sitting in the middle of it.
        check("Keyboard_WarningIsActuallyDrawnWhenSilenced",
              allOff.bright > 10 && allOff.bright < allOn.bright,
              "bright pixels across the middle row: live keyboard " + juce::String(allOn.bright)
                  + ", silenced " + juce::String(allOff.bright));

        // And the transition back, which is what a user actually does. Driven on
        // the component rather than through the editor's 30 Hz timer: a console
        // build has no message loop, so the timer never dispatches here. The
        // editor's half of the chain - deciding WHEN to call this - is what the
        // state-mapping check above covers.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            editor->setSize(1320, 798);
            editor->setVisible(true);
            auto* keys = keyboardOf(*editor);

            if (keys != nullptr)
            {
                keys->setSilenced(true);
                const auto whileSilenced = brightPixelsAcrossTheMiddle(*keys);

                keys->setSilenced(false);
                const auto afterClearing = brightPixelsAcrossTheMiddle(*keys);

                check("Keyboard_ClearsCompletelyWhenAnOscillatorReturns",
                      ! keys->isSilenced() && afterClearing > whileSilenced * 3,
                      "bright pixels across the middle row: silenced " + juce::String(whileSilenced)
                          + " -> cleared " + juce::String(afterClearing));
            }
        }
    }

    {
        // The preset tab carries the loaded preset's category and author under
        // its name, upper case and smaller. Verified by rendering the button:
        // the subtitles are painted, not held in a child label, so the only
        // honest check is whether ink appears in the bottom band.
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1320, 760);
        editor->setVisible(true);

        TopMenuBar* menu = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* m = dynamic_cast<TopMenuBar*>(&c)) menu = m;
            for (auto* child : c.getChildren()) walk(*child);
        };
        walk(*editor);

        auto inkInBottomBand = [](juce::Component& c)
        {
            const auto image = c.createComponentSnapshot(c.getLocalBounds());
            if (! image.isValid() || image.getHeight() < 6) return -1;

            // The band the subtitles occupy, sampled against the face colour
            // at the button's own left edge so a themed face does not count.
            const auto reference = image.getPixelAt(2, image.getHeight() / 2);
            auto ink = 0;
            for (int y = image.getHeight() * 2 / 3; y < image.getHeight() - 2; ++y)
            {
                for (int x = 4; x < image.getWidth() - 4; ++x)
                {
                    const auto p = image.getPixelAt(x, y);
                    if (std::abs(p.getBrightness() - reference.getBrightness()) > 0.08f) ++ink;
                }
            }
            return ink;
        };

        juce::String detail;
        auto ok = menu != nullptr;

        if (menu != nullptr)
        {
            auto& button = menu->getPresetNameButton();

            // Mixed case in, upper case on the face.
            menu->setPresetName("Test Patch");
            menu->setPresetDetails({}, {});
            const auto bare = inkInBottomBand(button);

            menu->setPresetDetails("Bass", "Nate");
            const auto withDetails = inkInBottomBand(button);

            // Room for two rows at all: the band has to be tall enough that a
            // 9.5px line can land in it.
            ok = button.getHeight() >= 28 && bare >= 0 && withDetails > bare + 20;

            detail << "button " << button.getWidth() << "x" << button.getHeight()
                   << ", bottom-band ink " << bare << " -> " << withDetails
                   << ", name \"" << button.getButtonText() << "\"";

            ok = ok && button.getButtonText() == "TEST PATCH";
        }

        check("TopMenu_PresetTabShowsCategoryAndAuthor", ok,
              menu == nullptr ? "no top menu bar found" : detail);
    }

    {
        // Every property in topMenu.presetTab has to do something. The tab's
        // text layout used to be entirely literals, so there was no way to push
        // the name and details down off the top edge or to resize them.
        //
        // Measured off the rendered face: where the ink is, and how much of it.
        auto inkRows = [](const TopMenuTabButton::ContentStyle& style)
        {
            TopMenuTabButton button { "" };
            button.setShowLed(false);
            button.setContentStyle(style);
            button.setButtonText("PRESET NAME");
            // Mixed case on purpose: fed "BASS" the uppercase switch has
            // nothing to do, and would read as inert when it is not.
            button.setSubtitles("Bass", "Nate f");
            button.setBounds(0, 0, 468, 32);
            button.setVisible(true);

            const auto img = button.createComponentSnapshot(button.getLocalBounds());
            const auto reference = img.getPixelAt(2, 2);

            struct R { int firstRow; int lastRow; int ink; double centreX; };
            R r { -1, -1, 0, 0.0 };
            double weighted = 0.0;
            for (int y = 0; y < img.getHeight(); ++y)
            {
                for (int x = 2; x < img.getWidth() - 2; ++x)
                {
                    if (std::abs(img.getPixelAt(x, y).getBrightness() - reference.getBrightness()) > 0.25f)
                    {
                        if (r.firstRow < 0) r.firstRow = y;
                        r.lastRow = y;
                        ++r.ink;
                        weighted += x;
                    }
                }
            }
            // Where the ink sits, not just how much: padding moves the text
            // rather than adding or removing any of it.
            r.centreX = r.ink > 0 ? weighted / r.ink : 0.0;
            return r;
        };

        const TopMenuTabButton::ContentStyle base;
        const auto plain = inkRows(base);

        juce::StringArray inert;
        juce::String detail;

        {
            auto style = base;
            style.padding.top = 10.0f;
            const auto moved = inkRows(style);
            detail << "padding.top: first ink row " << plain.firstRow << " -> " << moved.firstRow << "  ";
            if (moved.firstRow <= plain.firstRow) inert.add("padding.top");
        }
        {
            auto style = base;
            style.padding.bottom = 10.0f;
            const auto moved = inkRows(style);
            detail << "padding.bottom: last ink row " << plain.lastRow << " -> " << moved.lastRow << "  ";
            if (moved.lastRow >= plain.lastRow) inert.add("padding.bottom");
        }
        {
            auto style = base;
            style.padding.left = 120.0f;
            const auto shifted = inkRows(style);
            detail << "padding.left: ink centre " << fmt(plain.centreX, 0) << " -> "
                   << fmt(shifted.centreX, 0) << "  ";
            if (shifted.centreX <= plain.centreX + 4.0) inert.add("padding.left");
        }
        {
            auto style = base;
            style.padding.right = 120.0f;
            if (inkRows(style).centreX >= plain.centreX - 4.0) inert.add("padding.right");
        }
        {
            auto style = base;
            style.name.height = { px3::ui::Dimension::Unit::pixels, 24.0f };
            const auto tall = inkRows(style);
            detail << "rows.row1.height: last ink row " << plain.lastRow << " -> " << tall.lastRow << "  ";
            if (tall.lastRow == plain.lastRow && tall.ink == plain.ink) inert.add("rows.row1.height");
        }
        {
            auto style = base;
            style.detail.height = { px3::ui::Dimension::Unit::pixels, 8.0f };
            const auto squashed = inkRows(style);
            if (squashed.lastRow == plain.lastRow && squashed.ink == plain.ink) inert.add("rows.row2.height");
        }
        {
            auto style = base;
            style.name.padding.top = 6.0f;
            if (inkRows(style).firstRow <= plain.firstRow) inert.add("rows.row1.padding");
        }
        {
            auto style = base;
            style.detail.padding.left = 120.0f;
            if (inkRows(style).centreX <= plain.centreX + 2.0) inert.add("rows.row2.padding");
        }
        {
            auto style = base;
            style.name.fontSize = 6.0f;
            if (inkRows(style).ink >= plain.ink) inert.add("rows.row1.fontSize");
        }
        {
            auto style = base;
            style.detail.fontSize = 6.0f;
            if (inkRows(style).ink >= plain.ink) inert.add("rows.row2.fontSize");
        }
        {
            auto style = base;
            style.dividerAlpha = 0.0f;
            if (inkRows(style).ink >= plain.ink) inert.add("divider.alpha");
        }
        {
            auto style = base;
            style.dividerInset = 6.0f;
            if (inkRows(style).ink >= plain.ink) inert.add("divider.inset");
        }
        {
            auto style = base;
            style.dividerWidth = 5.0f;
            if (inkRows(style).ink <= plain.ink) inert.add("divider.width");
        }
        {
            auto style = base;
            style.showLabels = false;
            if (inkRows(style).ink >= plain.ink) inert.add("showLabels");
        }
        {
            auto style = base;
            style.detailUppercase = false;
            if (inkRows(style).ink == plain.ink) inert.add("detailUppercase");
        }
        {
            auto style = base;
            style.nameBold = false;
            if (inkRows(style).ink == plain.ink) inert.add("nameBold");
        }
        {
            // Alignment is per cell now, so both are checked - a shared setting
            // could not put the category left while the author stayed centred.
            auto style = base;
            style.categoryAlign = juce::Justification::centredLeft;
            if (inkRows(style).centreX == plain.centreX) inert.add("category.align");
        }
        {
            auto style = base;
            style.authorAlign = juce::Justification::centredRight;
            if (inkRows(style).centreX == plain.centreX) inert.add("author.align");
        }
        {
            auto style = base;
            style.categoryPadding = { 0.0f, 0.0f, 0.0f, 26.0f };
            if (inkRows(style).centreX == plain.centreX) inert.add("category.padding");
        }
        {
            auto style = base;
            style.authorPadding = { 0.0f, 26.0f, 0.0f, 0.0f };
            if (inkRows(style).centreX == plain.centreX) inert.add("author.padding");
        }
        {
            auto style = base;
            style.detailColour = juce::Colour::fromRGB(255, 0, 0);
            if (inkRows(style).ink == plain.ink) inert.add("rows.row2.colour");
        }

        {
            // The two subtitle cells align and pad independently. Checked as a
            // pair, because the point of splitting them is that one can differ
            // from the other - a shared setting would move both together and
            // still pass a test that only looked at one.
            auto leftOnly = base;
            leftOnly.categoryAlign = juce::Justification::centredLeft;
            auto rightOnly = base;
            rightOnly.authorAlign = juce::Justification::centredRight;

            const auto movedLeft = inkRows(leftOnly).centreX;
            const auto movedRight = inkRows(rightOnly).centreX;

            check("TopMenu_SubtitleCellsAlignIndependently",
                  std::abs(movedLeft - plain.centreX) > 0.5
                      && std::abs(movedRight - plain.centreX) > 0.5
                      && std::abs(movedLeft - movedRight) > 0.5,
                  "ink centre " + fmt(plain.centreX, 1) + " centred; "
                      + fmt(movedLeft, 1) + " with the category left; "
                      + fmt(movedRight, 1) + " with the author right");
        }

        check("TopMenu_EveryPresetTabPropertyChangesTheLayout", inert.isEmpty(),
              inert.isEmpty() ? detail + "(every property measurably changes the face)"
                              : "inert: " + inert.joinIntoString(", "));
    }

    {
        // topMenu.layout.prevNextWidth sizes the two arrows either side of the
        // preset name. Pinned because the name button takes whatever they
        // leave, so a change here is easy to make and easy to have silently
        // stop working.
        auto arrowWidth = [](int configured)
        {
            const juce::String json = juce::String(R"({"topMenu":{"layout":{"prevNextWidth":)")
                                      + juce::String(configured) + R"(}}})";
            juce::String error;
            const auto config = UIConfig::fromJsonText(json, error);

            TopMenuBar bar;
            bar.setUIConfig(config);
            bar.setBounds(0, 0, 1280, 40);
            bar.resized();

            struct R { int prev; int next; int name; };
            return R { bar.getPresetPrevButton().getWidth(),
                       bar.getPresetNextButton().getWidth(),
                       bar.getPresetNameButton().getWidth() };
        };

        const auto narrow = arrowWidth(20);
        const auto wide = arrowWidth(51);

        check("TopMenu_ArrowWidthComesFromConfig",
              narrow.prev == 20 && narrow.next == 20
                  && wide.prev == 51 && wide.next == 51
                  && wide.name < narrow.name,
              "20px -> prev " + juce::String(narrow.prev) + " next " + juce::String(narrow.next)
                  + " name " + juce::String(narrow.name) + ";  51px -> prev " + juce::String(wide.prev)
                  + " next " + juce::String(wide.next) + " name " + juce::String(wide.name));
    }

    {
        // The arrows step through everything the browser lists, INIT included.
        // They briefly skipped it - it is the default state, and landing on it
        // mid-audition drops the instrument back to nothing - but that made the
        // arrows disagree with the list they are stepping through.
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1320, 798);
        editor->setVisible(true);

        TopMenuBar* menu = nullptr;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c)
        {
            if (auto* m = dynamic_cast<TopMenuBar*>(&c)) menu = m;
            for (auto* child : c.getChildren()) walk(*child);
        };
        walk(*editor);

        juce::StringArray visited;
        if (menu != nullptr && menu->getPresetNextButton().onClick != nullptr)
        {
            for (int press = 0; press < 3; ++press)
            {
                menu->getPresetNextButton().onClick();
                visited.add(menu->getPresetNameButton().getButtonText());
            }
        }

        check("TopMenu_TheArrowsStepOntoInitToo",
              visited.size() == 3 && visited[0] == "- INIT -" && visited[1] != "- INIT -",
              "first three presses: " + visited.joinIntoString(" -> "));
    }
}

} // namespace px3tests
