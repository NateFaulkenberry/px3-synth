#include "UILayout.h"

#include <cmath>
#include <map>
#include <set>

namespace px3::ui
{
namespace
{
bool readNumber(const juce::var& object, const juce::Identifier& key, float& result)
{
    const auto value = object.getProperty(key, {});
    if (! (value.isInt() || value.isInt64() || value.isDouble()))
    {
        return false;
    }

    result = static_cast<float>(static_cast<double>(value));
    return std::isfinite(result);
}

bool readString(const juce::var& object, const juce::Identifier& key, juce::String& result)
{
    const auto value = object.getProperty(key, {});
    if (! value.isString())
    {
        return false;
    }

    result = value.toString();
    return true;
}

juce::String kindName(InstrumentSceneNodeKind kind)
{
    switch (kind)
    {
        case InstrumentSceneNodeKind::container: return "container";
        case InstrumentSceneNodeKind::parameterControl: return "parameterControl";
        case InstrumentSceneNodeKind::display: return "display";
        case InstrumentSceneNodeKind::decoration: return "decoration";
    }
    return {};
}

juce::String layoutName(InstrumentSceneLayoutMode mode)
{
    switch (mode)
    {
        case InstrumentSceneLayoutMode::absolute: return "absolute";
        case InstrumentSceneLayoutMode::row: return "row";
        case InstrumentSceneLayoutMode::column: return "column";
        case InstrumentSceneLayoutMode::grid: return "grid";
        case InstrumentSceneLayoutMode::overlay: return "overlay";
    }
    return {};
}

bool parseKind(const juce::String& text, InstrumentSceneNodeKind& result)
{
    if (text == "container") { result = InstrumentSceneNodeKind::container; return true; }
    if (text == "parameterControl") { result = InstrumentSceneNodeKind::parameterControl; return true; }
    if (text == "display") { result = InstrumentSceneNodeKind::display; return true; }
    if (text == "decoration") { result = InstrumentSceneNodeKind::decoration; return true; }
    return false;
}

bool parseLayout(const juce::String& text, InstrumentSceneLayoutMode& result)
{
    if (text == "absolute") { result = InstrumentSceneLayoutMode::absolute; return true; }
    if (text == "row") { result = InstrumentSceneLayoutMode::row; return true; }
    if (text == "column") { result = InstrumentSceneLayoutMode::column; return true; }
    if (text == "grid") { result = InstrumentSceneLayoutMode::grid; return true; }
    if (text == "overlay") { result = InstrumentSceneLayoutMode::overlay; return true; }
    return false;
}

juce::var makeSizeValue(juce::Point<float> size)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("width", static_cast<double>(size.x));
    object->setProperty("height", static_cast<double>(size.y));
    return juce::var(object);
}

bool readSizeValue(const juce::var& value, juce::Point<float>& size)
{
    return value.getDynamicObject() != nullptr
        && readNumber(value, "width", size.x)
        && readNumber(value, "height", size.y);
}

juce::var makeStyleValue(const InstrumentSceneStyleToken& token)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("id", token.id);
    value->setProperty("background", token.background);
    value->setProperty("foreground", token.foreground);
    value->setProperty("accent", token.accent);
    value->setProperty("texture", token.texture);
    value->setProperty("knobDiameter", static_cast<double>(token.knobDiameter));
    value->setProperty("labelSize", static_cast<double>(token.labelSize));
    value->setProperty("borderRadius", static_cast<double>(token.borderRadius));
    value->setProperty("borderWidth", static_cast<double>(token.borderWidth));
    return juce::var(value);
}

bool validHexColour(const juce::String& value)
{
    if (! value.startsWithChar('#') || (value.length() != 7 && value.length() != 9))
    {
        return false;
    }
    for (int i = 1; i < value.length(); ++i)
    {
        const auto c = value[i];
        if (! ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }
    return true;
}

juce::var makeNodeValue(const InstrumentSceneNode& node)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("id", node.id);
    value->setProperty("parentId", node.parentId);
    value->setProperty("kind", kindName(node.kind));
    value->setProperty("layout", layoutName(node.layout));
    value->setProperty("bindingId", node.bindingId);
    value->setProperty("styleToken", node.styleToken);
    value->setProperty("label", node.label);
    value->setProperty("order", node.order);
    value->setProperty("visible", node.visible);
    value->setProperty("minimumSize", makeSizeValue(node.minimumSize));
    value->setProperty("maximumSize", makeSizeValue(node.maximumSize));
    value->setProperty("flexGrow", static_cast<double>(node.flexGrow));
    value->setProperty("spacing", static_cast<double>(node.spacing));
    value->setProperty("gridColumns", node.gridColumns);

    auto* bounds = new juce::DynamicObject();
    bounds->setProperty("x", static_cast<double>(node.bounds.getX()));
    bounds->setProperty("y", static_cast<double>(node.bounds.getY()));
    bounds->setProperty("width", static_cast<double>(node.bounds.getWidth()));
    bounds->setProperty("height", static_cast<double>(node.bounds.getHeight()));
    value->setProperty("bounds", juce::var(bounds));
    return juce::var(value);
}
}

bool InstrumentSceneDocument::loadJson(const juce::String& text, juce::String& error)
{
    error.clear();

    juce::var root;
    const auto parseResult = juce::JSON::parse(text, root);
    if (parseResult.failed())
    {
        error = "Invalid scene JSON: " + parseResult.getErrorMessage();
        return false;
    }

    if (root.getDynamicObject() == nullptr)
    {
        error = "Scene root must be an object.";
        return false;
    }

    const auto version = root.getProperty("schemaVersion", {});
    if (! version.isInt() || static_cast<int>(version) != currentSchemaVersion)
    {
        error = "Unsupported layout schema version.";
        return false;
    }

    const auto nodeValues = root.getProperty("nodes", {});
    auto* array = nodeValues.getArray();
    if (array == nullptr)
    {
        error = "Scene nodes must be an array.";
        return false;
    }

    const auto styleValues = root.getProperty("styles", {});
    auto* styleArray = styleValues.getArray();
    if (styleArray == nullptr)
    {
        error = "Scene styles must be an array.";
        return false;
    }

    std::vector<InstrumentSceneStyleToken> parsedStyles;
    parsedStyles.reserve(static_cast<std::size_t>(styleArray->size()));
    for (const auto& value : *styleArray)
    {
        if (value.getDynamicObject() == nullptr)
        {
            error = "Each scene style token must be an object.";
            return false;
        }

        InstrumentSceneStyleToken token;
        if (! readString(value, "id", token.id)
            || ! readString(value, "background", token.background)
            || ! readString(value, "foreground", token.foreground)
            || ! readString(value, "accent", token.accent)
            || ! readString(value, "texture", token.texture)
            || ! readNumber(value, "knobDiameter", token.knobDiameter)
            || ! readNumber(value, "labelSize", token.labelSize)
            || ! readNumber(value, "borderRadius", token.borderRadius)
            || ! readNumber(value, "borderWidth", token.borderWidth))
        {
            error = "Scene style token is missing a valid property.";
            return false;
        }
        parsedStyles.push_back(std::move(token));
    }

    std::vector<InstrumentSceneNode> parsedNodes;
    parsedNodes.reserve(static_cast<std::size_t>(array->size()));
    for (const auto& value : *array)
    {
        if (value.getDynamicObject() == nullptr)
        {
            error = "Each scene node must be an object.";
            return false;
        }

        InstrumentSceneNode node;
        auto kindText = juce::String();
        auto layoutText = juce::String();
        auto x = 0.0f;
        auto y = 0.0f;
        auto width = 0.0f;
        auto height = 0.0f;
        auto orderValue = value.getProperty("order", {});
        const auto visibleValue = value.getProperty("visible", true);
        const auto gridColumnsValue = value.getProperty("gridColumns", 0);
        const auto flexGrowValue = value.getProperty("flexGrow", 1.0);
        const auto spacingValue = value.getProperty("spacing", 0.0);
        const auto boundsValue = value.getProperty("bounds", {});
        if (! readString(value, "id", node.id)
            || ! readString(value, "parentId", node.parentId)
            || ! readString(value, "kind", kindText)
            || ! readString(value, "layout", layoutText)
            || ! readString(value, "bindingId", node.bindingId)
            || ! readString(value, "styleToken", node.styleToken)
            || ! readString(value, "label", node.label)
            || ! orderValue.isInt()
            || ! visibleValue.isBool()
            || ! gridColumnsValue.isInt()
            || ! (flexGrowValue.isInt() || flexGrowValue.isInt64() || flexGrowValue.isDouble())
            || ! (spacingValue.isInt() || spacingValue.isInt64() || spacingValue.isDouble())
            || ! parseKind(kindText, node.kind)
            || ! parseLayout(layoutText, node.layout)
            || ! readSizeValue(value.getProperty("minimumSize", {}), node.minimumSize)
            || ! readSizeValue(value.getProperty("maximumSize", {}), node.maximumSize)
            || boundsValue.getDynamicObject() == nullptr
            || ! readNumber(boundsValue, "x", x)
            || ! readNumber(boundsValue, "y", y)
            || ! readNumber(boundsValue, "width", width)
            || ! readNumber(boundsValue, "height", height))
        {
            error = "Scene node is missing a valid identity, type, layout or constraints.";
            return false;
        }

        node.order = static_cast<int>(orderValue);
        node.visible = static_cast<bool>(visibleValue);
        node.gridColumns = static_cast<int>(gridColumnsValue);
        node.flexGrow = static_cast<float>(static_cast<double>(flexGrowValue));
        node.spacing = static_cast<float>(static_cast<double>(spacingValue));
        node.bounds = { x, y, width, height };
        parsedNodes.push_back(std::move(node));
    }

    const auto previous = std::move(nodes);
    const auto previousStyles = std::move(styleTokens);
    nodes = std::move(parsedNodes);
    styleTokens = std::move(parsedStyles);
    if (! validate(error))
    {
        nodes = previous;
        styleTokens = previousStyles;
        return false;
    }

    return true;
}

juce::String InstrumentSceneDocument::toJson() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty("schemaVersion", currentSchemaVersion);

    juce::Array<juce::var> styleValues;
    for (const auto& style : styleTokens)
    {
        styleValues.add(makeStyleValue(style));
    }
    root->setProperty("styles", juce::var(styleValues));

    juce::Array<juce::var> nodeValues;
    for (const auto& region : nodes)
    {
        nodeValues.add(makeNodeValue(region));
    }
    root->setProperty("nodes", juce::var(nodeValues));
    return juce::JSON::toString(juce::var(root), true);
}

bool InstrumentSceneDocument::addStyleToken(const InstrumentSceneStyleToken& token, juce::String& error)
{
    if (token.id.trim().isEmpty() || ! validHexColour(token.background)
        || ! validHexColour(token.foreground) || ! validHexColour(token.accent)
        || token.texture.trim().isEmpty() || ! std::isfinite(token.knobDiameter)
        || ! std::isfinite(token.labelSize) || ! std::isfinite(token.borderRadius)
        || ! std::isfinite(token.borderWidth) || token.knobDiameter <= 0.0f
        || token.labelSize <= 0.0f || token.borderRadius < 0.0f || token.borderWidth < 0.0f)
    {
        error = "Scene style token has invalid colors or dimensions.";
        return false;
    }
    if (std::any_of(styleTokens.begin(), styleTokens.end(), [&token](const auto& existing)
        { return existing.id == token.id; }))
    {
        error = "Duplicate scene style token id: " + token.id;
        return false;
    }

    styleTokens.push_back(token);
    if (nodes.empty())
    {
        error.clear();
        return true;
    }
    if (validate(error))
    {
        return true;
    }

    styleTokens.pop_back();
    return false;
}

bool InstrumentSceneDocument::addNode(const InstrumentSceneNode& region, juce::String& error)
{
    nodes.push_back(region);
    if (validate(error))
    {
        return true;
    }

    nodes.pop_back();
    return false;
}

bool InstrumentSceneDocument::setNodeStyleToken(const juce::String& id,
                                                const juce::String& styleToken,
                                                juce::String& error)
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& node)
    {
        return node.id == id;
    });
    if (index == nodes.end())
    {
        error = "Unknown scene node: " + id;
        return false;
    }

    const auto previous = index->styleToken;
    index->styleToken = styleToken;
    if (validate(error))
    {
        return true;
    }

    index->styleToken = previous;
    return false;
}

bool InstrumentSceneDocument::setLayoutMode(const juce::String& id,
                                             InstrumentSceneLayoutMode layout,
                                             juce::String& error)
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& node)
    {
        return node.id == id;
    });
    if (index == nodes.end())
    {
        error = "Unknown scene node: " + id;
        return false;
    }

    const auto previous = index->layout;
    index->layout = layout;
    if (validate(error))
    {
        return true;
    }
    index->layout = previous;
    return false;
}

bool InstrumentSceneDocument::setFlowProperties(const juce::String& id,
                                                 float flexGrow,
                                                 float spacing,
                                                 int gridColumns,
                                                 juce::String& error)
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& node)
    {
        return node.id == id;
    });
    if (index == nodes.end())
    {
        error = "Unknown scene node: " + id;
        return false;
    }

    const auto previousFlex = index->flexGrow;
    const auto previousSpacing = index->spacing;
    const auto previousColumns = index->gridColumns;
    index->flexGrow = flexGrow;
    index->spacing = spacing;
    index->gridColumns = gridColumns;
    if (validate(error))
    {
        return true;
    }

    index->flexGrow = previousFlex;
    index->spacing = previousSpacing;
    index->gridColumns = previousColumns;
    return false;
}

bool InstrumentSceneDocument::setParentNode(const juce::String& id,
                                            const juce::String& parentId,
                                            juce::String& error)
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& node)
    {
        return node.id == id;
    });
    if (index == nodes.end())
    {
        error = "Unknown scene node: " + id;
        return false;
    }

    const auto previous = index->parentId;
    index->parentId = parentId;
    if (validate(error))
    {
        return true;
    }

    index->parentId = previous;
    return false;
}

bool InstrumentSceneDocument::setBounds(const juce::String& id,
                                 juce::Rectangle<float> bounds,
                                 juce::String& error)
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& region)
    {
        return region.id == id;
    });
    if (index == nodes.end())
    {
        error = "Unknown layout region: " + id;
        return false;
    }

    const auto previous = index->bounds;
    index->bounds = bounds;
    if (validate(error))
    {
        return true;
    }

    index->bounds = previous;
    return false;
}

bool InstrumentSceneDocument::setOrder(const juce::String& id, int order, juce::String& error)
{
    auto target = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& region)
    {
        return region.id == id;
    });
    if (target == nodes.end())
    {
        error = "Unknown layout region: " + id;
        return false;
    }

    auto previous = nodes;
    std::vector<std::size_t> siblings;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        if (nodes[i].parentId == target->parentId)
        {
            siblings.push_back(i);
        }
    }
    std::sort(siblings.begin(), siblings.end(), [this](std::size_t a, std::size_t b)
    {
        if (nodes[a].order != nodes[b].order) { return nodes[a].order < nodes[b].order; }
        return nodes[a].id < nodes[b].id;
    });

    const auto targetIndex = static_cast<std::size_t>(std::distance(nodes.begin(), target));
    const auto oldPosition = std::find(siblings.begin(), siblings.end(), targetIndex);
    auto movedIndex = static_cast<int>(std::distance(siblings.begin(), oldPosition));
    const auto newPosition = juce::jlimit(0, static_cast<int>(siblings.size()) - 1, order);
    if (movedIndex != newPosition)
    {
        siblings.erase(oldPosition);
        siblings.insert(siblings.begin() + newPosition, targetIndex);
    }

    for (std::size_t i = 0; i < siblings.size(); ++i)
    {
        nodes[siblings[i]].order = static_cast<int>(i);
    }

    if (validate(error))
    {
        return true;
    }

    nodes = std::move(previous);
    return false;
}

const InstrumentSceneNode* InstrumentSceneDocument::findNode(const juce::String& id) const noexcept
{
    const auto index = std::find_if(nodes.begin(), nodes.end(), [&id](const auto& region)
    {
        return region.id == id;
    });
    return index == nodes.end() ? nullptr : &*index;
}

const InstrumentSceneStyleToken* InstrumentSceneDocument::findStyleToken(const juce::String& id) const noexcept
{
    const auto index = std::find_if(styleTokens.begin(), styleTokens.end(), [&id](const auto& token)
    {
        return token.id == id;
    });
    return index == styleTokens.end() ? nullptr : &*index;
}

juce::Rectangle<float> InstrumentSceneDocument::resolveBounds(
    const juce::String& id,
    juce::Rectangle<float> rootBounds) const
{
    const auto clampDimension = [](float value, float minimum, float maximum, float available)
    {
        const auto upper = maximum > 0.0f ? juce::jmin(maximum, available) : available;
        return juce::jlimit(juce::jmin(minimum, upper), upper, value);
    };

    std::function<juce::Rectangle<float>(const juce::String&, int)> resolve;
    resolve = [&](const juce::String& nodeId, int depth) -> juce::Rectangle<float>
    {
        const auto* node = findNode(nodeId);
        if (node == nullptr || depth > 64)
        {
            return {};
        }
        if (node->parentId.isEmpty())
        {
            return rootBounds;
        }

        const auto* parent = findNode(node->parentId);
        if (parent == nullptr || parent->kind != InstrumentSceneNodeKind::container)
        {
            return {};
        }
        const auto parentBounds = resolve(parent->id, depth + 1);
        if (parentBounds.isEmpty())
        {
            return {};
        }

        const auto clampToParent = [&](juce::Rectangle<float> bounds)
        {
            const auto width = clampDimension(bounds.getWidth(), node->minimumSize.x,
                                              node->maximumSize.x, parentBounds.getWidth());
            const auto height = clampDimension(bounds.getHeight(), node->minimumSize.y,
                                               node->maximumSize.y, parentBounds.getHeight());
            const auto x = juce::jlimit(parentBounds.getX(), parentBounds.getRight() - width,
                                        bounds.getX());
            const auto y = juce::jlimit(parentBounds.getY(), parentBounds.getBottom() - height,
                                        bounds.getY());
            return juce::Rectangle<float>(x, y, width, height);
        };

        if (parent->layout == InstrumentSceneLayoutMode::absolute
            || parent->layout == InstrumentSceneLayoutMode::overlay)
        {
            return clampToParent({ parentBounds.getX() + node->bounds.getX() * parentBounds.getWidth(),
                                   parentBounds.getY() + node->bounds.getY() * parentBounds.getHeight(),
                                   node->bounds.getWidth() * parentBounds.getWidth(),
                                   node->bounds.getHeight() * parentBounds.getHeight() });
        }

        std::vector<const InstrumentSceneNode*> siblings;
        for (const auto& candidate : nodes)
        {
            if (candidate.parentId == parent->id && candidate.visible)
            {
                siblings.push_back(&candidate);
            }
        }
        std::sort(siblings.begin(), siblings.end(), [](const auto* a, const auto* b)
        {
            return a->order != b->order ? a->order < b->order : a->id < b->id;
        });
        const auto target = std::find_if(siblings.begin(), siblings.end(), [&nodeId](const auto* candidate)
        {
            return candidate->id == nodeId;
        });
        if (target == siblings.end() || siblings.empty())
        {
            return {};
        }

        const auto count = static_cast<int>(siblings.size());
        if (parent->layout == InstrumentSceneLayoutMode::grid)
        {
            const auto columns = parent->gridColumns > 0
                                    ? juce::jlimit(1, count, parent->gridColumns)
                                    : juce::jmax(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count)))));
            const auto rows = (count + columns - 1) / columns;
            const auto gapX = juce::jmin(parent->spacing,
                                         parentBounds.getWidth() / static_cast<float>(juce::jmax(1, columns - 1)));
            const auto gapY = juce::jmin(parent->spacing,
                                         parentBounds.getHeight() / static_cast<float>(juce::jmax(1, rows - 1)));
            const auto columnCount = static_cast<float>(columns);
            const auto rowCount = static_cast<float>(rows);
            const auto cellWidth = juce::jmax(
                0.0f, (parentBounds.getWidth() - gapX * static_cast<float>(columns - 1)) / columnCount);
            const auto cellHeight = juce::jmax(
                0.0f, (parentBounds.getHeight() - gapY * static_cast<float>(rows - 1)) / rowCount);
            const auto index = static_cast<int>(std::distance(siblings.begin(), target));
            const auto column = index % columns;
            const auto row = index / columns;
            return clampToParent({ parentBounds.getX() + static_cast<float>(column) * (cellWidth + gapX),
                                   parentBounds.getY() + static_cast<float>(row) * (cellHeight + gapY),
                                   cellWidth, cellHeight });
        }

        const auto rowLayout = parent->layout == InstrumentSceneLayoutMode::row;
        const auto available = rowLayout ? parentBounds.getWidth() : parentBounds.getHeight();
        const auto gap = juce::jmin(parent->spacing,
                                    available / static_cast<float>(juce::jmax(1, count - 1)));
        const auto totalAvailable = juce::jmax(0.0f, available - gap * static_cast<float>(count - 1));
        auto totalGrow = 0.0f;
        for (const auto* sibling : siblings)
        {
            totalGrow += sibling->flexGrow;
        }
        const auto equalShare = totalAvailable / static_cast<float>(count);
        auto cursor = rowLayout ? parentBounds.getX() : parentBounds.getY();
        for (const auto* sibling : siblings)
        {
            const auto growShare = totalGrow > 0.0f
                                     ? totalAvailable * sibling->flexGrow / totalGrow
                                     : equalShare;
            const auto flexMinimum = rowLayout ? sibling->minimumSize.x : sibling->minimumSize.y;
            const auto flexMaximum = rowLayout ? sibling->maximumSize.x : sibling->maximumSize.y;
            const auto flexSize = clampDimension(growShare, flexMinimum, flexMaximum, available);
            juce::Rectangle<float> bounds;
            if (rowLayout)
            {
                bounds = { cursor, parentBounds.getY(), flexSize, parentBounds.getHeight() };
            }
            else
            {
                bounds = { parentBounds.getX(), cursor, parentBounds.getWidth(), flexSize };
            }
            if (sibling->id == nodeId)
            {
                return clampToParent(bounds);
            }
            cursor += flexSize + gap;
        }
        return {};
    };

    return resolve(id, 0);
}

void InstrumentSceneDocument::beginTransaction()
{
    if (transactionSnapshot.isEmpty())
    {
        transactionSnapshot = toJson();
    }
}

bool InstrumentSceneDocument::commitTransaction()
{
    if (transactionSnapshot.isEmpty())
    {
        return false;
    }

    const auto before = transactionSnapshot;
    transactionSnapshot.clear();
    const auto after = toJson();
    if (before == after)
    {
        return false;
    }

    constexpr std::size_t kHistoryLimit = 100;
    undoSnapshots.push_back(before);
    if (undoSnapshots.size() > kHistoryLimit)
    {
        undoSnapshots.erase(undoSnapshots.begin());
    }
    redoSnapshots.clear();
    return true;
}

void InstrumentSceneDocument::cancelTransaction()
{
    if (transactionSnapshot.isNotEmpty())
    {
        restoreSnapshot(transactionSnapshot);
        transactionSnapshot.clear();
    }
}

bool InstrumentSceneDocument::undo()
{
    if (transactionSnapshot.isNotEmpty() || undoSnapshots.empty())
    {
        return false;
    }

    const auto current = toJson();
    const auto previous = undoSnapshots.back();
    if (! restoreSnapshot(previous))
    {
        return false;
    }

    undoSnapshots.pop_back();
    redoSnapshots.push_back(current);
    return true;
}

bool InstrumentSceneDocument::redo()
{
    if (transactionSnapshot.isNotEmpty() || redoSnapshots.empty())
    {
        return false;
    }

    const auto current = toJson();
    const auto next = redoSnapshots.back();
    if (! restoreSnapshot(next))
    {
        return false;
    }

    redoSnapshots.pop_back();
    undoSnapshots.push_back(current);
    return true;
}

bool InstrumentSceneDocument::restoreSnapshot(const juce::String& snapshot)
{
    juce::String ignoredError;
    return loadJson(snapshot, ignoredError);
}

bool InstrumentSceneDocument::validate(juce::String& error) const
{
    error.clear();
    std::map<juce::String, const InstrumentSceneNode*> byId;
    std::map<juce::String, const InstrumentSceneStyleToken*> stylesById;
    std::map<juce::String, std::set<int>> ordersByParent;
    auto rootCount = 0;

    for (const auto& style : styleTokens)
    {
        if (style.id.trim().isEmpty() || ! validHexColour(style.background)
            || ! validHexColour(style.foreground) || ! validHexColour(style.accent)
            || style.texture.trim().isEmpty() || ! std::isfinite(style.knobDiameter)
            || ! std::isfinite(style.labelSize) || ! std::isfinite(style.borderRadius)
            || ! std::isfinite(style.borderWidth) || style.knobDiameter <= 0.0f
            || style.labelSize <= 0.0f || style.borderRadius < 0.0f || style.borderWidth < 0.0f)
        {
            error = "Scene style tokens must have valid ids, colors and positive dimensions.";
            return false;
        }
        if (! stylesById.emplace(style.id, &style).second)
        {
            error = "Duplicate scene style token id: " + style.id;
            return false;
        }
    }

    for (const auto& region : nodes)
    {
        if (region.id.trim().isEmpty() || region.styleToken.trim().isEmpty())
        {
            error = "Scene node ids and style tokens must not be empty.";
            return false;
        }
        if (! byId.emplace(region.id, &region).second)
        {
            error = "Duplicate layout region id: " + region.id;
            return false;
        }
        if (stylesById.find(region.styleToken) == stylesById.end())
        {
            error = "Unknown style token on scene node: " + region.styleToken;
            return false;
        }

        if (region.parentId.isEmpty())
        {
            ++rootCount;
            if (region.kind != InstrumentSceneNodeKind::container)
            {
                error = "The scene root must be a container node.";
                return false;
            }
        }

        if (region.kind == InstrumentSceneNodeKind::parameterControl && region.bindingId.trim().isEmpty())
        {
            error = "Parameter-control nodes require a binding id.";
            return false;
        }
        if (region.kind != InstrumentSceneNodeKind::parameterControl && region.bindingId.isNotEmpty())
        {
            error = "Only parameter-control nodes may have a parameter binding.";
            return false;
        }

        if (! std::isfinite(region.minimumSize.x) || ! std::isfinite(region.minimumSize.y)
            || ! std::isfinite(region.maximumSize.x) || ! std::isfinite(region.maximumSize.y)
            || ! std::isfinite(region.flexGrow) || ! std::isfinite(region.spacing)
            || region.minimumSize.x < 0.0f || region.minimumSize.y < 0.0f
            || region.maximumSize.x < 0.0f || region.maximumSize.y < 0.0f
            || region.flexGrow < 0.0f || region.spacing < 0.0f || region.gridColumns < 0
            || (region.maximumSize.x > 0.0f && region.maximumSize.x < region.minimumSize.x)
            || (region.maximumSize.y > 0.0f && region.maximumSize.y < region.minimumSize.y))
        {
            error = "Scene node size constraints must be finite and non-negative.";
            return false;
        }

        const auto bounds = region.bounds;
        if (! std::isfinite(bounds.getX()) || ! std::isfinite(bounds.getY())
            || ! std::isfinite(bounds.getWidth()) || ! std::isfinite(bounds.getHeight())
            || bounds.getX() < 0.0f || bounds.getY() < 0.0f
            || bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f
            || bounds.getRight() > 1.0f || bounds.getBottom() > 1.0f)
        {
            error = "Scene bounds must be finite, positive and fit within the parent.";
            return false;
        }

        if (! ordersByParent[region.parentId].insert(region.order).second)
        {
            error = "Sibling layout order values must be unique.";
            return false;
        }
    }

    if (rootCount != 1)
    {
        error = "Scene document must have exactly one root container.";
        return false;
    }

    for (const auto& region : nodes)
    {
        if (region.parentId.isNotEmpty() && byId.find(region.parentId) == byId.end())
        {
            error = "Unknown parent scene node: " + region.parentId;
            return false;
        }
        if (region.parentId.isNotEmpty()
            && byId.at(region.parentId)->kind != InstrumentSceneNodeKind::container)
        {
            error = "Only container nodes may own child nodes.";
            return false;
        }

        std::set<juce::String> visited;
        auto* current = &region;
        while (current->parentId.isNotEmpty())
        {
            if (! visited.insert(current->id).second)
            {
                error = "Scene hierarchy contains a cycle.";
                return false;
            }
            current = byId.at(current->parentId);
        }
    }

    return true;
}
}