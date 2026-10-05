#pragma once

// Dense module interiors (0.8.0 dense layout). The modulator cards on the
// VOICE surface are ~170-260 px wide and ~230-340 px tall, so their insides
// are stacked rows of cells rather than CardInner's percentage rows: a fixed
// caption height, controls as large as the cell allows, and the display (wave
// / envelope graph) takes whatever height is left. Everything is flex-style
// slicing of the content box; there are no absolute coordinates.

#include <JuceHeader.h>

#include "CardInner.h"

namespace px3::ui::compact
{
inline constexpr int captionHeight = 12;   // printed caption above a control
inline constexpr int readoutHeight = 12;   // value under a knob
inline constexpr int boxHeight = 20;       // dropdowns and switches
inline constexpr int pad = 4;              // module content inset
inline constexpr int gap = 3;              // between cells and rows
inline constexpr int headerClearance = 6;  // a display at the top of a module, clear of its header

// A row of `count` equal cells with `gap` between them.
inline std::vector<juce::Rectangle<int>> cells(juce::Rectangle<int> row, int count)
{
    std::vector<juce::Rectangle<int>> out;
    if (count <= 0) { return out; }
    const auto total = row.getWidth() - gap * (count - 1);
    for (int i = 0; i < count; ++i)
    {
        const auto w = (total * (i + 1)) / count - (total * i) / count;
        out.push_back(row.removeFromLeft(w));
        row.removeFromLeft(gap);
    }
    return out;
}

// Caption over a dropdown or switch, bottom-aligned in the cell.
inline void boxCell(juce::Rectangle<int> cell, juce::Component* caption, juce::Component& box)
{
    auto stack = cell.withTrimmedTop(juce::jmax(0, cell.getHeight() - captionHeight - boxHeight));
    if (caption != nullptr) { caption->setBounds(stack.removeFromTop(captionHeight)); }
    else { stack.removeFromTop(captionHeight); }
    box.setBounds(stack.removeFromTop(boxHeight));
}

// Caption, knob, readout - the knob as large as the cell allows up to `maxKnob`.
inline void knobCell(juce::Rectangle<int> cell, juce::Component* caption, juce::Component& knob,
                     juce::Component* readout, int maxKnob)
{
    ControlStyle style;
    style.gap = 1.0f;
    layoutLabelledControl(cell, { caption, &knob, readout, ControlShape::square, captionHeight, readoutHeight, maxKnob },
                          style);
}

// Height a knob row needs for a knob of `knob` px.
inline int knobRowHeight(int knob) { return captionHeight + knob + readoutHeight + 2; }
} // namespace px3::ui::compact
