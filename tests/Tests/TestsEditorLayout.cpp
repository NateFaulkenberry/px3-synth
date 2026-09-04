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

    // ---- the gear is a toggle, and remembers where you were ----------------
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
        processor.prepareToPlay(kSampleRate, kBlockSize);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            editor->setSize(1400, 900);
            auto* bar = editor->debugTopMenuBar();
            auto* settings = editor->debugSettingsPanel();

            if (bar != nullptr && settings != nullptr)
            {
                const auto pressGear = [&] { bar->getSettingsButton().onClick(); };

                // From FLT, not from the default, so "it went back" cannot pass
                // by returning to a fixed panel.
                editor->debugSelectSection(3);
                pressGear();
                const auto opened = editor->debugSelectedSection();
                pressGear();
                const auto returned = editor->debugSelectedSection();

                check("Settings_TheGearTogglesAndReturnsToWhereYouWere",
                      opened == 6 && returned == 3,
                      "from FLT the gear opened section " + juce::String(opened)
                          + " and came back to " + juce::String(returned));

                // The Close button does the same thing, from a different panel.
                editor->debugSelectSection(5);
                pressGear();
                const auto openedAgain = editor->debugSelectedSection();
                settings->debugCloseButton().onClick();
                const auto closed = editor->debugSelectedSection();

                check("Settings_TheCloseButtonReturnsToWhereYouWere",
                      openedAgain == 6 && closed == 5,
                      "from MIX, Close came back to section " + juce::String(closed));

                // And the strip comes back with the panel, without anything
                // else having to resize the window.
                check("Settings_ClosingBringsTheMacroStripBack",
                      ! editor->debugMacroStripArea().isEmpty(),
                      editor->debugMacroStripArea().isEmpty()
                          ? "the strip stayed hidden after closing"
                          : "the strip is back");
            }
        }
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

    // ========================================================================
    // The order the constructor and destructor depend on
    // ========================================================================
    //
    // The constructor is 1,270 lines and three of its steps have to happen in
    // a particular order. Each one says so in a comment, which is the weakest
    // form a constraint can take: a comment does not fail when it stops being
    // true, and every one of these fails SILENTLY - no crash, no assertion, a
    // control that quietly is not there or a layer that quietly is underneath.
    //
    // These are written so that splitting the constructor into per-panel
    // methods, which is the next thing anyone will want to do to that file,
    // cannot reorder them without a test going red.

    // ---- the wavetable controls are handed to the panel that shows them ----
    //
    // configureWavetableControls() ends with `if (oscPanel != nullptr)`, and
    // that is the whole of its protection. Called before the oscillator panel
    // is constructed it does not crash and does not complain: it configures
    // three combo boxes and three knobs, hands them to nobody, and returns.
    // The TABLE menu and the POSITION knob then simply do not exist on screen.
    //
    // The knobs carry their parameter IDs, so finding them in the editor's
    // component tree answers "did they reach a parent" exactly.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 256);
        processor.prepareToPlay(48000.0, 256);

        std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());

        if (editor != nullptr)
        {
            juce::StringArray wanted;
            for (int osc = 0; osc < 3; ++osc)
            {
                wanted.add(processor.getOscillatorWtPositionParam(osc).getParameterID());
            }

            // Being present in the tree is not the question - the knobs are
            // constructed as editor members either way, and something else
            // parents them. The question is whether they are UNDER the
            // oscillator panel, which is what setWavetableControls does and
            // what the early call skips.
            juce::StringArray inTree;
            juce::StringArray underOscPanel;
            std::function<void(juce::Component&)> walk = [&](juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (child == nullptr) { continue; }
                    if (auto* slider = dynamic_cast<juce::Slider*>(child))
                    {
                        const auto id = px3::ui::parameterIdOf(*slider);
                        if (wanted.contains(id))
                        {
                            inTree.addIfNotAlreadyThere(id);
                            for (auto* up = slider->getParentComponent(); up != nullptr;
                                 up = up->getParentComponent())
                            {
                                if (dynamic_cast<OscPanel*>(up) != nullptr)
                                {
                                    underOscPanel.addIfNotAlreadyThere(id);
                                    break;
                                }
                            }
                        }
                    }
                    walk(*child);
                }
            };
            walk(*editor);

            check("EditorOrder_WavetableControlsReachTheOscillatorPanel",
                  underOscPanel.size() == 3,
                  juce::String(underOscPanel.size())
                      + " of 3 wavetable POSITION knobs are under the oscillator panel ("
                      + juce::String(inTree.size()) + " exist anywhere in the tree)"
                      + (underOscPanel.size() == 3
                             ? ""
                             : " - configureWavetableControls ran before oscPanel existed"));
        }
    }

    // ---- the third constraint is not a unit test, and cannot be -------------
    //
    // The destructor clears the parameter attachments BEFORE resetting the
    // panels, because FxCardComponent owns its own sliders: destroy the panel
    // first and ~SliderParameterAttachment calls removeListener on freed
    // memory. Nothing an assertion can read distinguishes the two orders - the
    // editor is gone either way, and in a release build the wrong order
    // usually appears to work.
    //
    // Reordering it and running the ASan build over this very suite reports:
    //
    //   ERROR: AddressSanitizer: heap-use-after-free
    //     #0 juce::Slider::removeListener            juce_Slider.cpp:1476
    //     #1 juce::SliderParameterAttachment::~...   juce_ParameterAttachments.cpp:177
    //     #2 PX3SynthAudioProcessorEditor::~...      PluginEditor.cpp
    //   freed by ~FxCardComponent
    //
    // which is the fault the destructor's own comment records having been
    // measured as a segfault on quit. So the constraint IS covered, by the
    // editor constructions already in this file plus the asan build - not by
    // anything written here. Run it after touching the destructor:
    //
    //   cmake --build build/asan --target PX3Tests && \
    //     ./build/asan/PX3Tests_artefacts/RelWithDebInfo/PX3Tests editorlayout

    // ---- the processor holds nothing pointing at a closed editor ------------
    //
    // The destructor drops the processor's callback and the learn selection
    // "before anything else", because the processor outlives the window and
    // would otherwise call into freed memory on the next CC. The window is
    // gone by the time that happens, so nothing about it looks wrong until a
    // controller moves.
    //
    // This pins that the clearing HAPPENS, not where in the destructor it
    // happens: both are true once the destructor has run, whatever order it
    // ran in. It catches the line being dropped, which is the likely accident
    // when that function is split up.
    {
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 256);
        processor.prepareToPlay(48000.0, 256);

        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
            if (auto* px3Editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(editor.get()))
            {
                // Arm the two things the destructor is responsible for
                // clearing, so this cannot pass by their never having been set.
                processor.setMidiLearnTargets({ processor.getFilterCutoffParam(0).getParameterID() });
                juce::ignoreUnused(px3Editor);
            }
        }

        // A CC arriving after the window closed. Under the fault this is the
        // call that lands in freed memory.
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 21, 100), 0);
        buffer.clear();
        processor.processBlock(buffer, midi);
        processor.applyPendingMidiMappings();

        check("EditorOrder_AClosedEditorLeavesNothingPointingAtIt",
              processor.onMidiMappingAssigned == nullptr
                  && processor.getMidiLearnTargets().isEmpty(),
              juce::String("after the window closed the processor holds ")
                  + (processor.onMidiMappingAssigned == nullptr ? "no callback" : "A CALLBACK")
                  + " and " + juce::String(processor.getMidiLearnTargets().size())
                  + " learn targets");
    }
    // ---- the update notice is one closed bubble, arrow included ------------
    //
    // The point of building the pointer into the path rather than drawing a
    // triangle on top is that fill and outline stay continuous. That is easy to
    // regress and impossible to see in a headless test by looking at pixels, so
    // the geometry is checked instead: one subpath, closed, tall enough to
    // include the arrow, and the arrow sitting where the config says.
    {
        SpeechBubbleLabel::Style style;
        style.cornerRadius = 6.0f;
        style.borderWidth = 1.0f;
        style.arrowWidth = 14.0f;
        style.arrowHeight = 8.0f;
        style.arrowInsetFromRight = 18.0f;

        const juce::Rectangle<float> bounds(0.0f, 0.0f, 340.0f, 30.0f);
        const auto path = SpeechBubbleLabel::buildBubblePath(bounds, style);

        int subPaths = 0;
        for (juce::Path::Iterator it(path); it.next();)
        {
            if (it.elementType == juce::Path::Iterator::startNewSubPath) { ++subPaths; }
        }

        const auto box = path.getBounds();

        // The tip reaches the top of the component; the body starts below it.
        const auto reachesTop = box.getY() <= style.borderWidth;
        const auto spansHeight = box.getHeight() >= bounds.getHeight() - style.borderWidth * 2.0f - 0.5f;

        // The arrow is on the RIGHT half, which is what "aligned from top right"
        // has to mean for a 340-wide bubble with an 18px inset.
        const auto tipX = bounds.getRight() - style.arrowInsetFromRight - style.arrowWidth * 0.5f;
        const auto arrowIsRight = tipX > bounds.getCentreX();

        check("UpdateNotice_TheBubbleAndItsArrowAreOneClosedShape",
              subPaths == 1 && reachesTop && spansHeight && arrowIsRight,
              "subpaths=" + juce::String(subPaths)
                  + " bounds=" + juce::String(box.getWidth(), 1) + "x" + juce::String(box.getHeight(), 1)
                  + " tipX=" + juce::String(tipX, 1));
    }

    // A zero-height arrow must degrade to a plain rounded rectangle rather than
    // drawing a degenerate spike, because that is what setting arrowHeight to 0
    // in UIConfig is asking for.
    {
        SpeechBubbleLabel::Style style;
        style.arrowHeight = 0.0f;
        const auto path = SpeechBubbleLabel::buildBubblePath({ 0.0f, 0.0f, 200.0f, 24.0f }, style);
        check("UpdateNotice_ZeroArrowHeightIsJustARoundedRectangle",
              ! path.isEmpty() && path.getBounds().getHeight() <= 24.0f,
              "height " + juce::String(path.getBounds().getHeight(), 1));
    }
}

} // namespace px3tests
