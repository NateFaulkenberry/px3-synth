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
            if (text.contains("products/") || text.contains("PluginProcessor.h"))
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
}

} // namespace px3tests
