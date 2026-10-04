// The editor's side of the in-window modulation routing: building the drag
// controller, the patch bar and the MOD page's routing panel, and the 30 Hz
// pass that keeps sockets, the panel and the knob rings in step with the
// processor. See docs/PX3_0.8.0_MODULATION_UI.md.

#include "PluginEditor.h"
#include "EditorSections.h"

using namespace px3::ui::modrouting;
using px3::ui::kSectionMod;

void PX3SynthAudioProcessorEditor::buildModRouting()
{
    ModDragController::Host host;
    host.sectionTabAt = [this](juce::Point<int> point)
    {
        if (topMenuBar == nullptr) { return -1; }
        for (int section = 0; section < 6; ++section)
        {
            auto& button = topMenuBar->getSectionButton(section);
            if (button.isVisible() && button.getLocalBounds().contains(button.getLocalPoint(this, point)))
            {
                return section;
            }
        }
        return -1;
    };
    host.selectSection = [this](int section)
    {
        if (section != selectedTopMenuSection) { applyTopMenuSectionSelection(section, true); }
    };
    host.routeCreated = [this](const RouteInfo& route)
    {
        refreshModRouting();
        if (modRoutingPanel != nullptr)
        {
            RouteInfo key = route;
            key.kind = RouteInfo::Kind::graph;
            modRoutingPanel->select(key.key());
        }
    };
    modDragController = std::make_unique<ModDragController>(audioProcessor, *this, std::move(host));

    modPatchBar = std::make_unique<ModPatchBar>(*modDragController);
    addAndMakeVisible(*modPatchBar);

    modRoutingPanel = std::make_unique<ModRoutingPanel>(*modDragController);
    addChildComponent(*modRoutingPanel);

    if (modPanel != nullptr) { modPanel->attachModSources(*modDragController); }
}

void PX3SynthAudioProcessorEditor::refreshModRouting()
{
    if (modDragController == nullptr) { return; }

    modRoutes = collectRoutes(audioProcessor);
    if (modPatchBar != nullptr && modPatchBar->isVisible()) { modPatchBar->refresh(modRoutes); }
    if (modPanel != nullptr && modPanel->isVisible()) { modPanel->refreshModSources(modRoutes); }
    if (modRoutingPanel != nullptr && modRoutingPanel->isVisible()) { modRoutingPanel->refresh(); }

    // The rings every routed knob draws, keyed by parameter id. Built once
    // per tick from at most a few dozen routes; knobs without routes get none.
    modRingsByParameter.clear();
    for (const auto& route : modRoutes)
    {
        auto* parameter = audioProcessor.findRangedParameterById(route.destination);
        if (parameter == nullptr) { continue; }
        const auto sweep = sweepRange(route, parameter->getValue());
        auto& rings = modRingsByParameter[route.destination];
        if (! rings.isArray()) { rings = juce::Array<juce::var>(); }
        rings.append(static_cast<juce::int64>(sourceColour(route.source).getARGB()));
        rings.append(static_cast<double>(sweep.getStart()));
        rings.append(static_cast<double>(sweep.getEnd()));
    }
}

juce::var PX3SynthAudioProcessorEditor::modRingsFor(const juce::String& parameterId) const
{
    const auto found = modRingsByParameter.find(parameterId);
    return found != modRingsByParameter.end() ? found->second : juce::var();
}
