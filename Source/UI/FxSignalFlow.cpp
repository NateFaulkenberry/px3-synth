#include "FxSignalFlow.h"

#include "FxChainLayout.h"
#include "UIConfig.h"

namespace px3::ui
{
namespace
{
float approach(float current, float target, float rate)
{
    return current + (target - current) * rate;
}
} // namespace

FxSignalFlow::FxSignalFlow()
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

FxSignalFlow::~FxSignalFlow() = default;

void FxSignalFlow::setUIConfig(std::shared_ptr<const UIConfig> config)
{
    uiConfig = std::move(config);

    if (uiConfig != nullptr)
    {
        const Style defaults;
        nodeStyle.nodeGap = uiConfig->getInt("fx.signalFlow.nodeGap", defaults.nodeGap);
        nodeStyle.minNodeWidth = uiConfig->getInt("fx.signalFlow.minNodeWidth", defaults.minNodeWidth);
        nodeStyle.insetX = uiConfig->getInt("fx.signalFlow.insetX", defaults.insetX);
        nodeStyle.insetY = uiConfig->getInt("fx.signalFlow.insetY", defaults.insetY);
        nodeStyle.cornerRadius = uiConfig->getFloat("fx.signalFlow.cornerRadius", defaults.cornerRadius);
        nodeStyle.reflowRate = uiConfig->getFloat("fx.signalFlow.reflowRate", defaults.reflowRate);
        nodeStyle.fontSize = uiConfig->getFloat("fx.signalFlow.fontSize", defaults.fontSize);
        nodeStyle.accentBarHeight = uiConfig->getFloat("fx.signalFlow.accentBarHeight", defaults.accentBarHeight);
        nodeStyle.hoverBrighten = uiConfig->getFloat("fx.signalFlow.hoverBrighten", defaults.hoverBrighten);
        nodeStyle.dragBrighten = uiConfig->getFloat("fx.signalFlow.dragBrighten", defaults.dragBrighten);
        nodeStyle.inactiveSaturation = uiConfig->getFloat("fx.signalFlow.inactiveSaturation", defaults.inactiveSaturation);
        nodeStyle.nodeColour = uiConfig->getColour("fx.signalFlow.nodeColour", defaults.nodeColour);
        nodeStyle.textColour = uiConfig->getColour("fx.signalFlow.textColour", defaults.textColour);
        nodeStyle.inactiveTextColour = uiConfig->getColour("fx.signalFlow.inactiveTextColour", defaults.inactiveTextColour);
        nodeStyle.connectorColour = uiConfig->getColour("fx.signalFlow.connectorColour", defaults.connectorColour);
        nodeStyle.borderColour = uiConfig->getColour("fx.signalFlow.borderColour", defaults.borderColour);
        nodeStyle.dropHighlightColour = uiConfig->getColour("fx.signalFlow.dropHighlightColour", defaults.dropHighlightColour);
    }

    rebuildSlots();
    current = slots;
    repaint();
}

void FxSignalFlow::setNodes(std::vector<Node> newNodes)
{
    const auto sameOrder = newNodes.size() == nodes.size()
                           && std::equal(newNodes.begin(), newNodes.end(), nodes.begin(),
                                         [](const Node& a, const Node& b) { return a.id == b.id; });

    nodes = std::move(newNodes);
    rebuildSlots();

    // A reorder animates from where the nodes are; a first fill or a changed
    // set of effects simply appears, because there is no previous position for
    // the new nodes to slide from.
    if (! sameOrder || current.size() != nodes.size())
    {
        if (current.size() != nodes.size())
        {
            current = slots;
        }
        startTimerHz(60);
    }

    repaint();
}

void FxSignalFlow::setNodeActive(int id, bool active)
{
    for (auto& node : nodes)
    {
        if (node.id == id && node.active != active)
        {
            node.active = active;
            repaint();
        }
    }
}

void FxSignalFlow::rebuildSlots()
{
    const auto area = getLocalBounds().toFloat().reduced(static_cast<float>(nodeStyle.insetX),
                                                         static_cast<float>(nodeStyle.insetY));
    slots = signalFlowSlots(area, static_cast<int>(nodes.size()), nodeStyle.nodeGap, nodeStyle.minNodeWidth);

    if (current.size() != slots.size())
    {
        current = slots;
    }
}

void FxSignalFlow::resized()
{
    rebuildSlots();
    current = slots;
    repaint();
}

int FxSignalFlow::nodeIndexAt(juce::Point<int> position) const
{
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        if (current[i].contains(position.toFloat()))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int FxSignalFlow::insertionIndexFor(int draggedCentreX) const
{
    return insertionIndexForCentre(slots, static_cast<float>(draggedCentreX));
}

void FxSignalFlow::mouseDown(const juce::MouseEvent& event)
{
    pressedIndex = nodeIndexAt(event.getPosition());
    dragStart = event.getPosition();
    dragHasMoved = false;

    if (pressedIndex >= 0)
    {
        dragOffsetX = event.getPosition().x - static_cast<int>(current[static_cast<std::size_t>(pressedIndex)].getX());
    }
}

void FxSignalFlow::mouseDrag(const juce::MouseEvent& event)
{
    if (pressedIndex < 0)
    {
        return;
    }

    if (! dragHasMoved)
    {
        // Below the threshold this is still a click. Without it, the tremor in
        // an ordinary press would reorder the chain.
        if (event.getPosition().getDistanceFrom(dragStart) < kDragThresholdPx)
        {
            return;
        }
        dragHasMoved = true;
        draggedIndex = pressedIndex;
    }

    auto& dragged = current[static_cast<std::size_t>(draggedIndex)];
    const auto newX = juce::jlimit(getLocalBounds().toFloat().getX(),
                                   juce::jmax(0.0f, static_cast<float>(getWidth()) - dragged.getWidth()),
                                   static_cast<float>(event.getPosition().x - dragOffsetX));
    dragged.setX(newX);

    const auto target = insertionIndexFor(static_cast<int>(dragged.getCentreX()));
    if (target != draggedIndex && target >= 0 && target < static_cast<int>(nodes.size()))
    {
        // Reorder as the node passes, rather than only on release: the chain
        // reflows under the cursor so the result is visible before committing
        // to it.
        auto moved = nodes[static_cast<std::size_t>(draggedIndex)];
        nodes.erase(nodes.begin() + draggedIndex);
        nodes.insert(nodes.begin() + target, moved);

        auto movedBounds = current[static_cast<std::size_t>(draggedIndex)];
        current.erase(current.begin() + draggedIndex);
        current.insert(current.begin() + target, movedBounds);

        draggedIndex = target;
    }

    insertionIndex = draggedIndex;
    startTimerHz(60);
    repaint();
}

void FxSignalFlow::mouseUp(const juce::MouseEvent&)
{
    const auto reordered = dragHasMoved && draggedIndex >= 0;

    pressedIndex = -1;
    draggedIndex = -1;
    insertionIndex = -1;
    dragHasMoved = false;

    if (reordered && onOrderChanged != nullptr)
    {
        std::vector<int> order;
        order.reserve(nodes.size());
        for (const auto& node : nodes)
        {
            order.push_back(node.id);
        }
        // Reported, not applied. The processor owns the order; this strip finds
        // out what the new one is when it is handed back through setNodes.
        onOrderChanged(order);
    }

    startTimerHz(60);
    repaint();
}

void FxSignalFlow::mouseMove(const juce::MouseEvent& event)
{
    const auto index = nodeIndexAt(event.getPosition());
    if (index != hoveredIndex)
    {
        hoveredIndex = index;
        repaint();
    }
}

void FxSignalFlow::mouseExit(const juce::MouseEvent&)
{
    if (hoveredIndex != -1)
    {
        hoveredIndex = -1;
        repaint();
    }
}

void FxSignalFlow::timerCallback()
{
    // Nodes ease toward their slots so a reorder reads as movement rather than
    // as a jump. The dragged node is exempt: it follows the mouse.
    auto settled = true;
    for (std::size_t i = 0; i < current.size() && i < slots.size(); ++i)
    {
        if (static_cast<int>(i) == draggedIndex)
        {
            continue;
        }

        const auto target = slots[i];
        const auto next = juce::Rectangle<float>(approach(current[i].getX(), target.getX(), nodeStyle.reflowRate),
                                                 target.getY(), target.getWidth(), target.getHeight());
        if (std::abs(next.getX() - target.getX()) > 0.5f)
        {
            settled = false;
        }
        current[i] = next;
    }

    if (settled && draggedIndex < 0)
    {
        current = slots;
        stopTimer();
    }
    repaint();
}

void FxSignalFlow::drawConnector(juce::Graphics& g, juce::Rectangle<float> from, juce::Rectangle<float> to) const
{
    const auto y = from.getCentreY();
    const auto x1 = from.getRight() + 4.0f;
    const auto x2 = to.getX() - 4.0f;
    if (x2 <= x1)
    {
        return;
    }

    // A line into an arrowhead. The arrow is what says which way the audio
    // travels - without it a row of boxes is just a list.
    g.setColour(nodeStyle.connectorColour);
    g.drawLine(x1, y, x2 - 5.0f, y, 1.4f);

    juce::Path head;
    head.startNewSubPath(x2, y);
    head.lineTo(x2 - 6.0f, y - 4.0f);
    head.lineTo(x2 - 6.0f, y + 4.0f);
    head.closeSubPath();
    g.setColour(nodeStyle.connectorColour.withMultipliedAlpha(1.6f));
    g.fillPath(head);
}

void FxSignalFlow::drawNode(juce::Graphics& g, const Node& node, juce::Rectangle<float> bounds,
                            bool dragged, bool hovered) const
{
    const auto accent = node.active ? node.accent : node.accent.withSaturation(nodeStyle.inactiveSaturation);

    if (dragged)
    {
        // Lifted: a shadow and a brighter face, so the node being moved is
        // unmistakable against the ones reflowing around it.
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 140));
        g.fillRoundedRectangle(bounds.translated(0.0f, 3.0f), nodeStyle.cornerRadius);
    }

    auto face = nodeStyle.nodeColour;
    if (dragged)      face = face.brighter(nodeStyle.dragBrighten);
    else if (hovered) face = face.brighter(nodeStyle.hoverBrighten);

    g.setColour(face);
    g.fillRoundedRectangle(bounds, nodeStyle.cornerRadius);

    // A colour bar along the top ties the node to its card below.
    g.setColour(accent.withAlpha(node.active ? 0.95f : 0.35f));
    g.fillRoundedRectangle(bounds.withHeight(nodeStyle.accentBarHeight).reduced(2.0f, 0.0f), 1.5f);

    g.setColour(dragged ? nodeStyle.borderColour.withMultipliedAlpha(2.0f) : nodeStyle.borderColour);
    g.drawRoundedRectangle(bounds.reduced(0.5f), nodeStyle.cornerRadius, 1.0f);

    g.setColour(node.active ? nodeStyle.textColour : nodeStyle.inactiveTextColour);
    g.setFont(juce::FontOptions(nodeStyle.fontSize, juce::Font::bold));
    g.drawFittedText(node.name, bounds.reduced(6.0f, 4.0f).toNearestInt(),
                     juce::Justification::centred, 1);
}

void FxSignalFlow::paint(juce::Graphics& g)
{
    if (nodes.empty() || current.size() != nodes.size())
    {
        return;
    }

    // Connectors first, drawn between SLOTS rather than between the nodes'
    // live positions: the chain's shape should stay legible while a node is
    // lifted out of it.
    for (std::size_t i = 0; i + 1 < slots.size(); ++i)
    {
        drawConnector(g, slots[i], slots[i + 1]);
    }

    // The gap the dragged node will drop into.
    if (draggedIndex >= 0 && insertionIndex >= 0 && insertionIndex < static_cast<int>(slots.size()))
    {
        const auto slot = slots[static_cast<std::size_t>(insertionIndex)];
        g.setColour(nodeStyle.dropHighlightColour);
        g.fillRoundedRectangle(slot, nodeStyle.cornerRadius);
        g.setColour(nodeStyle.borderColour.withMultipliedAlpha(1.5f));
        g.drawRoundedRectangle(slot.reduced(0.5f), nodeStyle.cornerRadius, 1.0f);
    }

    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (static_cast<int>(i) == draggedIndex)
        {
            continue;   // drawn last, so it sits above its neighbours
        }
        drawNode(g, nodes[i], current[i], false, static_cast<int>(i) == hoveredIndex);
    }

    if (draggedIndex >= 0)
    {
        drawNode(g, nodes[static_cast<std::size_t>(draggedIndex)],
                 current[static_cast<std::size_t>(draggedIndex)], true, false);
    }
}

} // namespace px3::ui
