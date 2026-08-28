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

juce::var readWithDefaults(const UIConfig* config,
                           const juce::String& defaultsPath,
                           const juce::String& stylePath,
                           const juce::String& key)
{
    return readLayered(config, { stylePath, defaultsPath }, key);
}

FlexDirection parseDirection(const juce::String& text, FlexDirection fallback)
{
    const auto t = text.trim().toLowerCase();
    if (t == "row")            return FlexDirection::row;
    if (t == "row-reverse")    return FlexDirection::rowReverse;
    if (t == "column")         return FlexDirection::column;
    if (t == "column-reverse") return FlexDirection::columnReverse;
    return fallback;
}

FlexWrapMode parseWrap(const juce::String& text, FlexWrapMode fallback)
{
    const auto t = text.trim().toLowerCase();
    if (t == "nowrap" || t == "no-wrap") return FlexWrapMode::noWrap;
    if (t == "wrap")                     return FlexWrapMode::wrap;
    return fallback;
}

JustifyContent parseJustify(const juce::String& text, JustifyContent fallback)
{
    const auto t = text.trim().toLowerCase();
    if (t == "flex-start" || t == "start")  return JustifyContent::start;
    if (t == "flex-end" || t == "end")      return JustifyContent::end;
    if (t == "center" || t == "centre")     return JustifyContent::centre;
    if (t == "space-between")               return JustifyContent::spaceBetween;
    if (t == "space-around")                return JustifyContent::spaceAround;
    return fallback;
}

AlignItems parseAlign(const juce::String& text, AlignItems fallback)
{
    const auto t = text.trim().toLowerCase();
    if (t == "flex-start" || t == "start") return AlignItems::start;
    if (t == "flex-end" || t == "end")     return AlignItems::end;
    if (t == "center" || t == "centre")    return AlignItems::centre;
    if (t == "stretch")                    return AlignItems::stretch;
    return fallback;
}
} // namespace

// ---------------------------------------------------------------------------
// FlexStyle
// ---------------------------------------------------------------------------

FlexStyle FlexStyle::read(const UIConfig* config,
                          const juce::String& defaultsPath,
                          const juce::String& stylePath,
                          const FlexStyle& fallback)
{
    return readLayered(config, { stylePath, defaultsPath }, fallback);
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
        style.display = ! v.toString().trim().equalsIgnoreCase("none");
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

juce::FlexBox FlexStyle::toFlexBox() const
{
    juce::FlexBox box;

    switch (direction)
    {
        case FlexDirection::row:           box.flexDirection = juce::FlexBox::Direction::row; break;
        case FlexDirection::rowReverse:    box.flexDirection = juce::FlexBox::Direction::rowReverse; break;
        case FlexDirection::column:        box.flexDirection = juce::FlexBox::Direction::column; break;
        case FlexDirection::columnReverse: box.flexDirection = juce::FlexBox::Direction::columnReverse; break;
    }

    box.flexWrap = (wrap == FlexWrapMode::wrap) ? juce::FlexBox::Wrap::wrap
                                                : juce::FlexBox::Wrap::noWrap;

    switch (justifyContent)
    {
        case JustifyContent::start:        box.justifyContent = juce::FlexBox::JustifyContent::flexStart; break;
        case JustifyContent::end:          box.justifyContent = juce::FlexBox::JustifyContent::flexEnd; break;
        case JustifyContent::centre:       box.justifyContent = juce::FlexBox::JustifyContent::center; break;
        case JustifyContent::spaceBetween: box.justifyContent = juce::FlexBox::JustifyContent::spaceBetween; break;
        case JustifyContent::spaceAround:  box.justifyContent = juce::FlexBox::JustifyContent::spaceAround; break;
    }

    switch (alignItems)
    {
        case AlignItems::start:   box.alignItems = juce::FlexBox::AlignItems::flexStart; break;
        case AlignItems::end:     box.alignItems = juce::FlexBox::AlignItems::flexEnd; break;
        case AlignItems::centre:  box.alignItems = juce::FlexBox::AlignItems::center; break;
        case AlignItems::stretch: box.alignItems = juce::FlexBox::AlignItems::stretch; break;
    }

    switch (alignContent)
    {
        case AlignItems::start:   box.alignContent = juce::FlexBox::AlignContent::flexStart; break;
        case AlignItems::end:     box.alignContent = juce::FlexBox::AlignContent::flexEnd; break;
        case AlignItems::centre:  box.alignContent = juce::FlexBox::AlignContent::center; break;
        case AlignItems::stretch: box.alignContent = juce::FlexBox::AlignContent::stretch; break;
    }

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
                                          const juce::String& defaultsPath,
                                          const juce::String& stylePath,
                                          int rowCount)
{
    CardInnerStyle style;
    style.rows.clear();

    if (config != nullptr)
    {
        style.margin = Insets::parse(readWithDefaults(config, defaultsPath, stylePath, "margin"), style.margin);
        style.padding = Insets::parse(readWithDefaults(config, defaultsPath, stylePath, "padding"), style.padding);
        style.flex = FlexStyle::read(config, defaultsPath, stylePath, style.flex);
    }

    // Rows share a default block so a card only declares the ones that differ.
    const RowStyle rowFallback { Dimension { Dimension::Unit::percent, 100.0f / juce::jmax(1, rowCount) },
                                 {}, {},
                                 FlexStyle { true, FlexDirection::row, FlexWrapMode::noWrap,
                                             JustifyContent::centre, AlignItems::centre,
                                             AlignItems::centre, 0.0f } };

    for (int i = 0; i < rowCount; ++i)
    {
        RowStyle row = rowFallback;
        if (config != nullptr)
        {
            // Most specific first: this card's row N, then the shared default
            // for row N, then the shared default for any row.
            const auto rowKey = ".rows.row" + juce::String(i + 1);
            const juce::StringArray paths {
                stylePath + rowKey,
                defaultsPath + rowKey,
                defaultsPath + ".rows.default",
            };

            auto merged = rowFallback;
            merged.height = Dimension::parse(readLayered(config, paths, "height"), merged.height);
            merged.margin = Insets::parse(readLayered(config, paths, "margin"), merged.margin);
            merged.padding = Insets::parse(readLayered(config, paths, "padding"), merged.padding);
            merged.flex = FlexStyle::readLayered(config, paths, merged.flex);
            row = merged;
        }
        style.rows.push_back(row);
    }

    return style;
}

// ---------------------------------------------------------------------------
// CardInner
// ---------------------------------------------------------------------------

void CardInner::setKeys(juce::String defaultsPathIn, juce::String stylePathIn)
{
    if (defaultsPath != defaultsPathIn || stylePath != stylePathIn)
    {
        defaultsPath = std::move(defaultsPathIn);
        stylePath = std::move(stylePathIn);
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
        cached = CardInnerStyle::fromConfig(config.get(), defaultsPath, stylePath, rows);
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

juce::FlexItem::Margin CardInner::rowGap(int index) const
{
    if (index < 0 || index >= static_cast<int>(cached.rows.size()))
    {
        return {};
    }
    return cached.rows[static_cast<std::size_t>(index)].flex.gapMargin();
}

// ---------------------------------------------------------------------------

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
                           juce::Component* label,
                           juce::Component* control,
                           juce::Component* readout,
                           int labelHeight,
                           int readoutHeight,
                           ControlShape shape,
                           int maxControlSize)
{
    auto remaining = area;

    if (label != nullptr && label->isVisible())
    {
        label->setBounds(remaining.removeFromTop(juce::jmin(labelHeight, remaining.getHeight())));
    }
    if (readout != nullptr && readout->isVisible())
    {
        readout->setBounds(remaining.removeFromBottom(juce::jmin(readoutHeight, remaining.getHeight())));
    }
    if (control != nullptr)
    {
        const auto cap = maxControlSize > 0 ? maxControlSize : std::numeric_limits<int>::max();

        if (shape == ControlShape::stretch)
        {
            // Full width at a capped height: a dropdown that grew as tall as
            // its cell would look nothing like the ones it replaced.
            const auto height = juce::jmax(0, juce::jmin(remaining.getHeight(), cap));
            control->setBounds(juce::Rectangle<int>(remaining.getWidth(), height)
                                   .withCentre(remaining.getCentre()));
        }
        else
        {
            // Square and centred: knobs are round, and letting one stretch to
            // the cell would change how every existing knob looks.
            const auto side = juce::jmax(0, juce::jmin(juce::jmin(remaining.getWidth(),
                                                                  remaining.getHeight()), cap));
            control->setBounds(juce::Rectangle<int>(side, side).withCentre(remaining.getCentre()));
        }
    }
}

} // namespace px3::ui
