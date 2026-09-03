#include "TestSupport.h"

// testUninstaller
//
// The uninstaller is shell, not C++, and it is the one piece of this project
// that deletes things. It gets tested by being RUN - against a fixture tree
// standing in for a real machine - rather than by reading its source and
// hoping. A test that greps a script for "rm -rf" proves nothing about which
// paths reach it.
//
// Every case here builds a fresh fixture: three products installed, one user
// with a preset library holding both factory and user presets and an imported
// wavetable. PX3_SCAN_ROOT points the script's system paths at that tree, so
// nothing here can touch the machine running the tests.

namespace px3tests
{
namespace
{

juce::File fixtureRoot()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("px3-uninstaller-tests");
}

// Three products, one standalone, one user with content worth losing.
juce::File buildFixture()
{
    auto root = fixtureRoot();
    root.deleteRecursively();

    const juce::StringArray products { "PX3 Synth", "PX3 Mood", "PX3 Doom" };
    for (const auto& product : products)
    {
        root.getChildFile("Library/Audio/Plug-Ins/Components")
            .getChildFile(product + ".component").createDirectory();
        root.getChildFile("Library/Audio/Plug-Ins/VST3")
            .getChildFile(product + ".vst3").createDirectory();
    }
    root.getChildFile("Applications/PX3 Synth.app").createDirectory();

    const auto data = root.getChildFile("Users/alice/Library/P(X3)");
    data.getChildFile("Presets/Factory").createDirectory();
    data.getChildFile("Presets/User").createDirectory();
    data.getChildFile("Settings").createDirectory();
    data.getChildFile("Updates").createDirectory();
    data.getChildFile("Wavetables").createDirectory();
    data.getChildFile("Presets/Factory/Init.px3preset").replaceWithText("factory");
    data.getChildFile("Presets/User/MyPatch.px3preset").replaceWithText("mine");
    data.getChildFile("Wavetables/imported.px3wt").replaceWithText("mine");
    data.getChildFile("settings.xml").replaceWithText("<x/>");

    return root;
}

juce::File repoFile(const juce::String& relative)
{
    return juce::File::getCurrentWorkingDirectory().getChildFile(relative);
}

// The manifest the shipped uninstaller carries, generated the same way the
// release build generates it.
juce::File generatedManifest()
{
    const auto manifest = fixtureRoot().getParentDirectory()
                              .getChildFile("px3-products-test.tsv");
    manifest.deleteFile();

    juce::ChildProcess generator;
    generator.start(juce::StringArray {
        "/bin/sh",
        repoFile("scripts/installer/generate-product-manifest.sh").getFullPathName(),
        repoFile("CMakeLists.txt").getFullPathName(),
        manifest.getFullPathName() });
    generator.waitForProcessToFinish(20000);
    return manifest;
}

struct RunResult
{
    juce::String output;
    juce::uint32 exitCode { 0 };
};

RunResult runScript(const juce::String& script, const juce::StringArray& env,
                    const juce::File& root, const juce::File& manifest)
{
    juce::StringArray assignments;
    assignments.add("PX3_SCAN_ROOT=" + root.getFullPathName().quoted());
    assignments.add("PX3_MANIFEST=" + manifest.getFullPathName().quoted());
    assignments.add("PX3_LOG_FILE="
                    + root.getChildFile("uninstall.log").getFullPathName().quoted());
    assignments.addArray(env);

    const auto command = assignments.joinIntoString(" ") + " /bin/sh "
                         + repoFile(script).getFullPathName().quoted();

    juce::ChildProcess process;
    RunResult result;
    if (! process.start(juce::StringArray { "/bin/sh", "-c", command }))
    {
        result.exitCode = 127;
        return result;
    }

    result.output = process.readAllProcessOutput();
    process.waitForProcessToFinish(120000);
    result.exitCode = process.getExitCode();
    return result;
}

RunResult uninstall(const juce::String& products, bool keepPresets,
                    const juce::File& root, const juce::File& manifest)
{
    return runScript("scripts/installer/px3-uninstall.sh",
                     { "PX3_PRODUCTS=" + products.quoted(),
                       juce::String("PX3_KEEP_PRESETS=") + (keepPresets ? "1" : "0") },
                     root, manifest);
}

bool installed(const juce::File& root, const juce::String& product)
{
    return root.getChildFile("Library/Audio/Plug-Ins/Components")
        .getChildFile(product + ".component").isDirectory();
}

} // namespace

void testUninstaller()
{
    suite("UNINSTALLER");

    const auto manifest = generatedManifest();

    if (! manifest.existsAsFile() || manifest.getSize() == 0)
    {
        check("Uninstall_TheManifestGeneratorProducesAManifest", false,
              "generate-product-manifest.sh wrote nothing to "
                  + manifest.getFullPathName());
        return;
    }

    // ---- the manifest describes the products the build declares ------------
    {
        const auto text = manifest.loadFileAsString();
        juce::StringArray missing;

        // Read the product names straight out of CMakeLists rather than from a
        // list here, so a product added to the build without reaching the
        // manifest is a failure rather than something this test also forgot.
        //
        // Only px3_add_product blocks count. The build declares other things
        // with a PRODUCT_NAME - PX3 Updater is a console helper, not a plug-in
        // - and they have no business in a list of installed products.
        const auto cmake = repoFile("CMakeLists.txt").loadFileAsString();
        auto inProduct = false;
        for (const auto& line : juce::StringArray::fromLines(cmake))
        {
            if (line.startsWith("px3_add_product(")) { inProduct = true; continue; }
            if (! inProduct) { continue; }
            if (line.trim().endsWith(")")) { inProduct = false; }
            if (! line.contains("PRODUCT_NAME")) { continue; }

            const auto name = line.fromFirstOccurrenceOf("\"", false, false)
                                  .upToFirstOccurrenceOf("\"", false, false);
            if (name.isNotEmpty() && ! text.contains(name)) { missing.add(name); }
        }

        check("Uninstall_TheManifestListsEveryProductTheBuildDeclares",
              missing.isEmpty(),
              missing.isEmpty()
                  ? juce::String(juce::StringArray::fromLines(text.trim()).size())
                        + " products in the manifest"
                  : "missing from the manifest: " + missing.joinIntoString(", "));
    }

    // ---- an empty selection removes NOTHING --------------------------------
    //
    // The single most important behaviour here. Every path the script deletes
    // is built from a variable, and the failure mode of an uninstaller whose
    // selection came out empty is that it removes everything instead.
    {
        const auto root = buildFixture();
        const auto result = uninstall("", false, root, manifest);

        check("Uninstall_RefusesWhenNothingIsSelected",
              result.exitCode != 0
                  && installed(root, "PX3 Synth")
                  && installed(root, "PX3 Mood")
                  && installed(root, "PX3 Doom")
                  && root.getChildFile("Users/alice/Library/P(X3)/Presets/User/MyPatch.px3preset")
                         .existsAsFile(),
              "exit " + juce::String(static_cast<int>(result.exitCode))
                  + " and all three products still installed");
    }

    // ---- one product goes, the others stay ---------------------------------
    {
        const auto root = buildFixture();
        uninstall("PX3 Mood", true, root, manifest);

        check("Uninstall_RemovesOnlyTheSelectedProduct",
              ! installed(root, "PX3 Mood")
                  && installed(root, "PX3 Synth")
                  && installed(root, "PX3 Doom")
                  && ! root.getChildFile("Library/Audio/Plug-Ins/VST3/PX3 Mood.vst3").isDirectory(),
              "Mood removed in both formats, Synth and Doom untouched");
    }

    // ---- shared data survives while any product remains --------------------
    //
    // Presets live in one directory shared by every PX3 product. Removing one
    // effect must not take the Synth's preset library with it.
    {
        const auto root = buildFixture();
        uninstall("PX3 Mood", false, root, manifest);

        const auto data = root.getChildFile("Users/alice/Library/P(X3)");

        check("Uninstall_KeepsSharedDataWhileAnotherProductRemains",
              data.getChildFile("Presets/User/MyPatch.px3preset").existsAsFile()
                  && data.getChildFile("Presets/Factory/Init.px3preset").existsAsFile()
                  && data.getChildFile("Wavetables/imported.px3wt").existsAsFile(),
              "the preset library is intact even though this run was told NOT "
              "to keep presets - because the Synth is still installed");
    }

    // ---- keeping presets keeps what the user made --------------------------
    {
        const auto root = buildFixture();
        uninstall("PX3 Synth|PX3 Mood|PX3 Doom", true, root, manifest);

        const auto data = root.getChildFile("Users/alice/Library/P(X3)");

        check("Uninstall_KeepingPresetsKeepsWhatTheUserMade",
              data.getChildFile("Presets/User/MyPatch.px3preset").existsAsFile()
                  && data.getChildFile("Wavetables/imported.px3wt").existsAsFile()
                  && ! data.getChildFile("Presets/Factory").isDirectory()
                  && ! data.getChildFile("settings.xml").existsAsFile()
                  && ! installed(root, "PX3 Synth")
                  && ! root.getChildFile("Applications/PX3 Synth.app").isDirectory(),
              "user presets and imported wavetables kept; factory presets and "
              "settings - the things a reinstall puts back - removed");
    }

    // ---- removing everything really does remove everything -----------------
    {
        const auto root = buildFixture();
        uninstall("PX3 Synth|PX3 Mood|PX3 Doom", false, root, manifest);

        const auto data = root.getChildFile("Users/alice/Library/P(X3)");

        check("Uninstall_RemovingEverythingRemovesEveryPreset",
              ! data.isDirectory()
                  && ! installed(root, "PX3 Synth")
                  && ! installed(root, "PX3 Mood")
                  && ! installed(root, "PX3 Doom"),
              "the whole shared data directory is gone, user presets included");
    }

    // ---- a product this build has never heard of ---------------------------
    //
    // The claim that one uninstaller serves the whole ecosystem "now and in
    // future" rests entirely on this. The scanner offers what is on disk, not
    // what the manifest lists, so a product released after this uninstaller
    // was built is still listed and still removable.
    {
        const auto root = buildFixture();
        root.getChildFile("Library/Audio/Plug-Ins/Components/PX3 Nebula.component")
            .createDirectory();
        root.getChildFile("Library/Audio/Plug-Ins/VST3/PX3 Nebula.vst3").createDirectory();

        const auto listed = runScript("scripts/installer/px3-list-products.sh", {},
                                      root, manifest);

        const auto offersIt = listed.output.contains("PX3 Nebula")
                              && listed.output.contains("unknown");

        uninstall("PX3 Nebula", true, root, manifest);

        check("Uninstall_OffersAndRemovesAProductTheManifestDoesNotKnow",
              offersIt
                  && ! installed(root, "PX3 Nebula")
                  && ! root.getChildFile("Library/Audio/Plug-Ins/VST3/PX3 Nebula.vst3").isDirectory()
                  && installed(root, "PX3 Synth"),
              offersIt ? "listed as unknown, removed in both formats, Synth untouched"
                       : "the scanner did not offer it at all");
    }

    // ---- the scanner reports what is installed, in both scopes -------------
    {
        const auto root = buildFixture();
        root.getChildFile("Users/alice/Library/Audio/Plug-Ins/VST3/PX3 Lucy.vst3")
            .createDirectory();

        const auto listed = runScript("scripts/installer/px3-list-products.sh", {},
                                      root, manifest);

        // A plug-in in a user's own folder is just as loaded by a DAW as one in
        // /Library, so an uninstaller that only looked system-wide would report
        // success while leaving it working.
        check("Uninstall_TheScannerSeesPerUserInstallationsToo",
              listed.output.contains("PX3 Lucy"),
              "scanner output: " + listed.output.replaceCharacters("\n", " ").trim());
    }

    fixtureRoot().deleteRecursively();
}

} // namespace px3tests
