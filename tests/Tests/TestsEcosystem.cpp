#include "TestSupport.h"

#include "PX3Version.h"
#include "ProductRegistry.h"
#include "UpdateService.h"

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
