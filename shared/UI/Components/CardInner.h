#pragma once

#include "Card.h"

#include <JuceHeader.h>

#include <vector>

class UIConfig;

namespace px3::ui
{
// The layout environment inside a Card.
//
//     Card
//     ├── border / background / gloss / title      (Card.h)
//     └── cardInner                                 (here)
//         ├── row 0
//         ├── row 1
//         └── row 2
//
// cardInner and its rows provide geometry only. What goes in them - knobs,
// dropdowns, graphs - stays the component's business, and this deliberately
// knows nothing about any of it. That separation is what will let control
// styling be added later without disturbing this layer.
//
// juce::FlexBox is the engine. It already implements the semantics wanted here,
// so this is a thin, testable translation from JSON to FlexBox rather than a
// second layout algorithm.

enum class FlexDirection { row, rowReverse, column, columnReverse };
enum class FlexWrapMode { noWrap, wrap };
enum class JustifyContent { start, end, centre, spaceBetween, spaceAround };
enum class AlignItems { start, end, centre, stretch };

// The flex properties shared by cardInner and by each row.
struct FlexStyle
{
    // `display: none` really removes the box: a row with it is given no height,
    // no content area and no gap, so the rows around it close up. They do not
    // grow to absorb the space, exactly as in CSS. The only other value is
    // `flex`, which is the default.
    bool display { true };
    FlexDirection direction { FlexDirection::row };
    FlexWrapMode wrap { FlexWrapMode::noWrap };
    JustifyContent justifyContent { JustifyContent::centre };
    AlignItems alignItems { AlignItems::centre };
    AlignItems alignContent { AlignItems::centre };
    // FlexBox has no gap property, so it is applied as a margin on each item.
    float gap { 0.0f };

    static FlexStyle readLayered(const UIConfig* config,
                                 const juce::StringArray& pathsMostSpecificFirst,
                                 const FlexStyle& fallback);

    // A FlexBox carrying these settings, ready for items to be added.
    juce::FlexBox toFlexBox() const;
    // The margin that realises `gap` for an item in this container.
    juce::FlexItem::Margin gapMargin() const;
};

// How one cell stacks its label, control and readout.
//
// This is the layer below the row: the row decides where each CELL goes, this
// decides what happens inside one. Before it existed the stack was hand-rolled -
// label pinned to the top, readout to the bottom, control centred in whatever
// was left - so the distance between a label and its control was leftover space
// rather than a value anyone could set.
//
// `justifyContent: "space-between"` reproduces that old spread exactly, so
// nothing is lost by defaulting to "center".
struct ControlStyle
{
    // column: label above the control. row: label beside it.
    FlexDirection direction { FlexDirection::column };
    JustifyContent justifyContent { JustifyContent::centre };
    AlignItems alignItems { AlignItems::centre };
    // Between the label, the control and the readout - NOT between cells; that
    // is the row's own `gap`.
    float gap { 4.0f };

    // Each defaults to "auto", meaning "whatever the component asked for".
    // A pixel or percentage value overrides it. Percentages are of the cell's
    // shorter side. Keeping auto as the default is what lets this be introduced
    // without every control in the plugin changing size.
    Dimension labelHeight;
    Dimension readoutHeight;
    Dimension size;

    static ControlStyle readLayered(const UIConfig* config,
                                    const juce::StringArray& pathsMostSpecificFirst,
                                    const ControlStyle& fallback);
};

struct RowStyle
{
    // A percentage of the cardInner CONTENT height - not of the card, the
    // panel, or the previous row.
    Dimension height { Dimension::Unit::percent, 33.3333f };
    Insets margin;
    Insets padding;
    FlexStyle flex;
    ControlStyle control;
};

// The power toggle's slot: a fixed square pinned to cardInner's top-left
// corner, deliberately OUTSIDE the flex flow.
//
// It sits outside because it is not one control among a row's controls - it is
// the card's own switch, and it should stay put when the row beside it gains or
// loses items. `x` and `y` offset it from that corner and may be negative, so
// it can be nudged over the card's padding if a card wants it tighter in.
struct PowerStyle
{
    float x { 0.0f };
    float y { 0.0f };
    float size { 25.0f };
};

struct CardInnerStyle
{
    Insets margin;
    Insets padding;
    // Column by default: the standard structure is three stacked rows.
    FlexStyle flex { true, FlexDirection::column, FlexWrapMode::noWrap,
                     JustifyContent::centre, AlignItems::centre, AlignItems::centre, 0.0f };
    PowerStyle power;
    std::vector<RowStyle> rows;

    static CardInnerStyle fromConfig(const UIConfig* config,
                                     const juce::String& stylePath,
                                     int rowCount);
};

// Resolves the cardInner box and its row boxes, and hands components a
// pre-configured FlexBox for each row.
//
// Components add their own FlexItems with their own sizes: this decides where
// things go, not how big each control wants to be. Keeping that split is what
// lets controls be moved into the system without changing how they look.
class CardInner
{
public:
    // The single block this layout is read from, e.g. "cards.osc.cardInner".
    // There is no global fallback: cardInner is defined per component TYPE, so
    // every oscillator shares one layout and no card inherits another's.
    void setStylePath(juce::String path);
    void setConfig(std::shared_ptr<const UIConfig> config);
    void setRowCount(int count);

    // `cardContent` is the Card's content box, already inside the card's
    // padding and below its title. Everything below is measured from that.
    void layout(juce::Rectangle<int> cardContent);

    // The cardInner content box: inside its own margin and padding. Rows are
    // laid out in here and percentages are measured against it.
    juce::Rectangle<int> content() const { return innerContent; }

    // Where the power toggle goes: pinned to the top-left of the cardInner
    // content box, offset by the configured x/y. Not part of any row.
    juce::Rectangle<int> powerBounds() const;

    int rowCount() const { return static_cast<int>(rowContentBounds.size()); }
    // A row's content box: inside that row's own margin and padding.
    juce::Rectangle<int> rowContent(int index) const;
    // A FlexBox carrying that row's configured flex properties.
    juce::FlexBox rowFlex(int index) const;
    // The margin that realises that row's `gap` for each of its items.
    juce::FlexItem::Margin rowGap(int index) const;
    // How that row's cells stack their label, control and readout.
    const ControlStyle& rowControl(int index) const;

    const CardInnerStyle& style() const { return cached; }

private:
    void refresh();

    std::shared_ptr<const UIConfig> config;
    juce::String stylePath;
    int rows { 3 };

    CardInnerStyle cached;
    const UIConfig* parsedFrom { nullptr };
    juce::String parsedStylePath;
    bool hasParsed { false };

    juce::Rectangle<int> lastCardContent;
    juce::Rectangle<int> innerContent;
    std::vector<juce::Rectangle<int>> rowContentBounds;
};

// Stacks a label, a control and a readout vertically inside `area`, centred,
// skipping any that are absent.
//
// A free function rather than a component: the standard knob presentation is
// three existing widgets, and wrapping every knob in a container component
// would add an object per control for no benefit beyond grouping.
// How a control fills the space its label leaves behind. A knob is round and
// must stay square; a dropdown is a horizontal control and must not be. Getting
// this wrong is not subtle - it turns every combo box into a square.
enum class ControlShape
{
    square,  // knobs and tick boxes: the largest centred square that fits
    stretch, // dropdowns and buttons: full width, capped height, centred
};

// The card TYPE a per-instance style key belongs to: "osc2" -> "osc",
// "filter1" -> "filter", "subOsc" -> "subOsc". cardInner is configured per
// type, so the three oscillators cannot drift apart from one another.
juce::String cardTypeKey(const juce::String& styleKey);

// Widths for a non-wrapping row of fixed-size cells that must fit inside it.
//
// Each cell gets its natural width, and if the total - gaps included - exceeds
// the row, every cell is scaled by the same factor. This is the same rule row
// heights follow, and for the same reason: FlexBox will not shrink items whose
// size is set explicitly, so a row of fixed cells silently overflows instead.
std::vector<float> fitRowItemWidths(const std::vector<float>& naturalWidths,
                                    float gap,
                                    float rowWidth);

// How many lines a wrapping row will break its items into, given their natural
// widths and the gap between them.
//
// A wrapped row needs this because FlexBox derives line heights from the items:
// give every item the full row height and two lines are twice as tall as the
// row they are in. Dividing the row height by this count makes the wrapped
// lines fill the row instead of overflowing it.
int wrappedLineCount(const std::vector<float>& itemWidths, float gap, float rowWidth);

// One cell's contents, plus the sizes the COMPONENT wants. Config values of
// "auto" fall back to these, so a component that knows its knob should never
// exceed 56px keeps saying so.
struct LabelledControl
{
    juce::Component* label { nullptr };
    juce::Component* control { nullptr };
    juce::Component* readout { nullptr };
    ControlShape shape { ControlShape::square };
    int labelHeight { 0 };
    // A non-zero readoutHeight reserves the space whether or not this cell has
    // a readout. That is what lets a row mix cells that have one with cells
    // that do not and still have every knob land at the same height: without
    // it, each cell centres a different-height group and the knobs drift apart.
    int readoutHeight { 0 };
    // 0 means uncapped: as large as the cell allows.
    int maxControlSize { 0 };
};

void layoutLabelledControl(juce::Rectangle<int> area,
                           const LabelledControl& parts,
                           const ControlStyle& style);

} // namespace px3::ui
