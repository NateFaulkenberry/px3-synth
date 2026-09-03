#include "FxChainLayout.h"

namespace px3::ui
{

std::vector<int> moveChainEntry(std::vector<int> order, int fromIndex, int toIndex)
{
    const auto count = static_cast<int>(order.size());
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= count || toIndex >= count || fromIndex == toIndex)
    {
        return order;
    }

    const auto moved = order[static_cast<std::size_t>(fromIndex)];
    order.erase(order.begin() + fromIndex);
    order.insert(order.begin() + toIndex, moved);
    return order;
}

std::vector<juce::Rectangle<float>> signalFlowSlots(juce::Rectangle<float> area,
                                                    int count,
                                                    int gap,
                                                    int minWidth)
{
    std::vector<juce::Rectangle<float>> slots;
    if (count <= 0 || area.getWidth() <= 0.0f)
    {
        return slots;
    }

    slots.reserve(static_cast<std::size_t>(count));

    const auto totalGap = static_cast<float>(gap * juce::jmax(0, count - 1));
    const auto nodeWidth = juce::jmax(static_cast<float>(minWidth),
                                      (area.getWidth() - totalGap) / static_cast<float>(count));

    auto x = area.getX();
    for (int i = 0; i < count; ++i)
    {
        slots.push_back({ x, area.getY(), nodeWidth, area.getHeight() });
        x += nodeWidth + static_cast<float>(gap);
    }

    return slots;
}

int insertionIndexForCentre(const std::vector<juce::Rectangle<float>>& slots, float centreX)
{
    int index = 0;
    for (std::size_t i = 0; i < slots.size(); ++i)
    {
        if (centreX > slots[i].getCentreX())
        {
            index = static_cast<int>(i);
        }
    }
    return index;
}

int fxGridContentHeight(int count, int columns, int gap, int rowHeight)
{
    if (count <= 0)
    {
        return 0;
    }

    const auto safeColumns = juce::jmax(1, columns);
    const auto rows = (count + safeColumns - 1) / safeColumns;
    return rows * rowHeight + (rows - 1) * gap;
}

std::vector<juce::Rectangle<int>> fxGridCells(int contentWidth,
                                              int count,
                                              int columns,
                                              int gap,
                                              int rowHeight)
{
    std::vector<juce::Rectangle<int>> cells;
    if (count <= 0 || contentWidth <= 0)
    {
        return cells;
    }

    cells.reserve(static_cast<std::size_t>(count));

    const auto safeColumns = juce::jmax(1, columns);
    // Computed rather than laid out by FlexBox: FlexBox will not shrink an
    // explicitly-sized item, so a column count that does not divide the width
    // evenly would overflow instead of fitting.
    const auto cellWidth = (static_cast<float>(contentWidth) - static_cast<float>(gap * (safeColumns - 1)))
                           / static_cast<float>(safeColumns);

    for (int i = 0; i < count; ++i)
    {
        const auto column = i % safeColumns;
        const auto row = i / safeColumns;
        const auto x = static_cast<float>(column) * (cellWidth + static_cast<float>(gap));
        const auto y = static_cast<float>(row * (rowHeight + gap));

        cells.push_back(juce::Rectangle<float>(x, y, cellWidth, static_cast<float>(rowHeight)).toNearestInt());
    }

    return cells;
}

} // namespace px3::ui
