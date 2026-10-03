#pragma once

// In-window modulation routing (0.8.0). Replaces the floating ROUTES window.
//
// - ModSourceSocket: a jack for one modulation source (LFO 1-3, ENV 1-3,
//   MACRO 1-5). Drag it onto any modulatable knob in the editor to create a
//   route. Sockets sit in the always-visible patch bar, on the LFO/ENV cards
//   and in the MOD page's patch view; they all drive one ModDragController.
// - ModDragController: owns the drag (cable overlay over the whole editor),
//   finds the drop target (the deepest visible parameter knob, or a patch-view
//   destination node), spring-loads the section tabs and creates the route.
// - ModRoutingPanel: the MOD page's routing view: a patch view with cables
//   from source sockets to destination nodes, and a list of every route with
//   depth, polarity, curve and delete.
//
// Nothing here opens a window: everything lives in the editor's own tree.

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <functional>
#include <memory>
#include <vector>

namespace px3::ui::modrouting
{
// Property on any component that accepts a dropped source: its parameter id.
inline const juce::Identifier destinationProperty { "px3ModDestination" };
// Property the knob look-and-feel reads: an array of [argb, lo, hi] triplets,
// the normalised range each route can sweep the knob across.
inline const juce::Identifier& ringsProperty = px3::knob_properties::modRings;

constexpr int kSourceCount = PX3SynthAudioProcessor::kLfoSourceCount
                           + PX3SynthAudioProcessor::kEnvelopeSourceCount
                           + PX3SynthAudioProcessor::kMacroCount;

juce::Colour sourceColour(int source);
juce::String sourceShortName(int source);
bool sourceIsBipolar(int source);

struct RouteInfo
{
    // graph: an editable slot of the modulation matrix. card: an LFO/ENV card's
    // ASSIGN menu. macro: a macro destination (assigned from the macro strip).
    enum class Kind { graph, card, macro };
    Kind kind { Kind::graph };
    int slot { -1 };     // graph slot; for card the source; for macro the macro index
    int source { -1 };   // graph source index (0..kSourceCount-1)
    juce::String destination;
    float depth { 0.0f };
    px3::synth::ModulationPolarity polarity { px3::synth::ModulationPolarity::native };
    px3::synth::ModulationCurve curve { px3::synth::ModulationCurve::linear };

    juce::String key() const;   // identity for selection and list diffing
};

std::vector<RouteInfo> collectRoutes(const PX3SynthAudioProcessor& processor);
// Creates (or finds) a graph route; returns its slot or -1 with error set.
int createRoute(PX3SynthAudioProcessor& processor, int source, const juce::String& destination,
                juce::String& error, float initialDepth = 0.5f);
bool removeRoute(PX3SynthAudioProcessor& processor, const RouteInfo& route);
juce::String destinationName(const PX3SynthAudioProcessor& processor, const juce::String& id);
// The normalised range [lo, hi] a route sweeps a parameter sitting at `base`.
juce::Range<float> sweepRange(const RouteInfo& route, float base);

class ModDragController;

class ModSourceSocket final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ModSourceSocket(ModDragController& controllerIn, int sourceIn, bool showLabelIn = true);
    int getSource() const noexcept { return source; }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    juce::Rectangle<float> jackBounds() const;
    void setRouteCount(int count);

private:
    ModDragController& controller;
    int source;
    bool showLabel;
    int routeCount { 0 };
};

class ModDragController final
{
public:
    struct Host
    {
        // Selects a section tab when a drag dwells on it (spring-loaded tabs).
        std::function<int(juce::Point<int>)> sectionTabAt;
        std::function<void(int)> selectSection;
        // Called after a route was created from a drop.
        std::function<void(const RouteInfo&)> routeCreated;
    };

    ModDragController(PX3SynthAudioProcessor& processorIn, juce::Component& rootIn, Host hostIn);
    ~ModDragController();

    void begin(int source, juce::Point<float> rootPosition);
    void move(juce::Point<float> rootPosition);
    // Returns the created route's slot, or -1 when dropped on nothing valid.
    int end(juce::Point<float> rootPosition);
    void cancel();
    bool isDragging() const noexcept { return dragSource >= 0; }
    int getDragSource() const noexcept { return dragSource; }

    // The destination under a point of the root, or empty.
    juce::String destinationAt(juce::Point<int> rootPosition, juce::Component** target = nullptr) const;
    juce::Component& getRoot() noexcept { return root; }
    PX3SynthAudioProcessor& getProcessor() noexcept { return processor; }
    juce::String lastError;

private:
    class Overlay;
    PX3SynthAudioProcessor& processor;
    juce::Component& root;
    Host host;
    std::unique_ptr<Overlay> overlay;
    int dragSource { -1 };
    juce::Point<float> start, current;
    juce::Component::SafePointer<juce::Component> hoverTarget;
    int hoverTab { -1 };
    double hoverTabSince { 0.0 };
};

// Always-visible strip of the 11 source jacks (the scene's "patchbar" node).
class ModPatchBar final : public juce::Component
{
public:
    explicit ModPatchBar(ModDragController& controller);
    void paint(juce::Graphics& g) override;
    void resized() override;   // fallback when not scene-managed
    ModSourceSocket& getSocket(int source) { return *sockets[static_cast<std::size_t>(source)]; }
    juce::Label& getTitle() { return title; }
    void refresh(const std::vector<RouteInfo>& routes);

private:
    juce::Label title;
    std::vector<std::unique_ptr<ModSourceSocket>> sockets;
};

class ModRoutingPanel final : public juce::Component
{
public:
    explicit ModRoutingPanel(ModDragController& controller);
    ~ModRoutingPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;   // fallback when not scene-managed
    bool keyPressed(const juce::KeyPress& key) override;

    // 30 Hz from the editor while visible; rebuilds rows only on a change.
    void refresh();
    void select(const juce::String& routeKey);
    juce::String getSelectedKey() const { return selectedKey; }
    bool removeSelected();

    juce::Component& getTitle();
    juce::Component& getPatchView();
    juce::Component& getList();
    int getRowCount() const;
    juce::Component* getRow(int index);
    const std::vector<RouteInfo>& getRoutes() const { return routes; }

private:
    class PatchView;
    class Row;
    class ListContent;
    void rebuildRows();

    ModDragController& controller;
    PX3SynthAudioProcessor& processor;
    std::vector<RouteInfo> routes;
    juce::String signature;
    juce::String selectedKey;
    juce::Label title;
    juce::Label hint;
    std::unique_ptr<PatchView> patchView;
    juce::Viewport listViewport;
    std::unique_ptr<ListContent> listContent;
};
} // namespace px3::ui::modrouting
