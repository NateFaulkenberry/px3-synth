#include "TestSupport.h"

#include "PX3Version.h"
#include "ProductRegistry.h"
#include "UpdateService.h"
#include "PluginProcessor.h"

// testEcosystem
//
// The one architectural rule this repository now rests on, checked rather than
// written down and hoped for.
//
// shared/ may not depend on products/. Everything else - the source lists, the
// include paths, px3_add_product - is arrangement; this is the invariant that
// makes a second product possible at all. It is exactly the kind of rule that
// decays silently, because breaking it costs nothing at the moment it happens
// and everything the first time somebody tries to build a product without the
// Synth.

namespace px3tests
{

void testEcosystem()
{
    suite("ECOSYSTEM");

    const auto root = juce::File::getCurrentWorkingDirectory();
    const auto shared = root.getChildFile("shared");

    if (! shared.isDirectory())
    {
        check("Ecosystem_TheSharedTreeExists", false,
              "no shared/ directory under " + root.getFullPathName());
        return;
    }

    const auto sources = shared.findChildFiles(juce::File::findFiles, true, "*.h;*.cpp");

    // ---- shared code may not reach into a product --------------------------
    {
        juce::StringArray offenders;

        for (const auto& file : sources)
        {
            const auto text = file.loadFileAsString();

            // Either spelling of the same mistake: a path into products/, or
            // an include of a header that only a product defines.
            //
            // Matched WITH the quotes. Without them "PluginProcessor.h" is a
            // substring of "FxPluginProcessor.h", and the shared FX scaffold
            // was reported as reaching into a product when it does no such
            // thing - a false positive from a rule that is meant to be exact.
            if (text.contains("products/") || text.contains("\"PluginProcessor.h\""))
            {
                offenders.add(file.getRelativePathFrom(root));
            }
        }

        check("Ecosystem_SharedCodeDoesNotDependOnAProduct",
              offenders.isEmpty(),
              offenders.isEmpty()
                  ? juce::String(sources.size()) + " shared files, none reaching into products/"
                  : "reaching into a product: " + offenders.joinIntoString(", "));
    }

    // ---- the FX DSP is where a second product can find it -------------------
    //
    // Named individually rather than counted, because "13 files exist" would
    // still pass if the wrong 13 were there.
    {
        juce::StringArray missing;

        for (const auto& fx : { "Mood/Mood.cpp", "Delay/Delay.cpp", "Reverb/Reverb.cpp",
                                "Doom/Doom.cpp", "Lucy/Lucy.cpp", "Chorus/Chorus.cpp",
                                "StereoSpread/StereoSpread.cpp", "Vibe/Vibe.cpp",
                                "Filter/VoiceFilter.cpp", "Analog/AnalogEngine.cpp" })
        {
            if (! shared.getChildFile("DSP").getChildFile(fx).existsAsFile())
            {
                missing.add(fx);
            }
        }

        check("Ecosystem_EveryReusableEffectIsInTheSharedTree",
              missing.isEmpty(),
              missing.isEmpty() ? juce::String("all ten reusable effects are under shared/DSP")
                                : "not shared: " + missing.joinIntoString(", "));
    }

    // ---- the product tree holds what only the Synth needs -------------------
    {
        const auto product = root.getChildFile("products/PX3Synth");
        const auto hasProcessor = product.getChildFile("DSP/PluginProcessor.cpp").existsAsFile();
        const auto hasEditor = product.getChildFile("UI/PluginEditor.cpp").existsAsFile();

        check("Ecosystem_TheSynthIsAProductRatherThanTheRepository",
              hasProcessor && hasEditor,
              juce::String("PluginProcessor ") + (hasProcessor ? "and " : "or ")
                  + "PluginEditor "
                  + ((hasProcessor && hasEditor) ? "both live under products/PX3Synth"
                                                 : "NOT under products/PX3Synth"));
    }

    {
        PX3SynthAudioProcessor processor;
        const auto& catalog = processor.getParameterCatalog();
        const auto* coarse = catalog.find("voice.osc1.tuning.octave");
        const auto groups = processor.getParameterTree().getSubgroups(false);
        const auto parameterState = catalog.createStateTree();
        const auto coarseState = catalog.findStateEntry(parameterState, "voice.osc1.tuning.octave");
        std::vector<px3::synth::ParameterCatalog::StateValue> stateValues;
        juce::String stateError;
        const auto stateValid = catalog.readStateValues(parameterState, stateValues, stateError);
        auto hasVoiceGroup = false;
        auto hasModulationGroup = false;
        auto hasEffectsGroup = false;
        auto hasMixerGroup = false;
        for (const auto* group : groups)
        {
            if (group == nullptr) { continue; }
            hasVoiceGroup = hasVoiceGroup || group->getID() == "voice";
            hasModulationGroup = hasModulationGroup || group->getID() == "modulation";
            hasEffectsGroup = hasEffectsGroup || group->getID() == "effects";
            hasMixerGroup = hasMixerGroup || group->getID() == "mixer";
        }
        const auto allRegistered = catalog.entries().size()
                                == static_cast<std::size_t>(processor.getParameters().size());
        auto definitionsMatch = allRegistered;
        for (const auto& entry : catalog.entries())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(entry.parameter);
            definitionsMatch = definitionsMatch && entry.definition != nullptr && ranged != nullptr;
            if (entry.definition == nullptr || ranged == nullptr) { continue; }
            const auto& definition = *entry.definition;
            definitionsMatch = definitionsMatch && definition.id.getParamID() == entry.id
                && definition.name == entry.name
                && definition.range.start == ranged->getNormalisableRange().start
                && definition.range.end == ranged->getNormalisableRange().end
                && std::abs(definition.range.convertTo0to1(definition.defaultValue)
                            - ranged->getDefaultValue()) < 1.0e-5f;
        }
        check("ParameterCatalog_AllHostParametersComeFromTypedDefinitions", definitionsMatch);
        juce::StringArray nonCanonicalIds;
        for (const auto& entry : catalog.entries())
        {
            const auto rootId = entry.id.upToFirstOccurrenceOf(".", false, false);
            if (entry.id != entry.id.toLowerCase() || ! entry.id.containsChar('.')
                || (rootId != "voice" && rootId != "mod" && rootId != "fx" && rootId != "mix"
                    && rootId != "performance" && rootId != "global"))
            {
                nonCanonicalIds.add(entry.id);
            }
        }
        check("ParameterCatalog_AllHostIdsAreHierarchicalAndCanonical", nonCanonicalIds.isEmpty(),
              nonCanonicalIds.joinIntoString(", "));
          check("ParameterCatalog_MixerInsertsBelongToTheirOwningBuses",
              catalog.find("mix.dry.insert.eq.frequency.1")->groupPath == "MIXER / DRY BUS / EQ"
                && catalog.find("mix.fx.insert.comp.input")->groupPath == "MIXER / FX RETURN / COMPRESSOR"
                && catalog.find("mix.master.level")->groupPath == "MIXER / MASTER");

        check("ParameterCatalog_IndexesEveryHostParameterAndBuildsModuleGroups",
              allRegistered && coarse != nullptr && coarse->parameter != nullptr
                  && coarse->groupPath == "VOICE / OSC 1"
                  && hasVoiceGroup && hasModulationGroup && hasEffectsGroup && hasMixerGroup,
              juce::String(static_cast<int>(catalog.entries().size())) + " catalog entries / "
                  + juce::String(processor.getParameters().size()) + " host parameters");

        const auto* ampAttack = catalog.find("voice.amp.attack");
        const auto* filterCutoff = catalog.find("voice.filter1.cutoff");
        const auto* filterRouting = catalog.find("voice.filters.routing.mode");
        check("ParameterCatalog_AmpAndFiltersUseCanonicalIdsWithoutAliases",
              ampAttack != nullptr && ampAttack->groupPath == "VOICE / AMP ENVELOPE"
                  && catalog.find("voice.amp.enabled") != nullptr
                  && filterCutoff != nullptr && filterCutoff->groupPath == "VOICE / FILTER 1"
                  && filterRouting != nullptr && filterRouting->groupPath == "VOICE / FILTERS / ROUTING"
                  && catalog.find("ampAttack") == nullptr && catalog.find("ampEnvEnabled") == nullptr
                  && catalog.find("filter1Cutoff") == nullptr && catalog.find("filterRouting") == nullptr);

        check("ParameterCatalog_MacroIdsAndLevelRoutesHaveNoLegacyAliases",
              catalog.find("mod.macro1.value") != nullptr && catalog.find("macro1") == nullptr
                  && ! processor.setLfoAssignmentByParameterId(0, "osc1Level", false)
                  && ! processor.setEnvelopeAssignmentByParameterId(0, "subOscLevel", false)
                  && processor.setLfoAssignmentByParameterId(0, "mix.osc1.level", false));
        processor.setLfoAssignmentIndex(0, 0, false);

        const auto* lfoSource = catalog.find("mod.lfo1.frequency");
        const auto* envSource = catalog.find("mod.env3.amount");
        check("ParameterCatalog_DeclaresModulationCapabilitiesAndSourceControls",
              lfoSource != nullptr && lfoSource->modulationDestination && lfoSource->sourceControl
                  && envSource != nullptr && ! envSource->modulationDestination
                  && catalog.find("voice.filter1.cutoff")->modulationDestination
                  && ! catalog.find("voice.filter1.enabled")->modulationDestination);
        check("ParameterCatalog_ModulationSourcesUseUniformCanonicalIds",
              lfoSource != nullptr && lfoSource->groupPath == "MODULATION / LFO 1"
                  && envSource != nullptr && envSource->groupPath == "MODULATION / ENV 3"
                  && catalog.find("lfoFrequency") == nullptr && catalog.find("envAmount") == nullptr);

        const auto* chorus = catalog.find("fx.chorus.rate");
        const auto* granular = catalog.find("fx.delay.granular.mode");
        check("ParameterCatalog_EffectsUseCanonicalModuleIdsWithoutAliases",
              chorus != nullptr && chorus->groupPath == "EFFECTS / CHORUS"
                  && granular != nullptr && granular->groupPath == "EFFECTS / DELAY"
                  && catalog.find("chorusRate") == nullptr && catalog.find("granularMode") == nullptr);

        check("ParameterCatalog_SerializesCompleteGroupedNormalizedState",
              stateValid && stateValues.size() == catalog.entries().size()
                  && coarseState.isValid() && coarseState.getParent().getProperty("id").toString() == "osc1"
                  && std::abs(static_cast<float>(coarseState.getProperty("value"))
                              - coarse->parameter->getValue()) < 1.0e-6f,
              stateError);
          auto mismatchedSchema = parameterState.createCopy();
          mismatchedSchema.setProperty("schemaFingerprint", "different-choice-contract", nullptr);
          std::vector<px3::synth::ParameterCatalog::StateValue> mismatchValues;
          juce::String mismatchError;
          check("ParameterCatalog_RejectsChangedChoiceOrRangeContractsBeforeApply",
              ! catalog.readStateValues(mismatchedSchema, mismatchValues, mismatchError) && mismatchValues.empty());

        const auto rejectsMalformed = [&catalog, &parameterState](const juce::String& id,
                                                                   juce::var badValue)
        {
            auto malformed = parameterState.createCopy();
            auto entry = catalog.findStateEntry(malformed, id);
            if (! entry.isValid()) { return false; }
            entry.setProperty("value", badValue, nullptr);
            std::vector<px3::synth::ParameterCatalog::StateValue> parsed;
            juce::String error;
            return ! catalog.readStateValues(malformed, parsed, error) && parsed.empty();
        };
        check("ParameterCatalog_RejectsNaNAndInfinity",
              rejectsMalformed("voice.osc1.tuning.octave", std::numeric_limits<double>::quiet_NaN())
                  && rejectsMalformed("voice.osc1.tuning.cents", std::numeric_limits<double>::infinity()));
        check("ParameterCatalog_RejectsOutOfRangeAndWrongType",
              rejectsMalformed("voice.osc1.tuning.octave", -0.1)
                  && rejectsMalformed("voice.osc1.tuning.cents", "not a number"));

        auto unknownState = parameterState.createCopy();
        auto unknownGroup = juce::ValueTree("GROUP");
        unknownGroup.setProperty("id", "rogue", nullptr);
        unknownGroup.setProperty("name", "ROGUE", nullptr);
        auto unknownParameter = juce::ValueTree("PARAMETER");
        unknownParameter.setProperty("id", "unknown.parameter", nullptr);
        unknownParameter.setProperty("value", 0.5, nullptr);
        unknownGroup.addChild(unknownParameter, -1, nullptr);
        unknownState.addChild(unknownGroup, -1, nullptr);
        std::vector<px3::synth::ParameterCatalog::StateValue> unknownValues;
        check("ParameterCatalog_RejectsUnknownGroupedParameter",
              ! catalog.readStateValues(unknownState, unknownValues, stateError)
                  && unknownValues.empty(),
              stateError);

        PX3SynthAudioProcessor source;
        setParam(source, "mod.env1.attack", 12.5f);
        const auto stateTree = source.createParameterStateTree();
        PX3SynthAudioProcessor treeRestored;
        const auto treeApplied = treeRestored.applyParameterStateTree(stateTree, &stateError);
        check("ParameterCatalog_GroupedStateAppliesWithoutXmlRoundTrip",
              treeApplied && std::abs(getParamValue(treeRestored, "mod.env1.attack") - 12.5f) < 0.01f,
              stateError);

        juce::MemoryBlock stateBytes;
        source.getStateInformation(stateBytes);
        const auto stateXml = juce::AudioProcessor::getXmlFromBinary(
            stateBytes.getData(), static_cast<int>(stateBytes.getSize()));
        const auto parsedState = stateXml != nullptr ? juce::ValueTree::fromXml(*stateXml) : juce::ValueTree();
        PX3SynthAudioProcessor xmlRestored;
        juce::String xmlError;
        const auto xmlApplied = parsedState.isValid() && xmlRestored.applyParameterStateTree(parsedState, &xmlError);
        check("ParameterCatalog_GroupedStateSurvivesXmlParseAndDirectApply",
              xmlApplied && std::abs(getParamValue(xmlRestored, "mod.env1.attack") - 12.5f) < 0.01f,
              parsedState.getType().toString() + (xmlError.isEmpty() ? juce::String() : ": " + xmlError));
        PX3SynthAudioProcessor binaryRestored;
        binaryRestored.setStateInformation(stateBytes.getData(), static_cast<int>(stateBytes.getSize()));
        check("ParameterCatalog_GroupedStateSurvivesHostBinaryRoundTrip",
              std::abs(getParamValue(binaryRestored, "mod.env1.attack") - 12.5f) < 0.01f);
    }

    // ---- one source of truth for the version --------------------------------
    {
        const auto cmake = root.getChildFile("CMakeLists.txt").loadFileAsString();
        const auto declared = cmake.fromFirstOccurrenceOf("set(PX3_VERSION \"", false, false)
                                   .upToFirstOccurrenceOf("\"", false, false);

        check("Ecosystem_TheBuildAndTheBinaryAgreeOnTheVersion",
              declared.isNotEmpty() && declared == px3::version::string(),
              "CMakeLists declares " + declared + ", the binary reports "
                  + px3::version::string());
    }

    // ---- a shared header's implementation must be shared too ---------------
    //
    // StftEngine.h moved to shared while StftEngine.cpp stayed in the product
    // tree. The Synth kept building - it compiles both - and nothing showed
    // until the SECOND product needed it and failed to link. A split like that
    // is invisible from either side on its own.
    {
        juce::StringArray split;

        for (const auto& header : sources)
        {
            if (! header.hasFileExtension("h")) { continue; }
            if (header.withFileExtension("cpp").existsAsFile()) { continue; }

            const auto stray = root.getChildFile("products")
                                   .findChildFiles(juce::File::findFiles, true,
                                                   header.getFileNameWithoutExtension() + ".cpp");
            if (! stray.isEmpty())
            {
                split.add(header.getFileName() + " (implementation in "
                          + stray[0].getParentDirectory().getFileName() + ")");
            }
        }

        check("Ecosystem_ASharedHeadersImplementationIsSharedToo",
              split.isEmpty(),
              split.isEmpty() ? juce::String("no shared header has its implementation in a product")
                              : "split across the boundary: " + split.joinIntoString(", "));
    }

    // ---- no two products may claim the same plug-in code -------------------
    //
    // A four-character code is how a DAW tells one plug-in from another. Two
    // products sharing one is how a host loads the wrong plug-in, and it is
    // invisible until it happens on somebody else's machine - so it is checked
    // here rather than left to whoever adds the next product to remember.
    {
        const auto cmake = root.getChildFile("CMakeLists.txt").loadFileAsString();

        juce::StringArray codes;
        juce::StringArray duplicates;
        auto search = cmake;

        while (search.contains("PLUGIN_CODE"))
        {
            search = search.fromFirstOccurrenceOf("PLUGIN_CODE", false, false);
            const auto code = search.trimStart().upToFirstOccurrenceOf("\n", false, false).trim();
            if (code.isEmpty()) { continue; }

            if (codes.contains(code)) { duplicates.addIfNotAlreadyThere(code); }
            codes.add(code);
        }

        check("Ecosystem_NoTwoProductsShareAPluginCode",
              codes.size() >= 2 && duplicates.isEmpty(),
              juce::String(codes.size()) + " plug-in codes declared ("
                  + codes.joinIntoString(", ") + ")"
                  + (duplicates.isEmpty() ? "" : "; DUPLICATED: " + duplicates.joinIntoString(", ")));
    }

    // ---- and no two share a bundle identifier -------------------------------
    {
        const auto cmake = root.getChildFile("CMakeLists.txt").loadFileAsString();

        juce::StringArray ids;
        juce::StringArray duplicates;
        auto search = cmake;

        while (search.contains("BUNDLE_ID"))
        {
            search = search.fromFirstOccurrenceOf("BUNDLE_ID", false, false);
            const auto id = search.fromFirstOccurrenceOf("\"", false, false)
                                  .upToFirstOccurrenceOf("\"", false, false).trim();
            if (id.isEmpty()) { continue; }

            if (ids.contains(id)) { duplicates.addIfNotAlreadyThere(id); }
            ids.add(id);
        }

        check("Ecosystem_NoTwoProductsShareABundleIdentifier",
              ids.size() >= 2 && duplicates.isEmpty(),
              ids.joinIntoString(", ")
                  + (duplicates.isEmpty() ? "" : "; DUPLICATED: " + duplicates.joinIntoString(", ")));
    }

    // ---- the product registry carries a product's whole identity ------------
    {
        px3::update::installDefaultConfiguration();
        const auto synth = px3::update::ProductRegistry::getInstance()
                               .definition(px3::update::ProductRegistry::kSynthProductId);

        check("Ecosystem_TheRegistryKnowsAProductsIdentityNotJustItsName",
              synth.productId == "px3-synth"
                  && synth.bundleId == "com.px3.px3synth"
                  && synth.hasStandalone
                  && synth.installerComponentId.isNotEmpty(),
              synth.productId + " -> bundle " + synth.bundleId + ", component "
                  + synth.installerComponentId
                  + ", standalone " + (synth.hasStandalone ? "yes" : "no"));
    }

    // ---- every product the build declares is in the registry ----------------
    //
    // The registry is what the updater and the installer read. A product that
    // builds but is not registered ships and is then invisible to both - it
    // gets no updates and no installer component, and nothing says so.
    // Checked against the BUILD rather than a second list, so the two cannot
    // drift.
    {
        const auto cmake = root.getChildFile("CMakeLists.txt").loadFileAsString();
        auto& registry = px3::update::ProductRegistry::getInstance();

        juce::StringArray declared, unregistered;
        auto search = cmake;

        while (search.contains("BUNDLE_ID"))
        {
            search = search.fromFirstOccurrenceOf("BUNDLE_ID", false, false);
            const auto bundleId = search.fromFirstOccurrenceOf("\"", false, false)
                                        .upToFirstOccurrenceOf("\"", false, false).trim();
            if (bundleId.isEmpty()) { continue; }

            declared.add(bundleId);

            auto found = false;
            for (const auto& id : registry.productIds())
            {
                if (registry.definition(id).bundleId == bundleId) { found = true; break; }
            }
            if (! found) { unregistered.add(bundleId); }
        }

        check("Ecosystem_EveryProductTheBuildDeclaresIsRegistered",
              declared.size() >= 8 && unregistered.isEmpty(),
              juce::String(declared.size()) + " products in the build, "
                  + juce::String(static_cast<int>(registry.productIds().size()))
                  + " registered"
                  + (unregistered.isEmpty() ? "" : "; NOT REGISTERED: "
                                                       + unregistered.joinIntoString(", ")));
    }

    // ---- and the effects say they have no standalone ------------------------
    {
        auto& registry = px3::update::ProductRegistry::getInstance();

        juce::StringArray wrong;
        for (const auto& id : registry.productIds())
        {
            const auto product = registry.definition(id);
            const auto shouldHaveStandalone = (id == "px3-synth");
            if (product.hasStandalone != shouldHaveStandalone) { wrong.add(id); }
        }

        // Vibe is not a product: it has no audio interface to wrap. Its
        // absence here is the assessment's conclusion, in code.
        const auto vibeAbsent = ! registry.isRegistered("px3-vibe");

        check("Ecosystem_OnlyTheSynthHasAStandaloneAndVibeIsNotAProduct",
              wrong.isEmpty() && vibeAbsent,
              juce::String(static_cast<int>(registry.productIds().size()))
                  + " products; only the Synth has a standalone application"
                  + (vibeAbsent ? "; Vibe correctly absent" : "; VIBE REGISTERED")
                  + (wrong.isEmpty() ? "" : "; wrong: " + wrong.joinIntoString(", ")));
    }
}

} // namespace px3tests
