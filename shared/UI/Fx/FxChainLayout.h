#pragma once

#include <JuceHeader.h>

#include <vector>

namespace px3::ui
{

// The arithmetic behind the FX page, kept out of the components that use it.
//
// The page is a rack of cards that flows left to right and wraps onto further
// rows, one rack per processing domain (INSTRUMENT, SEND FX, MASTER). Free
// functions rather than component methods so the rules can be tested without
// a window, and so there is exactly one of each.

// Moves one entry within an order, sliding the entries it passes. Out-of-range
// indices leave the order untouched rather than throwing or clamping into a
// silent reorder.
std::vector<int> moveChainEntry(std::vector<int> order, int fromIndex, int toIndex);

// How many cards of at least `minCardWidth`, separated by `gap`, fit across
// `availableWidth`. Never less than one: a window narrower than one card
// scrolls rather than crushing it.
int fxRackColumns(int availableWidth, int minCardWidth, int gap);

// The one card width every section uses, so all cards share a footprint: a
// row of `columns` filling the width, capped at `maxCardWidth` (cards are not
// stretched across a wide window) and never below `minCardWidth`.
int fxRackCardWidth(int availableWidth, int columns, int gap, int minCardWidth, int maxCardWidth);

// One section's cards, laid out left to right from `origin` and wrapped after
// `columns` cells. Each row is as tall as its tallest card and every card in
// the row is stretched to it, so a row reads as one line of modules. Cells are
// returned in the order the heights were given - wrapping never reorders.
struct FxRackFlow
{
    std::vector<juce::Rectangle<int>> cells;
    std::vector<int> rowOfCell;
    int rowCount { 0 };
    int height { 0 };   // from origin.y to the bottom of the last row
};

FxRackFlow fxRackFlow(juce::Point<int> origin,
                      int cardWidth,
                      int columns,
                      int gap,
                      int rowGap,
                      const std::vector<int>& cardHeights);

// The cell a dragged card should drop into: the one whose centre is nearest
// `point`. Works across wrapped rows, because it is a 2D distance rather than
// a position along one axis. -1 when there are no cells.
int fxRackInsertionIndex(const std::vector<juce::Rectangle<int>>& cells, juce::Point<int> point);

} // namespace px3::ui
