#pragma once

#include <JuceHeader.h>

#include <vector>

namespace px3::ui
{

// The arithmetic behind the FX panel, kept out of the components that use it.
//
// Both the signal-flow strip and the FX grid are two readings of one ordered
// list, and both need the same answers: where does entry i sit, and where does
// a dragged entry land. Free functions rather than component methods so the
// rules can be tested without a window, and so there is exactly one of each.

// Moves one entry within an order, sliding the entries it passes. Out-of-range
// indices leave the order untouched rather than throwing or clamping into a
// silent reorder.
std::vector<int> moveChainEntry(std::vector<int> order, int fromIndex, int toIndex);

// The resting positions of `count` nodes laid across `area`, separated by
// `gap`. Nodes never go below `minWidth`, so a narrow panel overflows visibly
// instead of collapsing every node to a sliver.
std::vector<juce::Rectangle<float>> signalFlowSlots(juce::Rectangle<float> area,
                                                    int count,
                                                    int gap,
                                                    int minWidth);

// Which slot a dragged node's centre currently sits over. Measured against slot
// centres, so the swap happens once the node is more than halfway across its
// neighbour - which is where the eye expects it.
int insertionIndexForCentre(const std::vector<juce::Rectangle<float>>& slots, float centreX);

// A wrapping grid of `count` cells in `columns` columns. Returns cells in chain
// order; the caller places its components into them.
std::vector<juce::Rectangle<int>> fxGridCells(int contentWidth,
                                              int count,
                                              int columns,
                                              int gap,
                                              int rowHeight);

// Height the scrolled content needs for `count` cells - the grid's own height,
// which may be less than the viewport's.
int fxGridContentHeight(int count, int columns, int gap, int rowHeight);

} // namespace px3::ui
