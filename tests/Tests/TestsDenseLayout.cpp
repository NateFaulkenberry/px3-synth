#include "TestSupport.h"
#include "UILayout.h"
#include "UILayoutEditing.h"
#include "ModRouting.h"
#include "SubOscComponent.h"

#include "../../shared/UI/Style/Theme.h"
#include "../../shared/UI/Components/BypassButton.h"

#include <cmath>
#include <functional>
#include <typeinfo>

// testDenseLayout
//
// The 0.8.0 dense-layout contract (docs/PX3_0.8.0_DENSE_LAYOUT.md): content
// reaches the window edges, modules are separated by 1 px seams, containers
// are square, the performance section is hidden (but still built), the six
// modulators are one packed row on VOICE, the MOD page is the modulation
// matrix and fills its panel, every power button is the same size and sits
// centred in its header, the listed objects can be selected and moved by the
// designer model, and the 30 Hz tick does no layout work.

namespace px3tests
{
namespace
{
bool shownIn(const juce::Component& c, const juce::Component& root)
{
    for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
    {
        if (! p->isVisible()) { return false; }
    }
    return true;
}

void walkAll(juce::Component& c, const std::function<void(juce::Component&)>& visit)
{
    for (auto* child : c.getChildren())
    {
        if (child == nullptr) { continue; }
        visit(*child);
        walkAll(*child, visit);
    }
}

bool near(float a, float b, float tolerance = 1.0f) { return std::abs(a - b) <= tolerance; }

juce::Component* byId(juce::Component& root, const juce::String& id)
{
    juce::Component* found = nullptr;
    walkAll(root, [&](juce::Component& c) { if (found == nullptr && c.getComponentID() == id) { found = &c; } });
    return found;
}

// A real drag, delivered to the jack as JUCE does.
void dragJack(juce::Component& jack, juce::Point<int> targetInJack)
{
    auto& desktop = juce::Desktop::getInstance();
    const auto now = juce::Time::getCurrentTime();
    const auto start = jack.getLocalBounds().getCentre().toFloat();
    const auto end = targetInJack.toFloat();
    const auto event = [&](juce::Point<float> position, bool dragged)
    {
        return juce::MouseEvent(desktop.getMainMouseSource(), position,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &jack, &jack, now, start, now, 1, dragged);
    };
    jack.mouseDown(event(start, false));
    jack.mouseDrag(event((start + end) * 0.5f, true));
    jack.mouseDrag(event(end, true));
    jack.mouseUp(event(end, true));
}
} // namespace

void testDenseLayout()
{
    std::printf("\n-- dense layout (0.8.0 single modular surface) --\n");

    PX3SynthAudioProcessor processor;
    processor.setPlayConfigDetails(0, 2, kSampleRate, kBlockSize);
    processor.prepareToPlay(kSampleRate, kBlockSize);
    std::unique_ptr<juce::AudioProcessorEditor> base(processor.createEditor());
    auto* editor = dynamic_cast<PX3SynthAudioProcessorEditor*>(base.get());
    check("Dense_EditorBuilds", editor != nullptr);
    if (editor == nullptr) { return; }
    auto& doc = editor->getSceneDocument();

    // ---- the 1 px seam runs round the window edge too, at min / default / max
    {
        const int sizes[][2] = { { 1100, 700 }, { 1518, 938 }, { 2400, 1400 } };
        juce::StringArray padding, seams;
        for (const auto& size : sizes)
        {
            editor->setSize(size[0], size[1]);
            editor->debugSelectSection(0);
            const auto w = static_cast<float>(size[0]);
            const auto h = static_cast<float>(size[1]);
            const auto where = juce::String(size[0]) + "x" + juce::String(size[1]);
            const auto header = doc.rectOf("header");
            const auto macros = doc.rectOf("macros");
            const auto mods = doc.rectOf("voice.mods");
            const auto amp = doc.rectOf("primary.amp");
            const auto keys = doc.rectOf("keys");
            // Outline + 1 px + outline between modules; 2 px + one outline at
            // the window edge, so the edge seam reads the same width.
            constexpr auto edge = 2.0f;
            if (! near(header.getX(), edge) || ! near(header.getY(), edge) || ! near(header.getRight(), w - edge)
                || ! near(macros.getX(), edge) || ! near(keys.getBottom(), h - edge)
                || ! near(keys.getX(), edge) || ! near(keys.getRight(), w - edge)
                || ! near(mods.getBottom(), keys.getY() - 1.0f)
                || ! near(amp.getRight(), w - edge) || ! near(doc.rectOf("views").getRight(), w - edge))
            {
                padding.add(where);
            }
            // Seams between neighbouring modules.
            const auto seam = [&](const char* a, const char* b, bool horizontal)
            {
                const auto ra = doc.rectOf(a);
                const auto rb = doc.rectOf(b);
                const auto gap = horizontal ? rb.getX() - ra.getRight() : rb.getY() - ra.getBottom();
                if (! near(gap, 1.0f, 0.51f)) { seams.add(where + " " + a + "|" + b + "=" + juce::String(gap, 2)); }
            };
            seam("header", "controls", false);
            seam("macros", "views", true);
            seam("osc.sub", "osc.1", true);
            seam("osc.1", "osc.2", true);
            seam("osc.3", "primary.filter", true);
            seam("filter.1", "filter.2", true);
            seam("primary.filter", "primary.amp", true);
            seam("voice.top", "voice.mods", false);
            seam("lfo.1", "lfo.2", true);
            seam("lfo.3", "env.1", true);
            seam("env.2", "env.3", true);
            seam("mod.routing.patch", "mod.routing.list", true);
            seam("mod.routing.list", "mod.routing.dest", true);
        }
        check("Dense_ContentReachesEveryWindowEdge", padding.isEmpty(), padding.joinIntoString(", "));
        check("Dense_ModulesAreSeparatedByOnePixelSeams", seams.isEmpty(), seams.joinIntoString("; "));
    }

    // ---- square corners ------------------------------------------------------
    check("Dense_ContainersHaveSquareCorners",
          px3::ui::theme::space::panelRadius == 0.0f && px3::ui::theme::space::insetRadius == 0.0f);

    editor->setSize(1518, 938);
    editor->debugSelectSection(0);

    // ---- the performance section: a bottom row, and still retirable ---------
    {
        auto& keyboard = editor->debugPianoKeyboard();
        auto& wheels = editor->debugPerformanceControls();
        const auto shown = editor->debugPerformanceSectionShown() && keyboard.isVisible() && wheels.isVisible()
                           && keyboard.getHeight() > 40 && wheels.getWidth() > 40
                           && near(doc.rectOf("keys").getBottom(), 936.0f);
        check("Dense_KeyboardAndWheelsRunAlongTheBottomEdge", shown,
              keyboard.getBounds().toString() + " " + wheels.getBounds().toString());

        // Wheels and keyboard are one panel: they meet with no seam, and the
        // wheel panels share the keyboard's fill.
        check("Dense_WheelsAndKeyboardReadAsOnePanel",
              wheels.getRight() == keyboard.getX() && wheels.getY() == keyboard.getY()
                  && wheels.getHeight() == keyboard.getHeight(),
              wheels.getBounds().toString() + " | " + keyboard.getBounds().toString());

        // Flip the scene flag: the row goes and the controls take its height.
        juce::String error;
        const auto hid = doc.setVisible("keys", false, error);
        editor->relayoutScene();
        const auto gone = hid && ! editor->debugPerformanceSectionShown() && ! keyboard.isVisible()
                          && ! wheels.isVisible() && near(doc.rectOf("controls").getBottom(), 936.0f);
        doc.setVisible("keys", true, error);
        editor->relayoutScene();
        check("Dense_SceneFlagRetiresThePerformanceRow", gone && keyboard.isVisible(), error);
    }

    // ---- six modulators, one row, on VOICE --------------------------------------
    {
        auto* mods = editor->debugModPanel();
        auto ok = mods != nullptr && shownIn(*mods, *editor);
        juce::String detail;
        int previousRight = -1;
        for (int i = 0; ok && i < 6; ++i)
        {
            auto* card = mods->getCard(i);
            if (card == nullptr || ! shownIn(*card, *editor)) { ok = false; detail = "card " + juce::String(i); break; }
            const auto r = editor->getLocalArea(card, card->getLocalBounds());
            const auto first = editor->getLocalArea(mods->getCard(0), mods->getCard(0)->getLocalBounds());
            ok = r.getY() == first.getY() && r.getHeight() == first.getHeight() && r.getX() > previousRight
                 && r.getWidth() > 150 && r.getY() > doc.rectOf("voice.top").getBottom() - 1;
            previousRight = r.getRight() - 1;
            if (! ok) { detail = "card " + juce::String(i) + " at " + r.toString(); }
        }
        check("Dense_SixModulatorsShareOneRowOnVoice", ok, detail);

        // Each modulator carries its source jack, mirroring its power button.
        auto jacks = true;
        for (int s = 0; s < 6 && mods != nullptr; ++s)
        {
            auto* jack = mods->getCardSocket(s);
            auto* card = mods->getCard(s);
            jacks = jacks && jack != nullptr && card != nullptr && jack->isVisible()
                    && card->getBounds().contains(jack->getBounds())
                    && jack->getWidth() == static_cast<int>(px3::ui::theme::space::powerButton);
        }
        check("Dense_EveryModulatorHasItsSourceJack", jacks);
    }

    // ---- power buttons: one size, centred in the header, >= 4 px clear --------
    {
        juce::StringArray faults;
        auto counted = 0;
        for (int section : { 0, 4 })
        {
            editor->debugSelectSection(section);
            walkAll(*editor, [&](juce::Component& c)
            {
                auto* power = dynamic_cast<px3::ui::BypassButton*>(&c);
                if (power == nullptr || ! shownIn(c, *editor) || c.getWidth() <= 0) { return; }
                auto* card = c.getParentComponent();
                if (card == nullptr || card == editor) { return; }
                ++counted;
                const auto side = static_cast<int>(px3::ui::theme::space::powerButton);
                const auto band = px3::ui::theme::space::minTitleBand;
                const auto top = static_cast<float>(c.getY());
                const auto bottomClear = band - static_cast<float>(c.getBottom());
                // Centred in the band below the accent stripe, then dropped by
                // powerNudge: clear of the stripe above and the divider below.
                const auto stripe = px3::ui::theme::space::accentBar;
                const auto nudge = px3::ui::theme::space::powerNudge;
                if (c.getWidth() != side || c.getHeight() != side || top - stripe < 4.0f || bottomClear < 2.0f
                    || std::abs((top - stripe) - bottomClear - 2.0f * nudge) > 1.0f)
                {
                    faults.add(card->getName() + juce::String(" ") + c.getBounds().toString());
                }
            });
        }
        editor->debugSelectSection(0);
        check("Dense_EveryPowerButtonIsOneSizeAndCentredInItsHeader", counted >= 12 && faults.isEmpty(),
              juce::String(counted) + " buttons; " + faults.joinIntoString(", "));
    }

    // ---- the route list fits its column at every size -------------------------
    // The scene places the ROUTES column itself, so the list must refit when
    // the column changes size - it kept a stale width once, and the right-hand
    // destination plugs were drawn half outside the column.
    {
        editor->debugSelectSection(1);
        juce::StringArray over;
        const int sizes[][2] = { { 2400, 1400 }, { 1518, 938 }, { 1100, 700 } };
        for (const auto& size : sizes)
        {
            editor->setSize(size[0], size[1]);
            auto* routes = byId(*editor, "mod.routing.list");
            juce::Viewport* port = nullptr;
            if (routes != nullptr)
            {
                walkAll(*routes, [&](juce::Component& c) { if (port == nullptr) { port = dynamic_cast<juce::Viewport*>(&c); } });
            }
            auto* list = port != nullptr ? port->getViewedComponent() : nullptr;
            if (list == nullptr || list->getWidth() > port->getMaximumVisibleWidth())
            {
                over.add(juce::String(size[0]) + "x" + juce::String(size[1]) + ": "
                         + (list != nullptr ? juce::String(list->getWidth()) + " in " + juce::String(port->getMaximumVisibleWidth())
                                            : juce::String("no list")));
            }
        }
        editor->setSize(1518, 938);
        check("Dense_RouteListFitsItsColumnAtEverySize", over.isEmpty(), over.joinIntoString(", "));
    }

    // ---- the MOD page is the modulation matrix --------------------------------
    {
        using namespace px3::ui::modrouting;
        editor->debugSelectSection(1);
        editor->debugRefreshModRouting();
        auto* panel = editor->debugModRoutingPanel();
        const auto views = doc.rectOf("views").toNearestInt();
        check("Dense_ModMatrixFillsItsPanel",
              panel != nullptr && panel->isVisible() && std::abs(panel->getX() - views.getX()) <= 1
                  && std::abs(panel->getRight() - views.getRight()) <= 1
                  && std::abs(panel->getBottom() - views.getBottom()) <= 1
                  && panel->getList().getHeight() > 300 && panel->getDestinations().getWidth() >= 250
                  && editor->debugModPanel() != nullptr && ! editor->debugModPanel()->isVisible());

        auto expected = 0;
        for (const auto& entry : processor.getParameterCatalog().entries())
        {
            expected += processor.isGraphDestination(entry.id) ? 1 : 0;
        }
        check("Dense_MatrixOffersEveryCatalogDestination",
              panel != nullptr && expected > 100 && panel->getDestinationCount() == expected,
              juce::String(panel != nullptr ? panel->getDestinationCount() : -1) + " of " + juce::String(expected));

        if (panel != nullptr)
        {
            // Patching by dropping a jack on a destination row of the matrix.
            const juce::String target = "voice.filter2.resonance";
            const auto row = panel->revealDestination(target);
            auto* sourceJack = byId(panel->getPatchView(), "mod.source.4");
            if (sourceJack != nullptr && ! row.isEmpty())
            {
                const auto point = sourceJack->getLocalPoint(&panel->getDestinations(), row.getCentre());
                dragJack(*sourceJack, point);
            }
            auto dropped = false;
            for (const auto& r : collectRoutes(processor)) { dropped = dropped || (r.source == 4 && r.destination == target); }
            check("Dense_DropOnAMatrixDestinationCreatesTheRoute", dropped && ! row.isEmpty());

            // Click-to-patch.
            panel->armSource(0);
            const auto slot = panel->patchArmedSourceTo("voice.osc2.tuning.cents");
            panel->armSource(-1);
            check("Dense_ClickToPatchCreatesTheRoute",
                  slot >= 0 && processor.getGraphRoute(slot).source == 0
                      && processor.getGraphRoute(slot).destination == "voice.osc2.tuning.cents");

            // Fine amount: arrow keys step 1 %, Shift steps 0.1 %.
            editor->debugRefreshModRouting();
            panel->refresh();
            juce::Slider* amount = nullptr;
            auto routeSlot = -1;
            for (int i = 0; i < panel->getRowCount() && amount == nullptr; ++i)
            {
                if (panel->getRoutes()[static_cast<std::size_t>(i)].kind != RouteInfo::Kind::graph) { continue; }
                routeSlot = panel->getRoutes()[static_cast<std::size_t>(i)].slot;
                for (auto* child : panel->getRow(i)->getChildren())
                {
                    if (child->getComponentID() == "mod.routing.depth") { amount = dynamic_cast<juce::Slider*>(child); }
                }
            }
            auto fine = false;
            if (amount != nullptr && routeSlot >= 0)
            {
                auto& depth = processor.getGraphRouteDepthParam(routeSlot);
                const auto before = depth.get();
                amount->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
                const auto coarse = depth.get() - before;
                amount->keyPressed(juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::shiftModifier, 0));
                const auto fineStep = depth.get() - (before + coarse);
                fine = std::abs(coarse - 0.01f) < 1.0e-4f && std::abs(fineStep + 0.001f) < 1.0e-4f;
            }
            check("Dense_RouteAmountTakesFineSteps", fine);

            const auto all = panel->getDestinationCount();
            panel->setDestinationFilter("cutoff");
            const auto filtered = panel->revealDestination("voice.filter1.cutoff");
            panel->setDestinationFilter({});
            check("Dense_DestinationSearchFindsCutoff", all > 0 && ! filtered.isEmpty());
        }
    }

    // ---- the designer model can select and move the listed objects -----------
    {
        namespace le = px3::ui::layoutedit;
        editor->debugSelectSection(0);
        const auto root = editor->getLocalBounds().toFloat();
        juce::StringArray failed;
        for (const auto* id : { "view.osc", "voice.mods", "osc.1", "filter.1", "lfo.1", "env.1", "amp.envelope",
                                "osc.1.coarse.knob", "mod.routing", "mod.routing.dest" })
        {
            editor->debugSelectSection(juce::String(id).startsWith("mod.") ? 1 : 0);
            const auto rect = doc.rectOf(id);
            // Selectable: a click inside lands on it or a node inside it (the
            // designer then walks up with Parent / Alt-click). Eligibility is
            // the designer's rule: the nearest bound component must be on screen.
            const auto eligible = [&](int index)
            {
                if (! doc.isShown(index)) { return false; }
                for (auto i = index; i >= 0; i = doc.indexOf(doc.getNodes()[static_cast<std::size_t>(i)].parentId))
                {
                    if (auto* c = editor->getSceneBinding().componentFor(doc.getNodes()[static_cast<std::size_t>(i)].id))
                    {
                        return shownIn(*c, *editor);
                    }
                }
                return true;
            };
            const auto hit = le::hitTest(doc, rect.getCentre(), eligible);
            auto reaches = false;
            for (auto index = hit; juce::isPositiveAndBelow(index, static_cast<int>(doc.getNodes().size()));)
            {
                const auto& node = doc.getNodes()[static_cast<std::size_t>(index)];
                if (node.id == id) { reaches = true; break; }
                index = doc.indexOf(node.parentId);
            }
            // Movable/resizable: a right-edge drag changes its resolved rect and
            // undo puts it back.
            le::Drag drag;
            juce::String error;
            const auto from = juce::Point<float>(rect.getRight(), rect.getCentreY());
            auto moved = le::beginDrag(doc, drag, id, le::Handle::right, from, root)
                         && le::dragTo(doc, drag, from.translated(-12.0f, 0.0f), 0.0f, error);
            le::endDrag(doc, drag);
            doc.resolve(root);
            const auto changed = doc.rectOf(id) != rect;
            doc.undo();
            doc.resolve(root);
            const auto restored = doc.rectOf(id) == rect;
            if (! reaches || ! moved || ! changed || ! restored)
            {
                failed.add(juce::String(id) + (reaches ? "" : " hit:" + (hit >= 0 ? doc.getNodes()[static_cast<std::size_t>(hit)].id : juce::String("none"))) + (moved ? "" : " drag") + (changed ? "" : " same")
                           + (restored ? "" : " undo"));
            }
        }
        editor->relayoutScene();
        check("Dense_DesignerCanSelectAndMoveTheListedObjects", failed.isEmpty(), failed.joinIntoString(", "));
    }

    // ---- no per-frame layout ---------------------------------------------------
    {
        editor->debugSelectSection(0);
        editor->debugTimerTick();
        const auto before = doc.getResolveCount();
        for (int i = 0; i < 30; ++i) { editor->debugTimerTick(); }
        check("Dense_TimerTickDoesNoLayoutWork", doc.getResolveCount() == before,
              juce::String(doc.getResolveCount() - before) + " resolves in 30 ticks");
    }

    // ---- a dropdown choice survives the refresh tick ---------------------------
    // Choosing from a dropdown's menu selects the item and tells the attachment
    // ASYNCHRONOUSLY. A refresh tick landing in between read the parameter -
    // still the old value - and pushed it back into the box, so the queued
    // notification found nothing to do and the choice was lost. With every
    // panel on the VOICE page refreshing each tick this happened often enough
    // to look like clicks on menu items being ignored.
    {
        juce::StringArray lost;
        auto examined = 0;
        for (const int section : { 0, 4 })
        {
            editor->debugSelectSection(section);
            editor->debugTimerTick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            std::vector<juce::ComboBox*> boxes;
            walkAll(*editor, [&](juce::Component& c)
            {
                if (auto* box = dynamic_cast<juce::ComboBox*>(&c))
                {
                    if (shownIn(c, *editor) && box->isEnabled() && box->getNumItems() > 1) { boxes.push_back(box); }
                }
            });
            for (auto* box : boxes)
            {
                const auto before = box->getSelectedItemIndex();
                const auto choice = before == 1 ? 0 : 1;
                box->setSelectedItemIndex(choice, juce::sendNotificationAsync); // what the menu does
                editor->debugTimerTick();                                       // a refresh lands first
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
                editor->debugTimerTick();
                ++examined;
                if (box->getSelectedItemIndex() != choice)
                {
                    const auto* parent = box->getParentComponent();
                    lost.add(juce::String(parent != nullptr ? typeid(*parent).name() : "?") + ":" + box->getItemText(0) + "/"
                             + box->getItemText(1));
                }
            }
        }
        editor->debugSelectSection(0);
        check("Dense_DropdownChoiceSurvivesTheRefreshTick", examined > 8 && lost.isEmpty(),
              juce::String(examined) + " dropdowns; lost: " + lost.joinIntoString(", "));
    }

    // ---- an enabled sub osc draws its wave in its own colour -------------------
    // The wave and its frame take the card's identity colour once the sub is on,
    // as every other card's graph does - grey is what a bypassed card looks like.
    {
        editor->debugSelectSection(0);
        processor.getSubOscEnabledParam().setValueNotifyingHost(1.0f);
        editor->debugTimerTick();
        SubOscComponent* sub = nullptr;
        walkAll(*editor, [&](juce::Component& c)
        {
            if (auto* s = dynamic_cast<SubOscComponent*>(&c)) { sub = s; }
        });
        auto tinted = 0;
        auto grey = 0;
        juce::String accentText;
        if (sub != nullptr)
        {
            const auto graph = sub->getGraphSlot().getBounds();
            const auto image = sub->createComponentSnapshot(sub->getLocalBounds(), true, 1.0f);
            for (int y = graph.getY(); y < graph.getBottom(); ++y)
            {
                for (int x = graph.getX(); x < graph.getRight(); ++x)
                {
                    const auto p = image.getPixelAt(x, y);
                    const auto bright = p.getBrightness() > 0.35f;
                    if (! bright) { continue; }
                    // Sub Osc's identity is blue in both the shipped config and
                    // the defaults (pale there), so its wave leans blue.
                    if (p.getBlue() > p.getRed() + 12) { ++tinted; } else { ++grey; }
                }
            }
        }
        check("Dense_EnabledSubOscGraphTakesItsColour", sub != nullptr && tinted > grey * 4,
              juce::String(tinted) + " tinted against " + juce::String(grey) + " grey bright pixels");
        processor.getSubOscEnabledParam().setValueNotifyingHost(0.0f);
        editor->debugTimerTick();
    }
}
} // namespace px3tests
