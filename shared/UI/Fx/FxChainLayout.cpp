#include "FxChainLayout.h"

#include <cmath>
#include <limits>

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

int fxRackColumns(int availableWidth, int minCardWidth, int gap)
{
    const auto step = juce::jmax(1, minCardWidth + gap);
    return juce::jmax(1, (availableWidth + gap) / step);
}

int fxRackCardWidth(int availableWidth, int columns, int gap, int minCardWidth, int maxCardWidth)
{
    const auto safeColumns = juce::jmax(1, columns);
    const auto fill = (availableWidth - gap * (safeColumns - 1)) / safeColumns;
    return juce::jmax(minCardWidth, juce::jmin(juce::jmax(minCardWidth, maxCardWidth), fill));
}

FxRackFlow fxRackFlow(juce::Point<int> origin,
                      int cardWidth,
                      int columns,
                      int gap,
                      int rowGap,
                      const std::vector<int>& cardHeights)
{
    FxRackFlow flow;
    const auto count = static_cast<int>(cardHeights.size());
    if (count == 0)
    {
        return flow;
    }

    const auto safeColumns = juce::jmax(1, columns);
    flow.rowCount = (count + safeColumns - 1) / safeColumns;
    flow.cells.reserve(cardHeights.size());
    flow.rowOfCell.reserve(cardHeights.size());

    auto y = origin.y;
    for (int row = 0; row < flow.rowCount; ++row)
    {
        const auto first = row * safeColumns;
        const auto last = juce::jmin(count, first + safeColumns);

        auto rowHeight = 0;
        for (int i = first; i < last; ++i)
        {
            rowHeight = juce::jmax(rowHeight, cardHeights[static_cast<std::size_t>(i)]);
        }

        for (int i = first; i < last; ++i)
        {
            const auto column = i - first;
            flow.cells.push_back({ origin.x + column * (cardWidth + gap), y, cardWidth, rowHeight });
            flow.rowOfCell.push_back(row);
        }

        y += rowHeight + (row + 1 < flow.rowCount ? rowGap : 0);
    }

    flow.height = y - origin.y;
    return flow;
}

int fxRackInsertionIndex(const std::vector<juce::Rectangle<int>>& cells, juce::Point<int> point)
{
    auto best = -1;
    auto bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        const auto centre = cells[i].getCentre();
        const auto dx = static_cast<double>(centre.x - point.x);
        const auto dy = static_cast<double>(centre.y - point.y);
        const auto distance = dx * dx + dy * dy;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace px3::ui
