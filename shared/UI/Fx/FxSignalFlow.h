#pragma once

#include <JuceHeader.h>

#include <array>
#include <functional>
#include <vector>

class UIConfig;

namespace px3::ui
{

// The FX chain as a row of nodes with the signal running left to right.
//
// This is the ONLY place the chain order can be changed by hand. The FX cards
// below it are editors, not ordering controls: they are large, they scroll, and
// dragging one across a wrapped grid to change a linear order is a poor match
// between the gesture and the thing it edits.
//
// Ordering is not stored here. The strip is handed an order to display and
// reports the order the user asked for; the processor remains the single
// authority. Keeping a copy here is how a UI ends up disagreeing with its DSP.
class FxSignalFlow final : public juce::Component,
                           private juce::Timer
{
public:
    struct Node
    {
        int id { 0 };            // the processor's stage id
        juce::String name;
        juce::Colour accent;
        bool active { true };    // bypassed nodes are drawn dimmed
    };

    FxSignalFlow();
    ~FxSignalFlow() override;

    void setUIConfig(std::shared_ptr<const UIConfig> config);

    // Everything about how the strip is drawn and spaced, read from
    // "fx.signalFlow" in UIConfig.json. Exposed so tests can assert that a
    // config change actually reaches the layout.
    struct Style
    {
        int nodeGap { 26 };            // room for the connector between nodes
        int minNodeWidth { 48 };
        int insetX { 4 };
        int insetY { 6 };
        float cornerRadius { 6.0f };
        float reflowRate { 0.35f };
        float fontSize { 11.0f };
        float accentBarHeight { 3.0f };
        float hoverBrighten { 0.14f };
        float dragBrighten { 0.28f };
        float inactiveSaturation { 0.12f };
        juce::Colour nodeColour { juce::Colour::fromRGB(30, 32, 37) };
        juce::Colour textColour { juce::Colour::fromRGB(238, 241, 246) };
        juce::Colour inactiveTextColour { juce::Colour::fromRGB(150, 154, 162) };
        juce::Colour connectorColour { juce::Colour::fromRGBA(255, 255, 255, 60) };
        juce::Colour borderColour { juce::Colour::fromRGBA(255, 255, 255, 45) };
        juce::Colour dropHighlightColour { juce::Colour::fromRGBA(255, 255, 255, 26) };
    };

    const Style& style() const noexcept { return nodeStyle; }

    // Slot rectangles in chain order - the resting positions of the nodes.
    const std::vector<juce::Rectangle<float>>& slotBounds() const noexcept { return slots; }
    const std::vector<Node>& nodeList() const noexcept { return nodes; }

    // The nodes, in chain order. Called whenever the model changes - by a drag
    // here, by a preset load, or by the host restoring a session.
    void setNodes(std::vector<Node> nodes);
    // Bypass state only, which changes far more often than the order does.
    void setNodeActive(int id, bool active);

    // Reports the order the user dragged the chain into. The strip does not
    // apply it: the owner writes it to the processor, which then feeds the new
    // order back through setNodes.
    std::function<void(const std::vector<int>&)> onOrderChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    void rebuildSlots();
    int nodeIndexAt(juce::Point<int> position) const;
    int insertionIndexFor(int draggedCentreX) const;
    void drawNode(juce::Graphics& g, const Node& node, juce::Rectangle<float> bounds,
                  bool dragged, bool hovered) const;
    void drawConnector(juce::Graphics& g, juce::Rectangle<float> from, juce::Rectangle<float> to) const;

    std::vector<Node> nodes;
    // Where each node sits when nothing is being dragged. Slots are positions
    // in the row; nodes move between them.
    std::vector<juce::Rectangle<float>> slots;
    // Where each node is drawn right now. Animated toward its slot so that
    // neighbours slide aside rather than jumping.
    std::vector<juce::Rectangle<float>> current;

    std::shared_ptr<const UIConfig> uiConfig;
    Style nodeStyle;

    int draggedIndex { -1 };
    int pressedIndex { -1 };
    int hoveredIndex { -1 };
    int insertionIndex { -1 };
    juce::Point<int> dragStart;
    int dragOffsetX { 0 };
    bool dragHasMoved { false };

    // A press has to travel before it becomes a drag, or every click on a node
    // would nudge the chain.
    static constexpr int kDragThresholdPx = 4;
};

} // namespace px3::ui
