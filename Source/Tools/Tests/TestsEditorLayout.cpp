#include "TestSupport.h"

// testEditorLayout
//
// The gaps a refactor of PluginEditor.cpp would fall through.
//
// The suite reaches deep into the editor's BEHAVIOUR - macro assignment, MIDI
// learn, the envelope cards, panel visibility - but barely touches the three
// things a large refactor of that file is most likely to break: where resized()
// puts everything, whether the constructor leaves a coherent first frame, and
// whether each panel still paints. These are written to be run before and after
// such a change.

namespace px3tests
{

void testEditorLayout()
{
    suite("EDITOR LAYOUT");

    // ---- the bar, the panel and the keyboard never overlap -----------------
    //
    // At one size this is nearly free to satisfy by accident. Across a range of
    // window sizes it is a real constraint, and it is the one resized() is
    // most likely to get wrong: a fixed height that stops fitting, or a row
    // that grows into its neighbour.
    {
        const int sizes[][2] = { { 1100, 700 }, { 1280, 800 }, { 1400, 900 },
                                 { 1600, 1000 }, { 1920, 1080 } };

        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        juce::StringArray faults;
        auto checked = 0;

        if (editor != nullptr)
        {
            for (const auto& size : sizes)
            {
                for (int section = 0; section <= 6; ++section)
                {
                    editor->setSize(size[0], size[1]);
                    editor->debugSelectSection(section);
                    editor->resized();
                    ++checked;

                    const auto where = juce::String(size[0]) + "x" + juce::String(size[1])
                                       + " section " + juce::String(section);

                    const auto editorBounds = editor->getLocalBounds();
                    const auto panel = editor->debugPanelViewportArea();
                    const auto keys = editor->debugKeyboardBounds();
                    const auto bar = editor->debugTopMenuBar() != nullptr
                                         ? editor->debugTopMenuBar()->getBounds()
                                         : juce::Rectangle<int>();

                    if (panel.isEmpty() || keys.isEmpty() || bar.isEmpty())
                    {
                        faults.add(where + ": an area came out empty");
                        continue;
                    }

                    for (const auto& pair : { std::pair { bar, panel },
                                              std::pair { panel, keys },
                                              std::pair { bar, keys } })
                    {
                        if (pair.first.intersects(pair.second))
                        {
                            faults.add(where + ": two of the three rows overlap");
                            break;
                        }
                    }

                    if (! editorBounds.contains(panel) || ! editorBounds.contains(keys)
                        || ! editorBounds.contains(bar))
                    {
                        faults.add(where + ": a row is outside the window");
                    }
                }
            }
        }

        check("EditorLayout_TheBarPanelAndKeyboardNeverOverlapAtAnySize",
              editor != nullptr && checked > 0 && faults.isEmpty(),
              faults.isEmpty()
                  ? juce::String(checked) + " size and section combinations, all clean"
                  : faults.joinIntoString("; "));
    }

    // ---- the macro strip is where it should be, at every size --------------
    {
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        juce::StringArray faults;
        if (editor != nullptr)
        {
            for (const auto width : { 1100, 1400, 1920 })
            {
                editor->setSize(width, 900);

                for (int section = 0; section <= 6; ++section)
                {
                    editor->debugSelectSection(section);
                    editor->resized();

                    const auto strip = editor->debugMacroStripArea();
                    const auto panel = editor->debugPanelViewportArea();
                    const auto onSettings = section == 6;

                    if (onSettings && ! strip.isEmpty())
                    {
                        faults.add("SETTINGS at " + juce::String(width) + " still reserves a strip");
                    }
                    if (! onSettings && (strip.isEmpty() || strip.getRight() > panel.getX()))
                    {
                        faults.add("section " + juce::String(section) + " at "
                                   + juce::String(width) + ": the strip is missing or overlaps");
                    }
                }
            }
        }

        check("EditorLayout_TheMacroStripSitsLeftOfThePanelExceptOnSettings",
              editor != nullptr && faults.isEmpty(),
              faults.isEmpty() ? "the strip is placed correctly at every width and section"
                               : faults.joinIntoString("; "));
    }

    // ---- the first frame is coherent ---------------------------------------
    //
    // The constructor is the largest thing in the editor and the part a split
    // is most likely to reorder. What it has to leave behind is a window whose
    // widgets already agree with the processor - not one that corrects itself
    // on the first timer tick, because by then a wrong first frame has been
    // seen.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        // Deliberately not the defaults: a first frame built from initial
        // values would look right on a default patch and wrong on every other.
        setParam(processor, "ampAttack", 1.750f);
        setParam(processor, "filter1Cutoff", 640.0f);
        setChoice(processor, "analogProfile", 3);
        processor.setEnvelopeMode(1, px3::BreakpointEnvelope::Mode::breakpoint);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            // Read BEFORE any timer tick: this is the first frame.
            editor->debugSelectSection(6);
            editor->resized();
            auto* settings = editor->debugSettingsPanel();

            const auto profileShown = settings != nullptr
                                      && settings->debugAnalogProfileBox().getSelectedId() == 4;

            EnvelopeComponent* env1 = nullptr;
            std::function<void(juce::Component&)> walk = [&](juce::Component& parent)
            {
                for (auto* child : parent.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* e = dynamic_cast<EnvelopeComponent*>(child))
                    {
                        if (env1 == nullptr && ! e->isAdsrOnly()) { env1 = e; }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            check("EditorLayout_TheFirstFrameAlreadyAgreesWithTheProcessor",
                  profileShown,
                  profileShown ? "the settings page opens showing the profile the processor holds"
                               : "the first frame showed a stale value");

            check("EditorLayout_TheFirstFrameKnowsWhichEnvelopeModeIsActive",
                  env1 != nullptr
                      && env1->debugModeBox().getSelectedId() == 2,
                  env1 == nullptr ? "no mod envelope card found"
                                  : "ENV 1's TYPE menu opens on item "
                                        + juce::String(env1->debugModeBox().getSelectedId()));
        }
    }

    // ---- every panel paints, deterministically, and paints something else --
    //
    // Not a golden image: an intentional visual change would break that on
    // every commit. What this pins is that each panel draws SOMETHING, draws
    // the same thing twice, and draws something different from its neighbours -
    // which is what catches a panel that stopped being drawn, or a switch that
    // draws the wrong one. The hashes are printed so a refactor can be diffed
    // against a run from before it.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);

            const auto hashOfPanel = [&](int section)
            {
                editor->debugSelectSection(section);
                editor->resized();

                const auto area = editor->debugPanelViewportArea();
                const auto image = editor->createComponentSnapshot(area);

                juce::uint64 hash = 1469598103934665603ull;
                auto lit = 0;
                for (int y = 0; y < image.getHeight(); y += 3)
                {
                    for (int x = 0; x < image.getWidth(); x += 3)
                    {
                        const auto argb = image.getPixelAt(x, y).getARGB();
                        hash = (hash ^ argb) * 1099511628211ull;
                        if (image.getPixelAt(x, y).getBrightness() > 0.02f) { ++lit; }
                    }
                }
                return std::pair<juce::uint64, int> { hash, lit };
            };

            juce::StringArray digests;
            std::vector<juce::uint64> hashes;
            auto allDrawSomething = true;
            auto allDeterministic = true;

            for (int section = 0; section <= 6; ++section)
            {
                const auto first = hashOfPanel(section);
                const auto again = hashOfPanel(section);

                allDeterministic = allDeterministic && first.first == again.first;
                allDrawSomething = allDrawSomething && first.second > 200;
                hashes.push_back(first.first);
                digests.add(juce::String::toHexString(static_cast<juce::int64>(first.first)).paddedLeft('0', 16));
            }

            std::set<juce::uint64> distinct(hashes.begin(), hashes.end());

            check("EditorPaint_EveryPanelDrawsSomethingAndDrawsItTheSameTwice",
                  allDrawSomething && allDeterministic,
                  juce::String(allDrawSomething ? "all seven have content" : "a panel drew nothing")
                      + ", " + (allDeterministic ? "and each is identical across two renders"
                                                 : "and one differed between renders"));

            // Distinctness catches a panel that vanished, and one that started
            // drawing the same as another. It does NOT catch two panels being
            // SWAPPED - the seven pictures are still seven different pictures.
            // So this asks a different question: does the section show the
            // panel it names? Checked by type rather than by pixels, so an
            // intentional visual change does not break it.
            {
                const auto visiblePanelFor = [&](int section)
                {
                    editor->debugSelectSection(section);
                    editor->resized();

                    juce::String found;
                    std::function<void(juce::Component&, bool)> walk =
                        [&](juce::Component& parent, bool parentShown)
                    {
                        for (auto* child : parent.getChildren())
                        {
                            if (child == nullptr) { continue; }
                            const auto shown = parentShown && child->isVisible();

                            if (shown)
                            {
                                if (dynamic_cast<OscPanel*>(child))      { found = "OSC"; }
                                else if (dynamic_cast<ModPanel*>(child)) { found = "MOD"; }
                                else if (dynamic_cast<AmpPanel*>(child)) { found = "AMP"; }
                                else if (dynamic_cast<FltPanel*>(child)) { found = "FLT"; }
                                else if (dynamic_cast<FxPanel*>(child))  { found = "FX"; }
                                else if (dynamic_cast<MixPanel*>(child)) { found = "MIX"; }
                                else if (dynamic_cast<SettingsPanel*>(child)) { found = "SETTINGS"; }
                            }

                            walk(*child, shown);
                        }
                    };
                    walk(*editor, true);
                    return found;
                };

                const juce::StringArray expected { "OSC", "MOD", "AMP", "FLT", "FX", "MIX",
                                                   "SETTINGS" };
                juce::StringArray actual;
                for (int section = 0; section <= 6; ++section)
                {
                    actual.add(visiblePanelFor(section));
                }

                check("EditorPaint_EachSectionShowsThePanelItNames",
                      actual == expected,
                      "the seven sections show " + actual.joinIntoString(", "));
            }

            check("EditorPaint_NoTwoPanelsDrawTheSamePicture",
                  distinct.size() == hashes.size(),
                  juce::String(static_cast<int>(distinct.size())) + " distinct of "
                      + juce::String(static_cast<int>(hashes.size())) + ": "
                      + digests.joinIntoString(" "));
        }
    }
}

} // namespace px3tests
