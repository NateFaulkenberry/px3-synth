#include "TestSupport.h"
#include "BypassButton.h"
#include "BreakpointEnvelopeEditor.h"

#include "../../shared/UI/Style/Theme.h"
#include "../../shared/UI/Components/ChipLabel.h"
#include "../../shared/UI/Components/ToggleChipButton.h"

#include <functional>
#include <typeinfo>

// testVisualRedesign
//
// The 0.8.0 visual redesign's contract (docs/PX3_0.8.0_VISUAL_REDESIGN.md):
// every knob on screen is a bound, reachable parameter control; no caption is
// clipped at the minimum window size; the FX cards expose the new DSP
// parameters; Spread is outside the reorderable chain; the Drive card exists;
// VOICE shows OSC, FILTER and AMP together at every supported size.

namespace px3tests
{
namespace
{
// Visible all the way up to the editor, so a control inside a hidden page or a
// folded row does not count as on screen.
bool showingIn(const juce::Component& c, const juce::Component& root)
{
    for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
    {
        if (! p->isVisible()) { return false; }
    }
    return true;
}

// The part of a component the user can actually see: clipped by every
// ancestor (viewports included), in editor coordinates.
juce::Rectangle<int> visibleArea(const juce::Component& c, const juce::Component& root)
{
    auto area = root.getLocalArea(&c, c.getLocalBounds());
    for (auto* p = c.getParentComponent(); p != nullptr && p != &root; p = p->getParentComponent())
    {
        area = area.getIntersection(root.getLocalArea(p, p->getLocalBounds()));
    }
    return area.getIntersection(root.getLocalBounds());
}

void walk(juce::Component& c, const std::function<void(juce::Component&)>& visit)
{
    for (auto* child : c.getChildren())
    {
        if (child == nullptr) { continue; }
        visit(*child);
        walk(*child, visit);
    }
}

std::unique_ptr<juce::AudioProcessorEditor> makeEditor(PX3SynthAudioProcessor& processor, int w, int h)
{
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    base->setSize(w, h);
    return base;
}
} // namespace

void testVisualRedesign()
{
    suite("VISUAL REDESIGN");

    {
        // Every power button is the same: one size, and on the VOICE header row
        // the same height on the page. They are card chrome placed by the card,
        // never by a second authority.
        PX3SynthAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1518, 918);
        std::vector<juce::Rectangle<int>> buttons;
        std::function<void(juce::Component&)> collect = [&](juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<px3::ui::BypassButton*>(child))
                {
                    auto visible = true;
                    for (auto* p = static_cast<juce::Component*>(b); p != nullptr && p != editor.get() && visible; p = p->getParentComponent())
                        visible = p->isVisible();
                    if (visible && ! b->getBounds().isEmpty()) buttons.push_back(editor->getLocalArea(b, b->getLocalBounds()));
                }
                collect(*child);
            }
        };
        collect(*editor);
        auto sameSize = ! buttons.empty();
        int topRowY = 1 << 30;
        for (const auto& r : buttons) topRowY = juce::jmin(topRowY, r.getY());
        int inTopRow = 0, misaligned = 0;
        for (const auto& r : buttons)
        {
            sameSize = sameSize && r.getWidth() == buttons.front().getWidth() && r.getHeight() == buttons.front().getHeight();
            if (r.getY() - topRowY < 10) { ++inTopRow; if (r.getY() != topRowY) ++misaligned; }
        }
        check("PowerButtons_AreUniformAcrossCards", sameSize && inTopRow >= 6 && misaligned == 0,
              juce::String(static_cast<int>(buttons.size())) + " visible power buttons, size "
                  + juce::String(buttons.empty() ? 0 : buttons.front().getWidth()) + " px; " + juce::String(inTopRow)
                  + " on the header row, " + juce::String(misaligned) + " out of line");
    }

    {
        // The AMP envelope's note playhead animates on VOICE: the 30 Hz refresh
        // used to stop at OSC because the panels refreshed as an else-if chain.
        PX3SynthAudioProcessor processor;
        processor.setPlayConfigDetails(0, 2, 48000.0, 512);
        processor.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        editor->setSize(1518, 918);
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        processor.processBlock(buffer, midi);
        for (int block = 0; block < 4; ++block) { juce::MidiBuffer none; buffer.clear(); processor.processBlock(buffer, none); }
        if (auto* synthEditor = dynamic_cast<PX3SynthAudioProcessorEditor*>(editor.get())) { synthEditor->debugTimerTick(); }
        std::function<void(juce::Component&, std::vector<BreakpointEnvelopeEditor*>&)> collect =
            [&](juce::Component& c, std::vector<BreakpointEnvelopeEditor*>& out)
        {
            for (auto* child : c.getChildren())
            {
                if (auto* e = dynamic_cast<BreakpointEnvelopeEditor*>(child); e != nullptr)
                {
                    // Visible all the way up: the editor is offscreen, so isShowing() is always false.
                    auto visible = true;
                    for (auto* p = static_cast<juce::Component*>(e); p != nullptr && p != editor.get() && visible; p = p->getParentComponent()) visible = p->isVisible();
                    if (visible) out.push_back(e);
                }
                collect(*child, out);
            }
        };
        std::vector<BreakpointEnvelopeEditor*> graphs;
        collect(*editor, graphs);
        auto playing = false;
        for (auto* g : graphs) playing = playing || g->getProgress().active;
        check("AmpEnvelope_PlayheadAnimatesWhileANoteSounds", ! graphs.empty() && playing,
              juce::String(static_cast<int>(graphs.size())) + " envelope graph(s) on VOICE, playhead " + (playing ? "active" : "idle"));
    }

    // ---- every visible knob is bound and reachable -------------------------
    {
        PX3SynthAudioProcessor processor;
        auto base = makeEditor(processor, 1518, 918);
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        juce::StringArray unbound, unreachable;
        auto knobsSeen = 0;
        for (const auto section : { 0, 1, 4, 5 })
        {
            editor->debugTimerTick();
            editor->debugSelectSectionPersisted(section);
            walk(*editor, [&](juce::Component& c)
            {
                auto* slider = dynamic_cast<juce::Slider*>(&c);
                if (slider == nullptr || ! slider->isRotary() || ! showingIn(*slider, *editor)) { return; }
                const auto area = visibleArea(*slider, *editor);
                if (area.getWidth() < 6 || area.getHeight() < 6) { return; }
                ++knobsSeen;
                const auto id = px3::ui::parameterIdOf(*slider);
                if (id.isEmpty() || processor.findRangedParameterById(id) == nullptr)
                {
                    unbound.add("section " + juce::String(section) + " " + slider->getTooltip()
                                + " at " + area.toString());
                    return;
                }
                if (editor->debugKnobAt(area.getCentre()) != slider)
                {
                    unreachable.add(id);
                }
            });
        }
        check("VisualRedesign_EveryVisibleKnobIsBoundToAParameter",
              knobsSeen > 80 && unbound.isEmpty(),
              juce::String(knobsSeen) + " knobs; unbound: " + unbound.joinIntoString(", "));
        check("VisualRedesign_EveryVisibleKnobIsReachableByTheMouse",
              unreachable.isEmpty(), "covered: " + unreachable.joinIntoString(", "));
    }

    // ---- no clipped captions at the minimum size ---------------------------
    {
        PX3SynthAudioProcessor processor;
        auto base = makeEditor(processor, 1100, 700);
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        const auto minimum = editor->getConstrainer() != nullptr
                                 ? juce::Point<int>(editor->getConstrainer()->getMinimumWidth(),
                                                    editor->getConstrainer()->getMinimumHeight())
                                 : juce::Point<int>();
        check("VisualRedesign_MinimumSizeIs1100x700", minimum == juce::Point<int>(1100, 700),
              minimum.toString());

        juce::StringArray clipped;
        auto measured = 0;
        for (const auto section : { 0, 1, 4, 5 })
        {
            editor->debugTimerTick();
            editor->debugSelectSectionPersisted(section);
            walk(*editor, [&](juce::Component& c)
            {
                if (! showingIn(c, *editor) || visibleArea(c, *editor).isEmpty()) { return; }
                juce::String text;
                float needed = 0.0f;
                if (auto* chip = dynamic_cast<px3::ui::ChipLabel*>(&c))
                {
                    text = chip->getText();
                    const auto isValue = text.containsAnyOf("0123456789") || text != text.toUpperCase();
                    needed = px3::ui::theme::textWidth(text, isValue ? px3::ui::theme::Type::value
                                                                     : px3::ui::theme::Type::label);
                }
                else if (auto* toggle = dynamic_cast<px3::ui::ToggleChipButton*>(&c))
                {
                    text = toggle->getButtonText();
                    needed = px3::ui::theme::textWidth(text, px3::ui::theme::Type::label) + 8.0f;
                }
                if (text.isEmpty() || c.getWidth() <= 0) { return; }
                ++measured;
                // Captions may shrink to 72% before they count as clipped -
                // the fitted-text floor every caption is drawn with.
                if (needed * 0.72f > static_cast<float>(c.getWidth()) + 1.0f)
                {
                    clipped.add("section " + juce::String(section) + " '" + text + "' needs "
                                + juce::String(needed, 1) + "px in " + juce::String(c.getWidth()));
                }
            });
        }
        check("VisualRedesign_NoCaptionIsClippedAtTheMinimumSize",
              measured > 100 && clipped.isEmpty(),
              juce::String(measured) + " captions; " + clipped.joinIntoString("; "));

        // VOICE at the minimum: OSC, FILTER and AMP side by side, each with room.
        editor->debugSelectSectionPersisted(0);
        editor->debugTimerTick();
        auto* flt = editor->debugFltPanel();
        auto* osc = editor->debugFindKnobForParameter("voice.osc1.tuning.octave");
        // The AMP card's own attack knob (a hidden legacy knob shares the ID).
        juce::Slider* amp = nullptr;
        walk(*editor, [&](juce::Component& c)
        {
            if (auto* sl = dynamic_cast<juce::Slider*>(&c); sl != nullptr && amp == nullptr
                && px3::ui::parameterIdOf(*sl) == "voice.amp.attack" && showingIn(*sl, *editor))
            {
                amp = sl;
            }
        });
        check("Navigation_VoiceShowsOscFilterAndAmpTogetherAtTheMinimumSize",
              flt != nullptr && showingIn(*flt, *editor) && flt->getWidth() > 300
                  && osc != nullptr && showingIn(*osc, *editor) && osc->getWidth() >= 20
                  && amp != nullptr && showingIn(*amp, *editor),
              juce::String("flt ") + (flt != nullptr ? flt->getBounds().toString() : "none")
                  + " osc " + (osc != nullptr ? osc->getBounds().toString() + (showingIn(*osc, *editor) ? " shown" : " hidden") : "none")
                  + " amp " + (amp != nullptr ? (showingIn(*amp, *editor) ? "shown" : "hidden") : "none"));
        auto* bar = editor->debugTopMenuBar();
        juce::StringArray tabs;
        if (bar != nullptr)
        {
            walk(*bar, [&](juce::Component& c)
            {
                if (auto* tab = dynamic_cast<TopMenuTabButton*>(&c); tab != nullptr && tab->isVisible())
                {
                    tabs.add(tab->getButtonText());
                }
            });
        }
        check("Navigation_TabsAreVoiceModFxMix",
              tabs.contains("VOICE") && tabs.contains("MOD") && tabs.contains("FX") && tabs.contains("MIX")
                  && ! tabs.contains("AMP") && ! tabs.contains("FLT"),
              tabs.joinIntoString(" "));
    }

    // ---- FX cards ------------------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        auto base = makeEditor(processor, 1518, 918);
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        editor->debugTimerTick();
        editor->debugSelectSectionPersisted(4);
        auto* panel = editor->debugFxPanel();

        juce::StringArray missing;
        for (const auto* id : { "fx.distortion.drive", "fx.distortion.tight", "fx.distortion.tone",
                                "fx.distortion.level", "fx.distortion.mix", "fx.vibe.speed",
                                "fx.vibe.intensity", "fx.vibe.amount", "fx.reverb.shimmer",
                                "fx.delay.wobble", "fx.delay.tape.quality", "fx.delay.tape.slip",
                                "fx.delay.mod.depth" })
        {
            if (editor->debugFindKnobForParameter(id) == nullptr) { missing.add(id); }
        }
        check("FxCards_ExposeTheNewParameters", missing.isEmpty(), "no knob for " + missing.joinIntoString(", "));

        auto* drive = panel != nullptr ? panel->cardForSection(px3::fxStageDistortion) : nullptr;
        check("FxCards_DriveCardIsOnThePage",
              drive != nullptr && showingIn(*drive, *editor) && drive->getWidth() > 150
                  && drive->choice("type") != nullptr && drive->choice("type")->getNumItems() == 3
                  && FxPanel::debugSectionName(px3::fxStageDistortion) == "DRIVE");

        const auto strip = panel != nullptr ? panel->debugStripStages() : std::vector<int> {};
        const auto has = [&strip](int id) { return std::find(strip.begin(), strip.end(), id) != strip.end(); };
        check("FxCards_SpreadIsNotInTheReorderableStrip",
              strip.size() == static_cast<std::size_t>(px3::kFxStageCount - 1)
                  && ! has(px3::fxStageStereoSpread) && has(px3::fxStageDistortion));
        auto* spread = panel != nullptr ? panel->cardForSection(px3::fxStageStereoSpread) : nullptr;
        check("FxCards_SpreadStillHasItsCardAfterTheChain", spread != nullptr && showingIn(*spread, *editor));

        // Spread's basic view is the essentials; ADVANCED unfolds the rest.
        if (spread != nullptr)
        {
            const auto shown = [spread](const char* id)
            { auto* k = spread->knob(id); return k != nullptr && k->isVisible() && k->getWidth() > 8; };
            const auto basic = shown("width") && shown("lowFreq") && shown("mix") && shown("amount")
                            && ! shown("depth") && ! shown("highFreq");
            spread->setAltMode(true);
            const auto advanced = shown("depth") && shown("highFreq") && shown("lowWidth") && shown("width");
            spread->setAltMode(false);
            check("FxCards_SpreadShowsEssentialsAndFoldsTheRest", basic && advanced);
        }

        // Delay: the algorithm decides which of its extra controls show.
        setParam(processor, "fx.delay.algorithm", 1.0f);
        editor->debugTimerTick();
        const auto visible = [&](const char* id)
        { auto* k = editor->debugFindKnobForParameter(id); return k != nullptr && showingIn(*k, *editor) && k->getWidth() > 8; };
        const auto tape = visible("fx.delay.wobble") && visible("fx.delay.tape.quality")
                       && visible("fx.delay.tape.slip") && ! visible("fx.delay.mod.depth");
        setParam(processor, "fx.delay.algorithm", 5.0f);
        editor->debugTimerTick();
        const auto modulated = visible("fx.delay.mod.depth") && ! visible("fx.delay.wobble");
        check("FxCards_DelayShowsTapeAndModulationControlsForTheirAlgorithms", tape && modulated,
              juce::String("tape ") + (tape ? "ok" : "no") + " modulated " + (modulated ? "ok" : "no")
                  + " algo " + juce::String(processor.getDelayAlgorithmParam().getIndex())
                  + [&] {
                        juce::String chain;
                        if (auto* k = editor->debugFindKnobForParameter("fx.delay.mod.depth"))
                            for (juce::Component* p = k; p != nullptr && p != editor; p = p->getParentComponent())
                                chain << " " << typeid(*p).name() << (p->isVisible() ? "+" : "-") << p->getBounds().toString();
                        return chain; }());

        // Reverb IR: the loader shows only in IR mode.
        auto* reverb = panel != nullptr ? panel->cardForSection(px3::fxStageReverb) : nullptr;
        const auto irBefore = reverb != nullptr && reverb->isFooterShown();
        setParam(processor, "fx.reverb.algorithm", 4.0f);
        editor->debugTimerTick();
        const auto irAfter = reverb != nullptr && reverb->isFooterShown();
        check("FxCards_ReverbIrLoaderShowsInIrMode", ! irBefore && irAfter,
              juce::String(irBefore ? "shown before " : "hidden before ") + (irAfter ? "shown after" : "hidden after")
                  + " algo " + juce::String(processor.getReverbAlgorithmParam().getIndex()));

        // Renamed captions: unclear labels from the audit are gone.
        juce::StringArray captions;
        walk(*editor, [&](juce::Component& c)
        {
            if (auto* l = dynamic_cast<juce::Label*>(&c)) { captions.add(l->getText()); }
            if (auto* b = dynamic_cast<juce::Button*>(&c)) { captions.add(b->getButtonText()); }
        });
        juce::StringArray stale;
        for (const auto* old : { "GLUE", "FREEZER", "THRESHOLD", "V-PRE", "V-POST", "LOW XO", "CHARACTER",
                                 "MODIFY", "BLEND", "CROSS" })
        {
            if (captions.contains(old)) { stale.add(old); }
        }
        check("FxCards_UnclearLabelsAreRenamed", stale.isEmpty(), stale.joinIntoString(", "));
    }

    // ---- ENV cards carry SYNC --------------------------------------------------
    {
        PX3SynthAudioProcessor processor;
        auto base = makeEditor(processor, 1518, 918);
        auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
        editor->debugSelectSectionPersisted(0);   // dense layout: the ENV modules are on VOICE
        auto* sync = editor->debugModPanel() != nullptr ? editor->debugModPanel()->getEnvelopeSyncButton(0) : nullptr;
        auto ok = sync != nullptr && showingIn(*sync, *editor) && sync->getWidth() > 20;
        if (ok)
        {
            sync->setToggleState(true, juce::sendNotificationSync);
            ok = processor.findRangedParameterById("mod.env1.sync")->getValue() > 0.5f;
        }
        check("NewControls_EnvelopeSyncToggleWritesTheParameter", ok);
    }
}

} // namespace px3tests
