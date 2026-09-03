#include "CardInner.h"

#include <limits>

#include "UIConfig.h"

namespace px3::ui
{
namespace
{
// Reads `key` from the first path that defines it, most specific first.
//
// Rows need three layers - this card's row N, the shared default for row N, and
// the shared default for any row - and collapsing that to two made rows
// declared in the defaults block invisible, so every row silently fell back to
// an equal share of the height.
juce::var readLayered(const UIConfig* config,
                      const juce::StringArray& pathsMostSpecificFirst,
                      const juce::String& key)
{
    if (config == nullptr)
    {
        return {};
    }
    for (const auto& path : pathsMostSpecificFirst)
    {
        if (path.isEmpty())
        {
            continue;
        }
        auto value = config->getValue(path + "." + key);
        if (! value.isVoid())
        {
            return value;
        }
    }
    return {};
}

// Keyword spellings are normalised before matching: lowercased, with hyphens
// and underscores removed. Every property NAME in this schema is camelCase, so
// "flexStart" is the natural thing to write for a value - and requiring
// "flex-start" instead is a trap that fails silently. All three spellings work.
juce::String normaliseKeyword(const juce::String& text)
{
    return text.trim().toLowerCase().removeCharacters("-_");
}

// An unrecognised keyword falls back, which used to be invisible: a typo just
// left the property doing nothing. In a debug build it now says so.
void warnUnknownKeyword(const char* property, const juce::String& text)
{
    juce::ignoreUnused(property, text);
    DBG("UIConfig: unrecognised " << property << " value \"" << text << "\" - ignored");
}

juce::FlexBox::Direction toFlexDirection(FlexDirection d)
{
    switch (d)
    {
        case FlexDirection::rowReverse:    return juce::FlexBox::Direction::rowReverse;
        case FlexDirection::column:        return juce::FlexBox::Direction::column;
        case FlexDirection::columnReverse: return juce::FlexBox::Direction::columnReverse;
        case FlexDirection::row:           break;
    }
    return juce::FlexBox::Direction::row;
}

juce::FlexBox::JustifyContent toFlexJustify(JustifyContent j)
{
    switch (j)
    {
        case JustifyContent::start:        return juce::FlexBox::JustifyContent::flexStart;
        case JustifyContent::end:          return juce::FlexBox::JustifyContent::flexEnd;
        case JustifyContent::spaceBetween: return juce::FlexBox::JustifyContent::spaceBetween;
        case JustifyContent::spaceAround:  return juce::FlexBox::JustifyContent::spaceAround;
        case JustifyContent::centre:       break;
    }
    return juce::FlexBox::JustifyContent::center;
}

juce::FlexBox::AlignItems toFlexAlign(AlignItems a)
{
    switch (a)
    {
        case AlignItems::start:   return juce::FlexBox::AlignItems::flexStart;
        case AlignItems::end:     return juce::FlexBox::AlignItems::flexEnd;
        case AlignItems::stretch: return juce::FlexBox::AlignItems::stretch;
        case AlignItems::centre:  break;
    }
    return juce::FlexBox::AlignItems::center;
}

juce::FlexBox::AlignContent toFlexAlignContent(AlignItems a)
{
    switch (a)
    {
        case AlignItems::start:   return juce::FlexBox::AlignContent::flexStart;
        case AlignItems::end:     return juce::FlexBox::AlignContent::flexEnd;
        case AlignItems::stretch: return juce::FlexBox::AlignContent::stretch;
        case AlignItems::centre:  break;
    }
    return juce::FlexBox::AlignContent::center;
}

FlexDirection parseDirection(const juce::String& text, FlexDirection fallback)
{
    const auto t = normaliseKeyword(text);
    if (t == "row")           return FlexDirection::row;
    if (t == "rowreverse")    return FlexDirection::rowReverse;
    if (t == "column")        return FlexDirection::column;
    if (t == "columnreverse") return FlexDirection::columnReverse;
    warnUnknownKeyword("direction", text);
    return fallback;
}

FlexWrapMode parseWrap(const juce::String& text, FlexWrapMode fallback)
{
    const auto t = normaliseKeyword(text);
    if (t == "nowrap") return FlexWrapMode::noWrap;
    if (t == "wrap")   return FlexWrapMode::wrap;
    warnUnknownKeyword("wrap", text);
    return fallback;
}

JustifyContent parseJustify(const juce::String& text, JustifyContent fallback)
{
    const auto t = normaliseKeyword(text);
    if (t == "flexstart" || t == "start")  return JustifyContent::start;
    if (t == "flexend" || t == "end")      return JustifyContent::end;
    if (t == "center" || t == "centre")    return JustifyContent::centre;
    if (t == "spacebetween")               return JustifyContent::spaceBetween;
    if (t == "spacearound")                return JustifyContent::spaceAround;
    warnUnknownKeyword("justifyContent", text);
    return fallback;
}

AlignItems parseAlign(const juce::String& text, AlignItems fallback)
{
    const auto t = normaliseKeyword(text);
    if (t == "flexstart" || t == "start") return AlignItems::start;
    if (t == "flexend" || t == "end")     return AlignItems::end;
    if (t == "center" || t == "centre")   return AlignItems::centre;
    if (t == "stretch")                   return AlignItems::stretch;
    warnUnknownKeyword("alignItems", text);
    return fallback;
}

} // namespace

// ---------------------------------------------------------------------------
// FlexStyle
// ---------------------------------------------------------------------------

// Reads an inset the generic way - a number, or {top,right,bottom,left} - and
// then lets flat per-side siblings override individual edges:
//
//     "padding": 4, "paddingTop": 0
//
// The sibling form exists because trimming one edge is the common case and
// writing the whole object out to change one number is not. Sides are applied
// after the generic value, so the generic one is the base and each sibling is
// an override of it. Layered the same way as everything else, so a row may
// override a single edge of the default row's padding.
Insets readInsetsLayered(const UIConfig* config,
                         const juce::StringArray& pathsMostSpecificFirst,
                         const juce::String& key,
                         Insets fallback)
{
    auto result = Insets::parse(readLayered(config, pathsMostSpecificFirst, key), fallback);

    const auto side = [&](const char* suffix, float& target)
    {
        const auto value = readLayered(config, pathsMostSpecificFirst, key + suffix);
        if (! value.isVoid())
        {
            target = static_cast<float>(value);
        }
    };

    side("Top", result.top);
    side("Right", result.right);
    side("Bottom", result.bottom);
    side("Left", result.left);

    return result;
}

FlexStyle FlexStyle::readLayered(const UIConfig* config,
                                 const juce::StringArray& pathsMostSpecificFirst,
                                 const FlexStyle& fallback)
{
    FlexStyle style = fallback;
    if (config == nullptr)
    {
        return style;
    }

    const auto value = [&](const char* key)
    {
        return px3::ui::readLayered(config, pathsMostSpecificFirst, key);
    };

    if (const auto v = value("display"); ! v.isVoid())
        style.display = normaliseKeyword(v.toString()) != "none";
    if (const auto v = value("direction"); ! v.isVoid())
        style.direction = parseDirection(v.toString(), fallback.direction);
    if (const auto v = value("wrap"); ! v.isVoid())
        style.wrap = parseWrap(v.toString(), fallback.wrap);
    if (const auto v = value("justifyContent"); ! v.isVoid())
        style.justifyContent = parseJustify(v.toString(), fallback.justifyContent);
    if (const auto v = value("alignItems"); ! v.isVoid())
        style.alignItems = parseAlign(v.toString(), fallback.alignItems);
    if (const auto v = value("alignContent"); ! v.isVoid())
        style.alignContent = parseAlign(v.toString(), fallback.alignContent);
    if (const auto v = value("gap"); ! v.isVoid())
        style.gap = juce::jmax(0.0f, static_cast<float>(v));

    return style;
}

ControlStyle ControlStyle::readLayered(const UIConfig* config,
                                       const juce::StringArray& pathsMostSpecificFirst,
                                       const ControlStyle& fallback)
{
    ControlStyle style = fallback;
    if (config == nullptr)
    {
        return style;
    }

    const auto value = [&](const char* key)
    {
        return px3::ui::readLayered(config, pathsMostSpecificFirst, key);
    };

    if (const auto v = value("direction"); ! v.isVoid())
        style.direction = parseDirection(v.toString(), fallback.direction);
    if (const auto v = value("justifyContent"); ! v.isVoid())
        style.justifyContent = parseJustify(v.toString(), fallback.justifyContent);
    if (const auto v = value("alignItems"); ! v.isVoid())
        style.alignItems = parseAlign(v.toString(), fallback.alignItems);
    if (const auto v = value("gap"); ! v.isVoid())
        style.gap = juce::jmax(0.0f, static_cast<float>(v));

    style.labelHeight = Dimension::parse(value("labelHeight"), style.labelHeight);
    style.readoutHeight = Dimension::parse(value("readoutHeight"), style.readoutHeight);
    style.size = Dimension::parse(value("size"), style.size);

    return style;
}

juce::FlexBox FlexStyle::toFlexBox() const
{
    juce::FlexBox box;
    box.flexDirection = toFlexDirection(direction);
    box.flexWrap = (wrap == FlexWrapMode::wrap) ? juce::FlexBox::Wrap::wrap
                                                : juce::FlexBox::Wrap::noWrap;
    box.justifyContent = toFlexJustify(justifyContent);
    box.alignItems = toFlexAlign(alignItems);
    box.alignContent = toFlexAlignContent(alignContent);
    return box;
}

juce::FlexItem::Margin FlexStyle::gapMargin() const
{
    // FlexBox has no gap, so half the gap goes on each side of every item and
    // adjacent items therefore sit a full gap apart. The outer edges gain half
    // a gap, which is why the default gap is zero: a container that declares no
    // gap is unaffected.
    const auto half = gap * 0.5f;
    if (direction == FlexDirection::column || direction == FlexDirection::columnReverse)
    {
        return juce::FlexItem::Margin(half, 0.0f, half, 0.0f);
    }
    return juce::FlexItem::Margin(0.0f, half, 0.0f, half);
}

// ---------------------------------------------------------------------------
// CardInnerStyle
// ---------------------------------------------------------------------------

CardInnerStyle CardInnerStyle::fromConfig(const UIConfig* config,
                                          const juce::String& stylePath,
                                          int rowCount)
{
    CardInnerStyle style;
    style.rows.clear();

    if (config != nullptr)
    {
        style.margin = readInsetsLayered(config, { stylePath }, "margin", style.margin);
        style.padding = readInsetsLayered(config, { stylePath }, "padding", style.padding);
        style.flex = FlexStyle::readLayered(config, { stylePath }, style.flex);

        const auto powerPath = stylePath + ".power";
        if (const auto v = config->getValue(powerPath + ".x"); ! v.isVoid())
            style.power.x = static_cast<float>(v);
        if (const auto v = config->getValue(powerPath + ".y"); ! v.isVoid())
            style.power.y = static_cast<float>(v);
        if (const auto v = config->getValue(powerPath + ".size"); ! v.isVoid())
            style.power.size = juce::jmax(0.0f, static_cast<float>(v));
    }

    // Rows share a default block so a card only declares the ones that differ.
    const RowStyle rowFallback { Dimension { Dimension::Unit::percent, 100.0f / juce::jmax(1, rowCount) },
                                 {}, {},
                                 FlexStyle { true, FlexDirection::row, FlexWrapMode::noWrap,
                                             JustifyContent::centre, AlignItems::centre,
                                             AlignItems::centre, 0.0f },
                                 ControlStyle {} };

    for (int i = 0; i < rowCount; ++i)
    {
        RowStyle row = rowFallback;
        if (config != nullptr)
        {
            // This type's row N, then this type's own default row. A card
            // never falls back to another card's layout.
            const juce::StringArray paths {
                stylePath + ".rows.row" + juce::String(i + 1),
                stylePath + ".rows.default",
            };

            auto merged = rowFallback;
            merged.height = Dimension::parse(readLayered(config, paths, "height"), merged.height);
            merged.margin = readInsetsLayered(config, paths, "margin", merged.margin);
            merged.padding = readInsetsLayered(config, paths, "padding", merged.padding);
            merged.flex = FlexStyle::readLayered(config, paths, merged.flex);

            juce::StringArray controlPaths;
            for (const auto& path : paths)
            {
                controlPaths.add(path + ".control");
            }
            merged.control = ControlStyle::readLayered(config, controlPaths, merged.control);
            row = merged;
        }
        style.rows.push_back(row);
    }

    return style;
}

// ---------------------------------------------------------------------------
// CardInner
// ---------------------------------------------------------------------------

void CardInner::setStylePath(juce::String path)
{
    if (stylePath != path)
    {
        stylePath = std::move(path);
        hasParsed = false;
        layout(lastCardContent);
    }
}

void CardInner::setConfig(std::shared_ptr<const UIConfig> configIn)
{
    config = std::move(configIn);
    hasParsed = false;
    layout(lastCardContent);
}

void CardInner::setRowCount(int count)
{
    const auto clamped = juce::jmax(0, count);
    if (rows != clamped)
    {
        rows = clamped;
        hasParsed = false;
        layout(lastCardContent);
    }
}

void CardInner::refresh()
{
    // Same invalidation rule as CardStyleCache: a reload produces a new
    // UIConfig object, so a changed pointer is a changed file.
    if (! hasParsed || parsedFrom != config.get() || parsedStylePath != stylePath)
    {
        cached = CardInnerStyle::fromConfig(config.get(), stylePath, rows);
        parsedFrom = config.get();
        parsedStylePath = stylePath;
        hasParsed = true;
    }
}

void CardInner::layout(juce::Rectangle<int> cardContent)
{
    lastCardContent = cardContent;
    refresh();

    // cardInner margin is outside it, padding is inside - the same distinction
    // the Card makes, deliberately not collapsed into one spacing property.
    const auto marginBox = cached.margin.shrink(cardContent.toFloat());
    innerContent = cached.padding.shrink(marginBox).toNearestInt();

    rowContentBounds.assign(static_cast<std::size_t>(cached.rows.size()), {});
    if (cached.rows.empty() || innerContent.isEmpty())
    {
        return;
    }

    // Rows are laid out by FlexBox so that cardInner's own direction, justify,
    // align and gap all genuinely apply to them.
    auto box = cached.flex.toFlexBox();
    const auto gapMargin = cached.flex.gapMargin();
    const auto innerHeight = static_cast<float>(innerContent.getHeight());
    const auto innerWidth = static_cast<float>(innerContent.getWidth());

    // Row heights are resolved here rather than left to FlexBox's shrinking.
    //
    // Percentages are of the cardInner CONTENT height, and if the total -
    // including the space the gaps consume - exceeds what is available, every
    // row is scaled down by the same factor so the set always fits. That is a
    // deliberate, documented rule: relying on FlexBox to shrink explicitly
    // sized items did not work, and rows silently overflowing the card is a
    // worse outcome than rows being slightly shorter than requested.
    float requested = 0.0f;
    int visibleRows = 0;
    std::vector<float> heights;
    heights.reserve(cached.rows.size());
    for (const auto& row : cached.rows)
    {
        // A `display: none` row contributes no height and no gap - it is out of
        // the layout, not merely empty - so the rows around it close up.
        const auto height = row.flex.display ? row.height.resolve(innerHeight, innerHeight) : 0.0f;
        heights.push_back(height);
        requested += height;
        visibleRows += row.flex.display ? 1 : 0;
    }

    const auto gapTotal = cached.flex.gap * static_cast<float>(visibleRows);
    const auto usableHeight = juce::jmax(0.0f, innerHeight - gapTotal);
    const auto scale = (requested > usableHeight && requested > 0.0f)
                           ? usableHeight / requested
                           : 1.0f;

    for (std::size_t i = 0; i < heights.size(); ++i)
    {
        // Width is the full content width: a row spans cardInner by definition,
        // so it is never a percentage of anything.
        const auto margin = cached.rows[i].flex.display ? gapMargin : juce::FlexItem::Margin();
        box.items.add(juce::FlexItem(innerWidth, heights[i] * scale).withMargin(margin));
    }

    box.performLayout(innerContent.toFloat());

    for (std::size_t i = 0; i < cached.rows.size(); ++i)
    {
        const auto laidOut = box.items.getReference(static_cast<int>(i)).currentBounds;
        const auto rowMarginBox = cached.rows[i].margin.shrink(laidOut);
        rowContentBounds[i] = cached.rows[i].padding.shrink(rowMarginBox).toNearestInt();
    }
}

juce::Rectangle<int> CardInner::powerBounds() const
{
    const auto side = juce::roundToInt(cached.power.size);
    return juce::Rectangle<int>(innerContent.getX() + juce::roundToInt(cached.power.x),
                                innerContent.getY() + juce::roundToInt(cached.power.y),
                                side, side);
}

juce::Rectangle<int> CardInner::rowContent(int index) const
{
    if (index < 0 || index >= static_cast<int>(rowContentBounds.size()))
    {
        return {};
    }
    return rowContentBounds[static_cast<std::size_t>(index)];
}

juce::FlexBox CardInner::rowFlex(int index) const
{
    if (index < 0 || index >= static_cast<int>(cached.rows.size()))
    {
        return {};
    }
    return cached.rows[static_cast<std::size_t>(index)].flex.toFlexBox();
}

const ControlStyle& CardInner::rowControl(int index) const
{
    static const ControlStyle fallback;
    if (index < 0 || index >= static_cast<int>(cached.rows.size()))
    {
        return fallback;
    }
    return cached.rows[static_cast<std::size_t>(index)].control;
}

juce::FlexItem::Margin CardInner::rowGap(int index) const
{
    if (index < 0 || index >= static_cast<int>(cached.rows.size()))
    {
        return {};
    }
    return cached.rows[static_cast<std::size_t>(index)].flex.gapMargin();
}

// ---------------------------------------------------------------------------

juce::String cardTypeKey(const juce::String& styleKey)
{
    // Strip a trailing instance number. Every per-instance key in this UI is
    // "<type><n>", and the ones that are not - "subOsc", "ampEnv",
    // "mixerChannel" - have no trailing digit to strip.
    auto type = styleKey;
    while (type.isNotEmpty() && juce::CharacterFunctions::isDigit(type.getLastCharacter()))
    {
        type = type.dropLastCharacters(1);
    }
    return type.isEmpty() ? styleKey : type;
}

std::vector<float> fitRowItemWidths(const std::vector<float>& naturalWidths,
                                   float gap,
                                   float rowWidth)
{
    std::vector<float> widths = naturalWidths;
    if (widths.empty())
    {
        return widths;
    }

    // Each cell consumes its width plus the gap, which is realised as half a
    // margin on each side.
    const auto gapTotal = gap * static_cast<float>(widths.size());
    const auto usable = juce::jmax(0.0f, rowWidth - gapTotal);

    float requested = 0.0f;
    for (const auto w : widths)
    {
        requested += w;
    }

    if (requested > usable && requested > 0.0f)
    {
        const auto scale = usable / requested;
        for (auto& w : widths)
        {
            w *= scale;
        }
    }
    return widths;
}

int wrappedLineCount(const std::vector<float>& itemWidths, float gap, float rowWidth)
{
    if (rowWidth <= 0.0f || itemWidths.empty())
    {
        return 1;
    }

    int lines = 1;
    float used = 0.0f;
    for (const auto width : itemWidths)
    {
        // Each item consumes its own width plus the gap, which is realised as
        // half a margin on each side.
        const auto consumed = width + gap;
        if (used > 0.0f && used + consumed > rowWidth)
        {
            ++lines;
            used = consumed;
        }
        else
        {
            used += consumed;
        }
    }
    return juce::jmax(1, lines);
}

void layoutLabelledControl(juce::Rectangle<int> area,
                           const LabelledControl& parts,
                           const ControlStyle& style)
{
    const auto showLabel = parts.label != nullptr && parts.label->isVisible();
    const auto showReadout = parts.readout != nullptr && parts.readout->isVisible();
    // Space is kept for a readout that was asked for but is not there, so cells
    // with and without one still line up.
    const auto reserveReadout = showReadout || parts.readoutHeight > 0;

    const auto cellWidth = static_cast<float>(juce::jmax(0, area.getWidth()));
    const auto cellHeight = static_cast<float>(juce::jmax(0, area.getHeight()));
    const auto shorterSide = juce::jmin(cellWidth, cellHeight);

    // "auto" means "whatever the component asked for". Anything else overrides
    // it, with percentages measured against the cell's shorter side.
    const auto resolveOrRequested = [shorterSide](const Dimension& dim, int requested)
    {
        return dim.isAuto() ? static_cast<float>(requested)
                            : juce::jmax(0.0f, dim.resolve(shorterSide, static_cast<float>(requested)));
    };

    const auto labelHeight = showLabel ? resolveOrRequested(style.labelHeight, parts.labelHeight) : 0.0f;
    const auto readoutHeight = reserveReadout ? resolveOrRequested(style.readoutHeight, parts.readoutHeight) : 0.0f;

    const auto stacksVertically = style.direction == FlexDirection::column
                               || style.direction == FlexDirection::columnReverse;

    // The control takes what the label and readout leave, unless config gives
    // it an explicit size. This is what keeps a knob round and a dropdown not.
    const auto gapCount = static_cast<float>((showLabel ? 1 : 0) + (reserveReadout ? 1 : 0));
    const auto consumed = labelHeight + readoutHeight + style.gap * gapCount;
    const auto freeHeight = juce::jmax(0.0f, cellHeight - (stacksVertically ? consumed : 0.0f));

    const auto cap = parts.maxControlSize > 0 ? static_cast<float>(parts.maxControlSize)
                                              : std::numeric_limits<float>::max();

    float controlWidth = 0.0f;
    float controlHeight = 0.0f;

    if (parts.shape == ControlShape::stretch)
    {
        // Full width, capped height: a dropdown as tall as its cell would look
        // nothing like the ones it replaced.
        controlWidth = cellWidth;
        controlHeight = style.size.isAuto() ? juce::jmin(freeHeight, cap)
                                            : juce::jmax(0.0f, style.size.resolve(shorterSide, cap));
        controlHeight = juce::jmin(controlHeight, freeHeight);
    }
    else
    {
        const auto side = style.size.isAuto()
                              ? juce::jmin(juce::jmin(cellWidth, freeHeight), cap)
                              : juce::jmax(0.0f, style.size.resolve(shorterSide, cap));
        controlWidth = juce::jmin(side, cellWidth);
        controlHeight = juce::jmin(side, freeHeight);
    }

    // Laid out by FlexBox, like everything else here, so justifyContent and
    // alignItems mean what they mean everywhere else in this system.
    juce::FlexBox box;
    box.flexDirection = toFlexDirection(style.direction);
    box.flexWrap = juce::FlexBox::Wrap::noWrap;
    box.justifyContent = toFlexJustify(style.justifyContent);
    box.alignItems = toFlexAlign(style.alignItems);
    box.alignContent = juce::FlexBox::AlignContent::center;

    const auto half = style.gap * 0.5f;
    const auto gapMargin = stacksVertically ? juce::FlexItem::Margin(half, 0.0f, half, 0.0f)
                                            : juce::FlexItem::Margin(0.0f, half, 0.0f, half);

    int labelIndex = -1;
    int controlIndex = -1;
    int readoutIndex = -1;

    if (showLabel)
    {
        labelIndex = box.items.size();
        box.items.add(juce::FlexItem(cellWidth, labelHeight).withMargin(gapMargin));
    }
    if (parts.control != nullptr)
    {
        controlIndex = box.items.size();
        box.items.add(juce::FlexItem(controlWidth, controlHeight).withMargin(gapMargin));
    }
    if (reserveReadout)
    {
        // Added even with no component to place: the item holds the space.
        readoutIndex = showReadout ? box.items.size() : -1;
        box.items.add(juce::FlexItem(cellWidth, readoutHeight).withMargin(gapMargin));
    }

    box.performLayout(area.toFloat());

    const auto place = [&box](int index, juce::Component* component)
    {
        if (index >= 0 && component != nullptr)
        {
            component->setBounds(box.items.getReference(index).currentBounds.toNearestInt());
        }
    };

    place(labelIndex, parts.label);
    place(controlIndex, parts.control);
    place(readoutIndex, parts.readout);
}

} // namespace px3::ui
