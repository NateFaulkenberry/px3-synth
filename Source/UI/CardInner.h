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

    static FlexStyle read(const UIConfig* config,
                          const juce::String& defaultsPath,
                          const juce::String& stylePath,
                          const FlexStyle& fallback);
    // Rows need more than two layers, so the general form is exposed.
    static FlexStyle readLayered(const UIConfig* config,
                                 const juce::StringArray& pathsMostSpecificFirst,
                                 const FlexStyle& fallback);

    // A FlexBox carrying these settings, ready for items to be added.
    juce::FlexBox toFlexBox() const;
    // The margin that realises `gap` for an item in this container.
    juce::FlexItem::Margin gapMargin() const;
};

struct RowStyle
{
    // A percentage of the cardInner CONTENT height - not of the card, the
    // panel, or the previous row.
    Dimension height { Dimension::Unit::percent, 33.3333f };
    Insets margin;
    Insets padding;
    FlexStyle flex;
};

struct CardInnerStyle
{
    Insets margin;
    Insets padding;
    // Column by default: the standard structure is three stacked rows.
    FlexStyle flex { true, FlexDirection::column, FlexWrapMode::noWrap,
                     JustifyContent::centre, AlignItems::centre, AlignItems::centre, 0.0f };
    std::vector<RowStyle> rows;

    static CardInnerStyle fromConfig(const UIConfig* config,
                                     const juce::String& defaultsPath,
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
    void setKeys(juce::String defaultsPath, juce::String stylePath);
    void setConfig(std::shared_ptr<const UIConfig> config);
    void setRowCount(int count);

    // `cardContent` is the Card's content box, already inside the card's
    // padding and below its title. Everything below is measured from that.
    void layout(juce::Rectangle<int> cardContent);

    // The cardInner content box: inside its own margin and padding. Rows are
    // laid out in here and percentages are measured against it.
    juce::Rectangle<int> content() const { return innerContent; }

    int rowCount() const { return static_cast<int>(rowContentBounds.size()); }
    // A row's content box: inside that row's own margin and padding.
    juce::Rectangle<int> rowContent(int index) const;
    // A FlexBox carrying that row's configured flex properties.
    juce::FlexBox rowFlex(int index) const;
    // The margin that realises that row's `gap` for each of its items.
    juce::FlexItem::Margin rowGap(int index) const;

    const CardInnerStyle& style() const { return cached; }

private:
    void refresh();

    std::shared_ptr<const UIConfig> config;
    juce::String defaultsPath { "cards.defaults.cardInner" };
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

// How many lines a wrapping row will break its items into, given their natural
// widths and the gap between them.
//
// A wrapped row needs this because FlexBox derives line heights from the items:
// give every item the full row height and two lines are twice as tall as the
// row they are in. Dividing the row height by this count makes the wrapped
// lines fill the row instead of overflowing it.
int wrappedLineCount(const std::vector<float>& itemWidths, float gap, float rowWidth);

void layoutLabelledControl(juce::Rectangle<int> area,
                           juce::Component* label,
                           juce::Component* control,
                           juce::Component* readout,
                           int labelHeight = 16,
                           int readoutHeight = 14,
                           ControlShape shape = ControlShape::square,
                           int maxControlSize = 0);

} // namespace px3::ui
