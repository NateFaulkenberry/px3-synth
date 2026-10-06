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
                                "fx.vibe.intensity", "fx.vibe.level", "fx.analog.amount", "fx.reverb.shimmer",
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

        // The send section is the reorderable chain; SPREAD, ANALOG, VIBE and
        // LUCY have their own sections.
        const auto strip = panel != nullptr ? panel->debugRack().stagesIn(px3::ui::FxDomain::send) : std::vector<int> {};
        const auto has = [&strip](int id) { return std::find(strip.begin(), strip.end(), id) != strip.end(); };
        check("FxCards_SpreadIsNotInTheReorderableStrip",
              strip.size() == static_cast<std::size_t>(px3::kFxStageCount - 4)
                  && ! has(px3::fxStageStereoSpread) && ! has(px3::fxStageAnalog) && has(px3::fxStageDistortion)
                  && has(px3::fxStageChorus));
        // VIBE is a pre-chain insert on the whole instrument: no node in the
        // strip, its card first after ANALOG, ahead of the chain's cards.
        {
            auto* analogCard = panel != nullptr ? panel->cardForSection(px3::fxStageAnalog) : nullptr;
            auto* vibeCard = panel != nullptr ? panel->cardForSection(px3::fxStageVibe) : nullptr;
            auto* driveCard = panel != nullptr ? panel->cardForSection(px3::fxStageDistortion) : nullptr;
            const auto at = [](const juce::Component* c) { return c->getY() * 100000 + c->getX(); };
            check("FxCards_VibeIsAFixedFirstStage",
                  ! has(px3::fxStageVibe) && ! FxPanel::isReorderable(px3::fxStageVibe)
                      && analogCard != nullptr && vibeCard != nullptr && driveCard != nullptr
                      && showingIn(*vibeCard, *editor) && at(analogCard) < at(vibeCard) && at(vibeCard) < at(driveCard),
                  "VIBE in strip " + juce::String(has(px3::fxStageVibe) ? "YES" : "no"));
        }
        // LUCY is a master insert now, like SPREAD: no node in the strip, a
        // card after the send chain, and LUCY before SPREAD because that is
        // the order they process in.
        auto* lucy = panel != nullptr ? panel->cardForSection(px3::fxStageLucy) : nullptr;
        auto* spread = panel != nullptr ? panel->cardForSection(px3::fxStageStereoSpread) : nullptr;
        auto* reverbCard = panel != nullptr ? panel->cardForSection(px3::fxStageReverb) : nullptr;
        const auto readingOrder = [](const juce::Component* c)
        { return c->getY() * 100000 + c->getX(); };   // the cards share one grid parent
        check("FxCards_LucyIsAMasterStageAfterTheChain",
              ! has(px3::fxStageLucy) && ! FxPanel::isReorderable(px3::fxStageLucy)
                  && lucy != nullptr && spread != nullptr && reverbCard != nullptr && showingIn(*lucy, *editor)
                  && readingOrder(reverbCard) < readingOrder(lucy) && readingOrder(lucy) < readingOrder(spread),
              "LUCY in strip " + juce::String(has(px3::fxStageLucy) ? "YES" : "no"));
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

        // Reverb: the fourth knob is the type's own control - SHIMMER on
        // CLOUD only, nothing on PLATE - and controls a type does not read
        // are dimmed.
        auto* reverb = panel != nullptr ? panel->cardForSection(px3::fxStageReverb) : nullptr;
        setParam(processor, "fx.reverb.algorithm", 3.0f);
        editor->debugTimerTick();
        const auto onCloud = reverb != nullptr ? reverb->shownSlotMember("typeSlot") : juce::String("no card");
        const auto earlyDimmedOnCloud = reverb != nullptr && reverb->isKnobDimmed("early");
        setParam(processor, "fx.reverb.algorithm", 1.0f);
        editor->debugTimerTick();
        const auto onPlate = reverb != nullptr ? reverb->shownSlotMember("typeSlot") : juce::String("no card");
        setParam(processor, "fx.reverb.algorithm", 5.0f);
        editor->debugTimerTick();
        const auto modDimmedOnGated = reverb != nullptr && reverb->isKnobDimmed("modulation");
        const auto lengthCaption = reverb != nullptr ? reverb->debugKnobCaption("decay") : juce::String();
        check("FxCards_ReverbShowsEachTypesOwnControl",
              onCloud == "shimmer" && onPlate.isEmpty() && earlyDimmedOnCloud && modDimmedOnGated && lengthCaption == "LENGTH",
              "CLOUD slot '" + onCloud + "', PLATE slot '" + onPlate + "', GATED DECAY reads '" + lengthCaption + "'");
        setParam(processor, "fx.reverb.algorithm", 0.0f);

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

    // ---- the FX page mirrors the signal flow ----------------------------------
    //
    // Three sections in processing order (INSTRUMENT, SEND FX, MASTER); only
    // the send cards have a drag handle and only they reorder; dragging one
    // writes the processor's order; wrapping never changes the order; nothing
    // overlaps or clips at the minimum, default and maximum window sizes.
    {
        using px3::ui::FxDomain;
        UIConfigManager manager;
        manager.setConfigFile(shippingUiConfigFile());
        manager.loadInitial();
        const auto config = manager.getConfig();
        const auto minCard = config != nullptr ? config->getInt("fx.rack.minCardWidth", 0) : 0;

        const auto sendStagesOf = [](const px3::FxOrder& order)
        {
            std::vector<int> ids;
            for (const auto stage : order)
                if (px3::isSendChainFxStage(stage)) ids.push_back(stage);
            return ids;
        };

        struct Size { int w; int h; int columnsAtLeast; };
        for (const auto size : { Size { 1100, 700, 3 }, Size { 1488, 884, 4 }, Size { 2400, 1400, 6 } })
        {
            const auto at = " @" + juce::String(size.w) + "x" + juce::String(size.h);
            PX3SynthAudioProcessor processor;
            auto base = makeEditor(processor, size.w, size.h);
            auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
            editor->debugTimerTick();
            editor->debugSelectSection(4);
            auto* panel = editor->debugFxPanel();
            if (panel == nullptr || config == nullptr) { check(juce::String("FxPage_PanelExists" + at).toRawUTF8(), false); continue; }
            panel->setUIConfig(config);
            auto& rack = panel->debugRack();
            rack.setMotionEnabled(false);

            // -- sections, in the order the signal meets them --
            const auto hi = rack.headerBounds(FxDomain::instrument);
            const auto hs = rack.headerBounds(FxDomain::send);
            const auto hm = rack.headerBounds(FxDomain::master);
            check(juce::String("FxPage_ThreeSectionsInProcessingOrder" + at).toRawUTF8(),
                  ! hi.isEmpty() && ! hs.isEmpty() && ! hm.isEmpty()
                      && rack.sectionBounds(FxDomain::instrument).getBottom() <= hs.getY()
                      && rack.sectionBounds(FxDomain::send).getBottom() <= hm.getY(),
                  hi.toString() + " | " + hs.toString() + " | " + hm.toString());

            check(juce::String("FxPage_AnalogThenVibeAreTheInstrumentSection" + at).toRawUTF8(),
                  rack.stagesIn(FxDomain::instrument) == std::vector<int>({ px3::fxStageAnalog, px3::fxStageVibe })
                      && rack.stagesIn(FxDomain::master) == std::vector<int>({ px3::fxStageLucy, px3::fxStageStereoSpread }));
            check(juce::String("FxPage_SendSectionIsTheProcessorsChainInOrder" + at).toRawUTF8(),
                  rack.stagesIn(FxDomain::send) == sendStagesOf(processor.getFxProcessingOrder())
                      && rack.stagesIn(FxDomain::send).size() == 6);

            // -- affordances: a handle on send cards, a tag on the rest --
            juce::StringArray wrongAffordance;
            for (const auto stage : px3::kDefaultFxOrder)
            {
                auto* rail = rack.railFor(stage);
                if (rail == nullptr) { wrongAffordance.add("no rail " + FxPanel::debugSectionName(stage)); continue; }
                const auto shouldHandle = px3::isSendChainFxStage(stage);
                if (rail->hasHandle() != shouldHandle || rail->handleBounds().isEmpty() == shouldHandle)
                    wrongAffordance.add(FxPanel::debugSectionName(stage));
            }
            check(juce::String("FxPage_OnlySendCardsHaveADragHandle" + at).toRawUTF8(), wrongAffordance.isEmpty(), wrongAffordance.joinIntoString(", "));
            check(juce::String("FxPage_FixedCardsSayWhereTheyRun" + at).toRawUTF8(),
                  rack.railFor(px3::fxStageAnalog)->tagText() == "PER VOICE"
                      && rack.railFor(px3::fxStageVibe)->tagText() == "INSTRUMENT INSERT"
                      && rack.railFor(px3::fxStageLucy)->tagText() == "MASTER INSERT"
                      && rack.railFor(px3::fxStageStereoSpread)->tagText() == "MASTER INSERT"
                      && rack.railFor(rack.stagesIn(FxDomain::send).front())->tagText() == "SEND 1");

            // -- one footprint, never below the minimum, wrapping keeps order --
            juce::String widths;
            auto footprint = minCard > 0;
            for (const auto stage : px3::kDefaultFxOrder)
            {
                auto* card = panel->debugComponentForSection(stage);
                footprint = footprint && card != nullptr && card->getWidth() >= minCard && card->getWidth() == rack.cardWidth();
                if (card != nullptr) widths << card->getWidth() << " ";
            }
            check(juce::String("FxPage_EveryCardSharesOneWidthAboveTheMinimum" + at).toRawUTF8(), footprint,
                  widths + "(min " + juce::String(minCard) + ")");

            auto readingOrder = true;
            const auto send = rack.stagesIn(FxDomain::send);
            for (std::size_t i = 1; i < send.size(); ++i)
            {
                const auto a = rack.restingSlot(send[i - 1]);
                const auto b = rack.restingSlot(send[i]);
                readingOrder = readingOrder && (b.getY() > a.getBottom() || (b.getY() == a.getY() && b.getX() > a.getRight()));
            }
            check(juce::String("FxPage_WrappingKeepsTheChainInReadingOrder" + at).toRawUTF8(),
                  readingOrder && rack.columns() >= size.columnsAtLeast,
                  juce::String(rack.columns()) + " columns");

            const auto lastSend = rack.restingSlot(send.back());
            check(juce::String("FxPage_FxReturnFollowsTheLastSendCard" + at).toRawUTF8(),
                  ! rack.fxReturnBounds().isEmpty() && rack.fxReturnBounds().getY() >= lastSend.getBottom()
                      && rack.fxReturnBounds().getBottom() <= hm.getY()
                      && rack.fxReturnBounds().getX() <= lastSend.getCentreX()
                      && rack.fxReturnBounds().getRight() >= lastSend.getCentreX(),
                  rack.fxReturnBounds().toString() + " after " + lastSend.toString());
            check(juce::String("FxPage_MasterEndsInTheOutputCap" + at).toRawUTF8(),
                  ! rack.endCapBounds().isEmpty()
                      && rack.endCapBounds().getBottom() <= rack.getHeight()
                      && rack.endCapBounds().getRight() <= rack.getWidth());

            // -- the bus send lives in the SEND FX header --
            auto& knob = panel->busSendKnob();
            check(juce::String("FxPage_HeaderSendIsTheBusSend" + at).toRawUTF8(),
                  px3::ui::parameterIdOf(knob) == "mix.send.fx.level" && knob.isVisible()
                      && hs.contains(knob.getBounds()) && hs.contains(rack.busSendLabelBounds())
                      && px3::ui::FxRackCanvas::busSendTooltip().containsIgnoreCase("FX bus"),
                  px3::ui::parameterIdOf(knob) + " at " + knob.getBounds().toString());

            // -- nothing overlaps, nothing clips --
            juce::StringArray overlaps;
            std::vector<std::pair<juce::String, juce::Rectangle<int>>> boxes;
            for (const auto stage : px3::kDefaultFxOrder)
            {
                const auto name = FxPanel::debugSectionName(stage);
                auto* card = panel->debugComponentForSection(stage);
                auto* rail = rack.railFor(stage);
                if (card == nullptr || rail == nullptr) { continue; }
                boxes.push_back({ name, card->getBounds() });
                boxes.push_back({ name + " rail", rail->getBounds() });
                if (rail->getBottom() != card->getY() || rail->getX() != card->getX() || rail->getWidth() != card->getWidth())
                    overlaps.add(name + " rail is not on its card");

                // Inside the card: every visible control on the card, in it,
                // and clear of the others.
                std::vector<juce::Component*> controls;
                for (auto* child : card->getChildren())
                    if (child->isVisible() && ! child->getBounds().isEmpty()) controls.push_back(child);
                for (std::size_t i = 0; i < controls.size(); ++i)
                {
                    const juce::Component& control = *controls[i];
                    // Cramped knobs are a fit failure too: a rotary drawn smaller
                    // than this is a card squeezed below its usable size.
                    if (auto* rotary = dynamic_cast<const juce::Slider*>(&control);
                        rotary != nullptr && rotary->isRotary() && juce::jmin(rotary->getWidth(), rotary->getHeight()) < 24)
                        overlaps.add(name + " knob crushed to " + rotary->getBounds().toString());
                    if (! card->getLocalBounds().contains(control.getBounds()))
                        overlaps.add(name + " clips " + juce::String(typeid(control).name()) + " " + control.getBounds().toString());
                    for (std::size_t j = i + 1; j < controls.size(); ++j)
                    {
                        const auto both = controls[i]->getBounds().getIntersection(controls[j]->getBounds());
                        if (both.getWidth() > 2 && both.getHeight() > 2)
                            overlaps.add(name + ": " + controls[i]->getBounds().toString() + " x " + controls[j]->getBounds().toString());
                    }
                }
            }
            boxes.push_back({ "send header", hs });
            boxes.push_back({ "instrument header", hi });
            boxes.push_back({ "master header", hm });
            boxes.push_back({ "fx return", rack.fxReturnBounds() });
            for (std::size_t i = 0; i < boxes.size(); ++i)
            {
                if (! rack.getLocalBounds().contains(boxes[i].second)) overlaps.add(boxes[i].first + " outside the page");
                for (std::size_t j = i + 1; j < boxes.size(); ++j)
                    if (boxes[i].second.intersects(boxes[j].second))
                        overlaps.add(boxes[i].first + " x " + boxes[j].first);
            }
            auto& view = panel->debugViewport();
            if (rack.getWidth() > view.getMaximumVisibleWidth())
                overlaps.add("page wider than its view: " + juce::String(rack.getWidth()) + " > " + juce::String(view.getMaximumVisibleWidth()));
            check(juce::String("FxPage_NothingOverlapsOrClips" + at).toRawUTF8(), overlaps.isEmpty(), overlaps.joinIntoString("; "));

            if (size.w != 1488) { continue; }

            // ---- dragging -------------------------------------------------------
            const auto mouse = [](juce::Component& on, juce::Point<int> p)
            {
                const auto at2 = p.toFloat();
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at2, juce::ModifierKeys(),
                                        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &on, &on, juce::Time::getCurrentTime(), at2,
                                        juce::Time::getCurrentTime(), 1, false);
            };
            const auto drag = [&](int stage, juce::Point<int> to)
            {
                auto* rail = rack.railFor(stage);
                const auto start = rail->getLocalBounds().getCentre();
                const auto delta = to - rack.restingSlot(stage).getPosition();
                rail->mouseDown(mouse(*rail, start));
                rail->mouseDrag(mouse(*rail, start + delta / 2));
                const auto dragging = rack.isDragging();
                rail->mouseDrag(mouse(*rail, start + delta));
                rail->mouseUp(mouse(*rail, start + delta));
                return dragging;
            };

            {
                // DOOM (third) onto the first slot.
                const auto before = rack.stagesIn(FxDomain::send);
                const auto firstCell = rack.restingSlot(before.front());
                const auto lifted = drag(px3::fxStageDoom, rack.restingSlot(before.front()).getPosition());
                const auto after = sendStagesOf(processor.getFxProcessingOrder());
                auto expected = before;
                expected.erase(std::find(expected.begin(), expected.end(), px3::fxStageDoom));
                expected.insert(expected.begin(), px3::fxStageDoom);
                const auto full = processor.getFxProcessingOrder();
                check("FxPage_DraggingASendCardReordersTheDsp",
                      lifted && after == expected && rack.stagesIn(FxDomain::send) == expected
                          && full[0] == px3::kDefaultFxOrder[0] && full[1] == px3::kDefaultFxOrder[1]
                          && full[8] == px3::fxStageLucy && full[9] == px3::fxStageStereoSpread,
                      "send order now " + juce::String(static_cast<int>(after.size())) + " stages, first "
                          + FxPanel::debugSectionName(after.empty() ? -1 : after.front()));
                check("FxPage_TheDroppedCardRestsInItsNewSlot",
                      panel->debugComponentForSection(px3::fxStageDoom)->getPosition()
                              == firstCell.getPosition().translated(0, rack.style().railHeight)
                          && rack.restingSlot(px3::fxStageDoom) == firstCell,
                      panel->debugComponentForSection(px3::fxStageDoom)->getBounds().toString());
            }
            {
                // Across a wrap: the last card (second row) to the second slot.
                const auto before = rack.stagesIn(FxDomain::send);
                drag(before.back(), rack.restingSlot(before[1]).getPosition());
                const auto after = sendStagesOf(processor.getFxProcessingOrder());
                check("FxPage_DragAcrossAWrappedRow", after.size() == 6 && after[1] == before.back() && after[0] == before[0]);
            }
            {
                // Fixed stages have no handle: pressing and dragging their rail
                // into the middle of the chain does nothing.
                const auto before = processor.getFxProcessingOrder();
                const auto target = rack.restingSlot(rack.stagesIn(FxDomain::send)[2]).getPosition();
                auto anyLifted = false;
                for (const auto stage : { px3::fxStageVibe, px3::fxStageAnalog, px3::fxStageLucy, px3::fxStageStereoSpread })
                {
                    anyLifted = drag(stage, target) || anyLifted;
                    rack.railPressed(stage, rack.restingSlot(stage).getCentre());   // even asked directly
                    rack.railDragged(target);
                    anyLifted = rack.isDragging() || anyLifted;
                    rack.railReleased(target);
                }
                check("FxPage_FixedStagesCannotBeDroppedIntoTheChain",
                      ! anyLifted && processor.getFxProcessingOrder() == before
                          && rack.stagesIn(FxDomain::instrument) == std::vector<int>({ px3::fxStageAnalog, px3::fxStageVibe }));
            }
            {
                // A send card dragged down onto MASTER stays in the send
                // section and only reorders the chain.
                const auto first = rack.stagesIn(FxDomain::send).front();
                auto* rail = rack.railFor(first);
                const auto start = rail->getLocalBounds().getCentre();
                const auto far = rack.headerBounds(FxDomain::master).getCentre() - rack.restingSlot(first).getPosition();
                rail->mouseDown(mouse(*rail, start));
                rail->mouseDrag(mouse(*rail, start + far));
                auto* card = panel->debugComponentForSection(first);
                const auto held = rack.sectionBounds(FxDomain::send).contains(card->getBounds().withHeight(1));
                rail->mouseUp(mouse(*rail, start + far));
                const auto full = processor.getFxProcessingOrder();
                check("FxPage_ADraggedCardIsHeldInTheSendSection",
                      held && full[8] == px3::fxStageLucy && full[9] == px3::fxStageStereoSpread
                          && rack.stagesIn(FxDomain::master) == std::vector<int>({ px3::fxStageLucy, px3::fxStageStereoSpread }),
                      card->getBounds().toString());
            }
            {
                // The header SEND writes the bus send.
                knob.setValue(0.25, juce::sendNotificationSync);
                check("FxPage_HeaderSendWritesTheBusSendParameter",
                      std::abs(processor.getFxSendGainParam().get() - 0.25f) < 1.0e-3f,
                      juce::String(processor.getFxSendGainParam().get(), 3));
            }
            {
                // Bypass keeps a send card in place, says so, and leaves it draggable.
                const auto stage = rack.stagesIn(FxDomain::send)[1];
                const auto slot = rack.restingSlot(stage);
                panel->setSectionActive(stage, false);
                rack.layoutForWidth(rack.getWidth(), false);
                check("FxPage_BypassedSendCardStaysPutAndDraggable",
                      rack.restingSlot(stage) == slot && rack.railFor(stage)->hasHandle()
                          && rack.stagesIn(FxDomain::send)[1] == stage);
                panel->setSectionActive(stage, true);
            }
        }
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
