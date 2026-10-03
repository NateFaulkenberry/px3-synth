#include "UILayout.h"

#include <algorithm>
#include <cmath>
#include <limits>

// The layout engine.
//
// A deliberately small subset of CSS flexbox and grid, implemented directly so
// that its behaviour is ours to pin in tests and cheap enough to run on every
// resize:
//
//  flex  direction row/column, wrap, gapX/gapY, padding, child margin,
//        align-items / align-self (start/center/end/stretch), justify-content
//        (start/center/end/space-between/space-around/space-evenly),
//        basis px/%/auto, grow, shrink, min/max px/% with the CSS freeze loop
//        (a clamped item gives its share back to the others), order,
//        align-content: stretch for wrapped lines.
//  grid  column template px/%/fr/auto, row template (or implicit equal rows),
//        gaps, explicit or auto placement, column/row spans.
//  none  overlay: relative children each fill the content box.
//  absolute children of any container: x/y/width/height px/% from the
//        parent's box, auto size = fill to the far edge.
//
// Leaves have no intrinsic content size: an auto size with nothing to grow
// into is 0. Every size that matters is in the scene, which is the point.

namespace px3::ui
{
namespace
{
constexpr float kInf = std::numeric_limits<float>::infinity();

bool isNumber(const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

bool readFinite(const juce::var& v, float& out)
{
    if (! isNumber(v)) { return false; }
    out = static_cast<float>(static_cast<double>(v));
    return std::isfinite(out);
}

juce::var num(float v)
{
    // Whole numbers are written as ints so the file stays readable.
    if (std::abs(v - std::round(v)) < 1.0e-6f && std::abs(v) < 1.0e9f)
    {
        return juce::var(static_cast<int>(std::lround(v)));
    }
    return juce::var(std::round(static_cast<double>(v) * 10000.0) / 10000.0);
}

bool readEdges(const juce::var& v, Edges& out)
{
    float single = 0.0f;
    if (readFinite(v, single))
    {
        out = { single, single, single, single };
        return true;
    }
    if (auto* a = v.getArray(); a != nullptr && (a->size() == 2 || a->size() == 4))
    {
        float values[4] {};
        for (int i = 0; i < a->size(); ++i)
        {
            if (! readFinite(a->getReference(i), values[i])) { return false; }
        }
        if (a->size() == 2) { out = { values[0], values[1], values[0], values[1] }; }
        else { out = { values[0], values[1], values[2], values[3] }; }
        return true;
    }
    return false;
}

juce::var edgesVar(const Edges& e)
{
    if (e.top == e.right && e.right == e.bottom && e.bottom == e.left) { return num(e.top); }
    juce::Array<juce::var> a { num(e.top), num(e.right), num(e.bottom), num(e.left) };
    return a;
}

const juce::StringArray& nodeKeys()
{
    static const juce::StringArray keys {
        "id", "parent", "kind", "label", "binding", "style", "order", "visible", "locked",
        "layout", "direction", "wrap", "gap", "gapX", "gapY", "padding", "alignItems", "justify",
        "columns", "rows", "position", "x", "y", "width", "height", "basis", "grow", "shrink",
        "minWidth", "minHeight", "maxWidth", "maxHeight", "margin", "alignSelf", "column", "row",
        "columnSpan", "rowSpan", "aspect", "note"
    };
    return keys;
}

bool hexColour(const juce::String& value)
{
    if (! value.startsWithChar('#') || (value.length() != 7 && value.length() != 9)) { return false; }
    for (int i = 1; i < value.length(); ++i)
    {
        if (juce::CharacterFunctions::getHexDigitValue(value[i]) < 0) { return false; }
    }
    return true;
}

bool validStyle(const InstrumentSceneStyleToken& t)
{
    return t.id.trim().isNotEmpty() && hexColour(t.background) && hexColour(t.foreground)
        && hexColour(t.accent) && t.texture.trim().isNotEmpty() && std::isfinite(t.knobDiameter)
        && std::isfinite(t.labelSize) && std::isfinite(t.borderRadius) && std::isfinite(t.borderWidth)
        && t.knobDiameter > 0.0f && t.labelSize > 0.0f && t.borderRadius >= 0.0f && t.borderWidth >= 0.0f;
}

bool readNode(const juce::var& value, InstrumentSceneNode& node, juce::String& error)
{
    auto* object = value.getDynamicObject();
    if (object == nullptr)
    {
        error = "Each scene node must be an object.";
        return false;
    }

    const auto fail = [&](const juce::String& what)
    {
        error = "Node '" + node.id + "': " + what;
        return false;
    };

    const auto idValue = value.getProperty("id", {});
    if (! idValue.isString()) { error = "Scene node without a string id."; return false; }
    node.id = idValue.toString();

    for (const auto& property : object->getProperties())
    {
        if (! nodeKeys().contains(property.name.toString()))
        {
            return fail("unknown property '" + property.name.toString() + "'");
        }
    }

    const auto str = [&](const char* key, juce::String& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        if (v.isVoid()) { return true; }
        if (! v.isString()) { return false; }
        out = v.toString();
        return true;
    };
    const auto flt = [&](const char* key, float& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        return v.isVoid() || readFinite(v, out);
    };
    const auto integer = [&](const char* key, int& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        if (v.isVoid()) { return true; }
        if (! (v.isInt() || v.isInt64())) { return false; }
        out = static_cast<int>(v);
        return true;
    };
    const auto boolean = [&](const char* key, bool& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        if (v.isVoid()) { return true; }
        if (! v.isBool()) { return false; }
        out = static_cast<bool>(v);
        return true;
    };
    const auto length = [&](const char* key, Length& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        return v.isVoid() || Length::fromVar(v, out, false);
    };
    const auto enumeration = [&](const char* key, auto& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        if (v.isVoid()) { return true; }
        return v.isString() && parse(v.toString(), out);
    };
    const auto tracks = [&](const char* key, std::vector<Length>& out) -> bool
    {
        const auto v = value.getProperty(key, {});
        if (v.isVoid()) { return true; }
        auto* a = v.getArray();
        if (a == nullptr) { return false; }
        out.clear();
        for (const auto& t : *a)
        {
            Length l;
            if (! Length::fromVar(t, l, true)) { return false; }
            out.push_back(l);
        }
        return true;
    };

    if (! str("parent", node.parentId)) { return fail("parent must be a string"); }
    if (! enumeration("kind", node.kind)) { return fail("invalid kind"); }
    if (! str("label", node.label) || ! str("binding", node.bindingId) || ! str("style", node.styleToken))
    {
        return fail("label/binding/style must be strings");
    }
    if (! integer("order", node.order)) { return fail("order must be an integer"); }
    if (! boolean("visible", node.visible) || ! boolean("locked", node.locked) || ! boolean("wrap", node.wrap))
    {
        return fail("visible/locked/wrap must be booleans");
    }
    if (! enumeration("layout", node.layout)) { return fail("invalid layout"); }
    if (! enumeration("direction", node.direction)) { return fail("invalid direction"); }
    float gap = 0.0f;
    if (! value.getProperty("gap", {}).isVoid())
    {
        if (! flt("gap", gap)) { return fail("gap must be a number"); }
        node.gapX = node.gapY = gap;
    }
    if (! flt("gapX", node.gapX) || ! flt("gapY", node.gapY)) { return fail("gapX/gapY must be numbers"); }
    if (const auto p = value.getProperty("padding", {}); ! p.isVoid() && ! readEdges(p, node.padding))
    {
        return fail("padding must be a number or [top,right,bottom,left]");
    }
    if (const auto m = value.getProperty("margin", {}); ! m.isVoid() && ! readEdges(m, node.margin))
    {
        return fail("margin must be a number or [top,right,bottom,left]");
    }
    if (! enumeration("alignItems", node.alignItems) || ! enumeration("alignSelf", node.alignSelf))
    {
        return fail("invalid alignItems/alignSelf");
    }
    if (! enumeration("justify", node.justify)) { return fail("invalid justify"); }
    if (! tracks("columns", node.gridColumns) || ! tracks("rows", node.gridRows))
    {
        return fail("columns/rows must be arrays of lengths");
    }
    if (! enumeration("position", node.position)) { return fail("invalid position"); }
    if (! length("x", node.x) || ! length("y", node.y) || ! length("width", node.width)
        || ! length("height", node.height) || ! length("basis", node.basis)
        || ! length("minWidth", node.minWidth) || ! length("minHeight", node.minHeight)
        || ! length("maxWidth", node.maxWidth) || ! length("maxHeight", node.maxHeight))
    {
        return fail("invalid length (use a number, \"N%\" or \"auto\")");
    }
    if (! flt("grow", node.grow) || ! flt("shrink", node.shrink) || ! flt("aspect", node.aspect))
    {
        return fail("grow/shrink/aspect must be numbers");
    }
    if (! integer("column", node.gridColumn) || ! integer("row", node.gridRow)
        || ! integer("columnSpan", node.columnSpan) || ! integer("rowSpan", node.rowSpan))
    {
        return fail("column/row/spans must be integers");
    }
    if (const auto note = value.getProperty("note", {}); ! note.isVoid() && ! note.isString())
    {
        return fail("note must be a string");
    }
    return true;
}

juce::var writeNode(const InstrumentSceneNode& n)
{
    const InstrumentSceneNode d;
    auto* o = new juce::DynamicObject();
    o->setProperty("id", n.id);
    if (n.parentId.isNotEmpty()) { o->setProperty("parent", n.parentId); }
    if (n.kind != d.kind) { o->setProperty("kind", toString(n.kind)); }
    if (n.label.isNotEmpty()) { o->setProperty("label", n.label); }
    if (n.bindingId.isNotEmpty()) { o->setProperty("binding", n.bindingId); }
    if (n.styleToken.isNotEmpty()) { o->setProperty("style", n.styleToken); }
    if (n.order != d.order) { o->setProperty("order", n.order); }
    if (n.visible != d.visible) { o->setProperty("visible", n.visible); }
    if (n.locked != d.locked) { o->setProperty("locked", n.locked); }
    if (n.layout != d.layout) { o->setProperty("layout", toString(n.layout)); }
    if (n.direction != d.direction) { o->setProperty("direction", toString(n.direction)); }
    if (n.wrap != d.wrap) { o->setProperty("wrap", n.wrap); }
    if (n.gapX == n.gapY) { if (n.gapX != 0.0f) { o->setProperty("gap", num(n.gapX)); } }
    else { o->setProperty("gapX", num(n.gapX)); o->setProperty("gapY", num(n.gapY)); }
    if (! n.padding.isZero()) { o->setProperty("padding", edgesVar(n.padding)); }
    if (n.alignItems != d.alignItems) { o->setProperty("alignItems", toString(n.alignItems)); }
    if (n.justify != d.justify) { o->setProperty("justify", toString(n.justify)); }
    const auto trackVar = [](const std::vector<Length>& t)
    {
        juce::Array<juce::var> a;
        for (const auto& l : t) { a.add(l.toVar()); }
        return juce::var(a);
    };
    if (! n.gridColumns.empty()) { o->setProperty("columns", trackVar(n.gridColumns)); }
    if (! n.gridRows.empty()) { o->setProperty("rows", trackVar(n.gridRows)); }
    if (n.position != d.position) { o->setProperty("position", toString(n.position)); }
    const auto len = [o](const char* key, const Length& l)
    {
        if (! l.isAuto()) { o->setProperty(key, l.toVar()); }
    };
    len("x", n.x);
    len("y", n.y);
    len("width", n.width);
    len("height", n.height);
    len("basis", n.basis);
    if (n.grow != d.grow) { o->setProperty("grow", num(n.grow)); }
    if (n.shrink != d.shrink) { o->setProperty("shrink", num(n.shrink)); }
    len("minWidth", n.minWidth);
    len("minHeight", n.minHeight);
    len("maxWidth", n.maxWidth);
    len("maxHeight", n.maxHeight);
    if (! n.margin.isZero()) { o->setProperty("margin", edgesVar(n.margin)); }
    if (n.alignSelf != d.alignSelf) { o->setProperty("alignSelf", toString(n.alignSelf)); }
    if (n.gridColumn != d.gridColumn) { o->setProperty("column", n.gridColumn); }
    if (n.gridRow != d.gridRow) { o->setProperty("row", n.gridRow); }
    if (n.columnSpan != d.columnSpan) { o->setProperty("columnSpan", n.columnSpan); }
    if (n.rowSpan != d.rowSpan) { o->setProperty("rowSpan", n.rowSpan); }
    if (n.aspect != d.aspect) { o->setProperty("aspect", num(n.aspect)); }
    return juce::var(o);
}

juce::var writeStyle(const InstrumentSceneStyleToken& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("id", t.id);
    o->setProperty("background", t.background);
    o->setProperty("foreground", t.foreground);
    o->setProperty("accent", t.accent);
    o->setProperty("texture", t.texture);
    o->setProperty("knobDiameter", num(t.knobDiameter));
    o->setProperty("labelSize", num(t.labelSize));
    o->setProperty("borderRadius", num(t.borderRadius));
    o->setProperty("borderWidth", num(t.borderWidth));
    return juce::var(o);
}

bool readStyle(const juce::var& v, InstrumentSceneStyleToken& t)
{
    const auto s = [&](const char* key, juce::String& out)
    {
        const auto x = v.getProperty(key, {});
        if (! x.isString()) { return false; }
        out = x.toString();
        return true;
    };
    return v.getDynamicObject() != nullptr && s("id", t.id) && s("background", t.background)
        && s("foreground", t.foreground) && s("accent", t.accent) && s("texture", t.texture)
        && readFinite(v.getProperty("knobDiameter", {}), t.knobDiameter)
        && readFinite(v.getProperty("labelSize", {}), t.labelSize)
        && readFinite(v.getProperty("borderRadius", {}), t.borderRadius)
        && readFinite(v.getProperty("borderWidth", {}), t.borderWidth);
}

float clampSize(float v, float lo, float hi) noexcept
{
    // max wins over min only when they conflict the CSS way round: min wins.
    return juce::jmax(lo, juce::jmin(v, hi));
}
} // namespace

// ---- enum names ------------------------------------------------------------

juce::String toString(InstrumentSceneNodeKind k)
{
    switch (k)
    {
        case InstrumentSceneNodeKind::container: return "container";
        case InstrumentSceneNodeKind::panel: return "panel";
        case InstrumentSceneNodeKind::card: return "card";
        case InstrumentSceneNodeKind::control: return "control";
        case InstrumentSceneNodeKind::button: return "button";
        case InstrumentSceneNodeKind::label: return "label";
        case InstrumentSceneNodeKind::display: return "display";
        case InstrumentSceneNodeKind::decoration: return "decoration";
    }
    return {};
}
juce::String toString(InstrumentSceneLayoutMode m)
{
    switch (m)
    {
        case InstrumentSceneLayoutMode::none: return "none";
        case InstrumentSceneLayoutMode::flex: return "flex";
        case InstrumentSceneLayoutMode::grid: return "grid";
    }
    return {};
}
juce::String toString(LayoutDirection d) { return d == LayoutDirection::row ? "row" : "column"; }
juce::String toString(LayoutAlign a)
{
    switch (a)
    {
        case LayoutAlign::automatic: return "auto";
        case LayoutAlign::start: return "start";
        case LayoutAlign::center: return "center";
        case LayoutAlign::end: return "end";
        case LayoutAlign::stretch: return "stretch";
    }
    return {};
}
juce::String toString(LayoutJustify j)
{
    switch (j)
    {
        case LayoutJustify::start: return "start";
        case LayoutJustify::center: return "center";
        case LayoutJustify::end: return "end";
        case LayoutJustify::spaceBetween: return "space-between";
        case LayoutJustify::spaceAround: return "space-around";
        case LayoutJustify::spaceEvenly: return "space-evenly";
    }
    return {};
}
juce::String toString(LayoutPosition p) { return p == LayoutPosition::relative ? "relative" : "absolute"; }

template <typename E, std::size_t N>
static bool parseAmong(const juce::String& text, E& out, const E (&values)[N])
{
    for (auto v : values)
    {
        if (toString(v) == text) { out = v; return true; }
    }
    return false;
}

bool parse(const juce::String& t, InstrumentSceneNodeKind& o)
{
    using K = InstrumentSceneNodeKind;
    const K all[] { K::container, K::panel, K::card, K::control, K::button, K::label, K::display, K::decoration };
    return parseAmong(t, o, all);
}
bool parse(const juce::String& t, InstrumentSceneLayoutMode& o)
{
    using M = InstrumentSceneLayoutMode;
    const M all[] { M::none, M::flex, M::grid };
    return parseAmong(t, o, all);
}
bool parse(const juce::String& t, LayoutDirection& o)
{
    const LayoutDirection all[] { LayoutDirection::row, LayoutDirection::column };
    return parseAmong(t, o, all);
}
bool parse(const juce::String& t, LayoutAlign& o)
{
    using A = LayoutAlign;
    const A all[] { A::automatic, A::start, A::center, A::end, A::stretch };
    return parseAmong(t, o, all);
}
bool parse(const juce::String& t, LayoutJustify& o)
{
    using J = LayoutJustify;
    const J all[] { J::start, J::center, J::end, J::spaceBetween, J::spaceAround, J::spaceEvenly };
    return parseAmong(t, o, all);
}
bool parse(const juce::String& t, LayoutPosition& o)
{
    const LayoutPosition all[] { LayoutPosition::relative, LayoutPosition::absolute };
    return parseAmong(t, o, all);
}

// ---- Length ------------------------------------------------------------------

juce::var Length::toVar() const
{
    switch (unit)
    {
        case Unit::automatic: return "auto";
        case Unit::px: return num(value);
        case Unit::percent: return toString();
        case Unit::fr: return toString();
    }
    return {};
}

juce::String Length::toString() const
{
    const auto n = [this]
    {
        auto s = juce::String(value, 4);
        if (s.containsChar('.')) { s = s.trimCharactersAtEnd("0").trimCharactersAtEnd("."); }
        return s;
    };
    switch (unit)
    {
        case Unit::automatic: return "auto";
        case Unit::px: return n();
        case Unit::percent: return n() + "%";
        case Unit::fr: return n() + "fr";
    }
    return {};
}

bool Length::fromString(const juce::String& textIn, Length& result, bool allowFr)
{
    const auto text = textIn.trim();
    if (text == "auto" || text.isEmpty()) { result = autoSize(); return true; }
    auto unitOut = Unit::px;
    auto numberText = text;
    if (text.endsWithChar('%')) { unitOut = Unit::percent; numberText = text.dropLastCharacters(1); }
    else if (text.endsWithIgnoreCase("fr")) { unitOut = Unit::fr; numberText = text.dropLastCharacters(2); }
    else if (text.endsWithIgnoreCase("px")) { numberText = text.dropLastCharacters(2); }
    numberText = numberText.trim();
    if (numberText.isEmpty() || ! numberText.containsOnly("0123456789.-+eE")) { return false; }
    const auto v = static_cast<float>(numberText.getDoubleValue());
    if (! std::isfinite(v) || (unitOut == Unit::fr && (! allowFr || v < 0.0f))) { return false; }
    result = { unitOut, v };
    return true;
}

bool Length::fromVar(const juce::var& v, Length& result, bool allowFr)
{
    float n = 0.0f;
    if (readFinite(v, n)) { result = px(n); return true; }
    return v.isString() && fromString(v.toString(), result, allowFr);
}

// ---- load / save --------------------------------------------------------------

bool InstrumentSceneDocument::loadJson(const juce::String& text, juce::String& error)
{
    error.clear();
    juce::var parsed;
    if (const auto r = juce::JSON::parse(text, parsed); r.failed())
    {
        error = "Invalid scene JSON: " + r.getErrorMessage();
        return false;
    }
    if (parsed.getDynamicObject() == nullptr) { error = "Scene root must be an object."; return false; }

    const auto version = parsed.getProperty("schemaVersion", {});
    if (! version.isInt() || static_cast<int>(version) != currentSchemaVersion)
    {
        error = "Unsupported layout schema version (expected " + juce::String(currentSchemaVersion) + ").";
        return false;
    }

    auto* styleArray = parsed.getProperty("styles", {}).getArray();
    auto* nodeArray = parsed.getProperty("nodes", {}).getArray();
    if (styleArray == nullptr || nodeArray == nullptr)
    {
        error = "Scene needs 'styles' and 'nodes' arrays.";
        return false;
    }

    std::vector<InstrumentSceneStyleToken> newStyles;
    for (const auto& v : *styleArray)
    {
        InstrumentSceneStyleToken t;
        if (! readStyle(v, t) || ! validStyle(t))
        {
            error = "Scene style token is missing a valid property.";
            return false;
        }
        newStyles.push_back(std::move(t));
    }

    std::vector<InstrumentSceneNode> newNodes;
    newNodes.reserve(static_cast<std::size_t>(nodeArray->size()));
    for (const auto& v : *nodeArray)
    {
        InstrumentSceneNode n;
        if (! readNode(v, n, error)) { return false; }
        newNodes.push_back(std::move(n));
    }

    auto oldNodes = std::move(nodes);
    auto oldStyles = std::move(styleTokens);
    nodes = std::move(newNodes);
    styleTokens = std::move(newStyles);
    rebuildTopology();
    if (! validate(error))
    {
        nodes = std::move(oldNodes);
        styleTokens = std::move(oldStyles);
        rebuildTopology();
        return false;
    }
    touched();
    return true;
}

juce::String InstrumentSceneDocument::toJson() const
{
    // Hand-assembled so each node is one line: a 300-node scene stays diffable.
    juce::String out;
    out << "{\n  \"schemaVersion\": " << currentSchemaVersion << ",\n  \"styles\": [\n";
    for (std::size_t i = 0; i < styleTokens.size(); ++i)
    {
        out << "    " << juce::JSON::toString(writeStyle(styleTokens[i]), true)
            << (i + 1 < styleTokens.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"nodes\": [\n";
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        out << "    " << juce::JSON::toString(writeNode(nodes[i]), true)
            << (i + 1 < nodes.size() ? ",\n" : "\n");
    }
    out << "  ]\n}\n";
    return out;
}

// ---- topology -------------------------------------------------------------------

void InstrumentSceneDocument::rebuildTopology()
{
    indexById.clear();
    indexById.reserve(nodes.size());
    root = -1;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        indexById.emplace(nodes[static_cast<std::size_t>(i)].id, i);
    }

    children.assign(nodes.size(), {});
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        const auto& n = nodes[static_cast<std::size_t>(i)];
        if (n.parentId.isEmpty())
        {
            if (root < 0) { root = i; }
            continue;
        }
        const auto p = indexOf(n.parentId);
        if (p >= 0 && p != i) { children[static_cast<std::size_t>(p)].push_back(i); }
    }
    for (auto& list : children)
    {
        std::stable_sort(list.begin(), list.end(), [this](int a, int b)
        {
            return nodes[static_cast<std::size_t>(a)].order < nodes[static_cast<std::size_t>(b)].order;
        });
    }

    depths.assign(nodes.size(), -1);
    postOrder.clear();
    if (root >= 0)
    {
        std::vector<int> stack { root };
        depths[static_cast<std::size_t>(root)] = 0;
        while (! stack.empty())
        {
            const auto i = stack.back();
            stack.pop_back();
            for (auto c : children[static_cast<std::size_t>(i)])
            {
                if (depths[static_cast<std::size_t>(c)] >= 0) { continue; }
                depths[static_cast<std::size_t>(c)] = depths[static_cast<std::size_t>(i)] + 1;
                stack.push_back(c);
            }
        }
    }
    // Deepest first, so a bottom-up pass sees every child before its parent.
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        if (depths[static_cast<std::size_t>(i)] >= 0) { postOrder.push_back(i); }
    }
    std::stable_sort(postOrder.begin(), postOrder.end(), [this](int a, int b)
    {
        return depths[static_cast<std::size_t>(a)] > depths[static_cast<std::size_t>(b)];
    });
}

void InstrumentSceneDocument::touched()
{
    rebuildTopology();
    ++revision;
}

bool InstrumentSceneDocument::validate(juce::String& error) const
{
    if (nodes.empty()) { error = "Scene has no nodes."; return false; }
    if (indexById.size() != nodes.size()) { error = "Scene node ids must be unique."; return false; }

    for (std::size_t i = 0; i < styleTokens.size(); ++i)
    {
        for (std::size_t j = i + 1; j < styleTokens.size(); ++j)
        {
            if (styleTokens[i].id == styleTokens[j].id) { error = "Duplicate style token " + styleTokens[i].id; return false; }
        }
    }

    auto roots = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const auto& n = nodes[i];
        const auto bad = [&](const juce::String& what)
        {
            error = "Node '" + n.id + "': " + what;
            return false;
        };
        if (n.id.trim().isEmpty() || n.id != n.id.trim()) { return bad("id must be non-empty and untrimmed"); }
        if (n.parentId.isEmpty()) { ++roots; }
        else if (indexOf(n.parentId) < 0) { return bad("unknown parent '" + n.parentId + "'"); }
        if (depths[i] < 0 && n.parentId.isNotEmpty()) { return bad("parent chain does not reach the root (cycle?)"); }
        if (n.styleToken.isNotEmpty() && findStyleToken(n.styleToken) == nullptr) { return bad("unknown style token"); }
        if (n.grow < 0.0f || n.shrink < 0.0f) { return bad("grow and shrink must be >= 0"); }
        if (n.gapX < 0.0f || n.gapY < 0.0f) { return bad("gaps must be >= 0"); }
        if (n.padding.top < 0 || n.padding.right < 0 || n.padding.bottom < 0 || n.padding.left < 0)
        {
            return bad("padding must be >= 0");
        }
        if (n.columnSpan < 1 || n.rowSpan < 1 || n.gridColumn < 0 || n.gridRow < 0)
        {
            return bad("grid spans must be >= 1 and positions >= 0");
        }
        if (n.aspect < 0.0f) { return bad("aspect must be >= 0"); }
        for (const auto* l : { &n.width, &n.height, &n.basis, &n.minWidth, &n.minHeight, &n.maxWidth, &n.maxHeight })
        {
            if (l->isDefinite() && l->value < 0.0f) { return bad("sizes must be >= 0"); }
        }
        if (n.minWidth.unit == n.maxWidth.unit && n.minWidth.isDefinite() && n.minWidth.value > n.maxWidth.value)
        {
            return bad("minWidth > maxWidth");
        }
        if (n.minHeight.unit == n.maxHeight.unit && n.minHeight.isDefinite() && n.minHeight.value > n.maxHeight.value)
        {
            return bad("minHeight > maxHeight");
        }
        if (n.layout == InstrumentSceneLayoutMode::grid && n.gridColumns.empty())
        {
            return bad("a grid needs a columns template");
        }
        if (n.layout != InstrumentSceneLayoutMode::grid && (! n.gridColumns.empty() || ! n.gridRows.empty()))
        {
            return bad("columns/rows are only valid on a grid");
        }
    }
    if (roots != 1) { error = "Scene must have exactly one root node."; return false; }
    return true;
}

const InstrumentSceneNode* InstrumentSceneDocument::findNode(const juce::String& id) const noexcept
{
    const auto i = indexOf(id);
    return i >= 0 ? &nodes[static_cast<std::size_t>(i)] : nullptr;
}

int InstrumentSceneDocument::indexOf(const juce::String& id) const noexcept
{
    if (const auto it = indexById.find(id); it != indexById.end()) { return it->second; }
    return -1;
}

bool InstrumentSceneDocument::isNodeVisible(const juce::String& id) const noexcept
{
    auto i = indexOf(id);
    if (i < 0) { return false; }
    for (int guard = 0; i >= 0 && guard < 1000; ++guard)
    {
        const auto& n = nodes[static_cast<std::size_t>(i)];
        if (! n.visible) { return false; }
        if (n.parentId.isEmpty()) { return true; }
        i = indexOf(n.parentId);
    }
    return false;
}

const InstrumentSceneStyleToken* InstrumentSceneDocument::findStyleToken(const juce::String& id) const noexcept
{
    for (const auto& t : styleTokens)
    {
        if (t.id == id) { return &t; }
    }
    return nullptr;
}

bool InstrumentSceneDocument::setRuntimeHidden(const juce::String& id, bool hidden)
{
    const auto changed = hidden ? runtimeHidden.insert(id).second : runtimeHidden.erase(id) > 0;
    if (changed) { ++revision; }
    return changed;
}

bool InstrumentSceneDocument::isShown(int index) const noexcept
{
    return index >= 0 && index < static_cast<int>(shown.size()) && shown[static_cast<std::size_t>(index)] != 0;
}

const std::vector<int>& InstrumentSceneDocument::childrenOf(int index) const noexcept
{
    static const std::vector<int> none;
    return index >= 0 && index < static_cast<int>(children.size()) ? children[static_cast<std::size_t>(index)] : none;
}

int InstrumentSceneDocument::depthOf(int index) const noexcept
{
    return index >= 0 && index < static_cast<int>(depths.size()) ? depths[static_cast<std::size_t>(index)] : -1;
}

// ---- resolve -------------------------------------------------------------------

void InstrumentSceneDocument::resolve(juce::Rectangle<float> rootBounds) const
{
    if (cachedRevision == revision && cachedRoot == rootBounds && rects.size() == nodes.size()) { return; }
    ++resolveCount;
    cachedRevision = revision;
    cachedRoot = rootBounds;
    rects.assign(nodes.size(), {});

    // Effective visibility, children before parents: a node is shown when it
    // is visible, not hidden at runtime, and - for a flex/grid container with
    // children - when at least one child is shown. That last rule is what lets
    // a cell whose only control is hidden by its owner drop out of its row.
    shown.assign(nodes.size(), 0);
    for (auto it = postOrder.begin(); it != postOrder.end(); ++it)
    {
        const auto i = static_cast<std::size_t>(*it);
        const auto& n = nodes[i];
        auto on = n.visible && runtimeHidden.count(n.id) == 0;
        if (on && n.isContainer() && ! children[i].empty())
        {
            on = std::any_of(children[i].begin(), children[i].end(),
                             [this](int c) { return shown[static_cast<std::size_t>(c)] != 0; });
        }
        shown[i] = on ? 1 : 0;
    }
    if (root >= 0 && shown[static_cast<std::size_t>(root)] != 0)
    {
        place(root, rootBounds);
    }
}

juce::Rectangle<float> InstrumentSceneDocument::rectOf(int index) const noexcept
{
    return index >= 0 && index < static_cast<int>(rects.size()) ? rects[static_cast<std::size_t>(index)]
                                                                 : juce::Rectangle<float>();
}

juce::Rectangle<float> InstrumentSceneDocument::resolveBounds(const juce::String& id,
                                                              juce::Rectangle<float> rootBounds) const
{
    resolve(rootBounds);
    return rectOf(id);
}

void InstrumentSceneDocument::place(int index, juce::Rectangle<float> rect) const
{
    const auto& n = nodes[static_cast<std::size_t>(index)];
    rect.setWidth(juce::jmax(0.0f, rect.getWidth()));
    rect.setHeight(juce::jmax(0.0f, rect.getHeight()));
    if (n.aspect > 0.0f && rect.getHeight() > 0.0f)
    {
        const auto w = juce::jmin(rect.getWidth(), rect.getHeight() * n.aspect);
        const auto h = w / n.aspect;
        rect = juce::Rectangle<float>(w, h).withCentre(rect.getCentre());
    }
    rects[static_cast<std::size_t>(index)] = rect;
    layoutChildren(index, rect);
}

void InstrumentSceneDocument::layoutChildren(int index, juce::Rectangle<float> box) const
{
    const auto& n = nodes[static_cast<std::size_t>(index)];
    const auto& kids = children[static_cast<std::size_t>(index)];
    if (kids.empty()) { return; }

    const auto content = juce::Rectangle<float>::leftTopRightBottom(
        box.getX() + n.padding.left, box.getY() + n.padding.top,
        juce::jmax(box.getX() + n.padding.left, box.getRight() - n.padding.right),
        juce::jmax(box.getY() + n.padding.top, box.getBottom() - n.padding.bottom));

    switch (n.layout)
    {
        case InstrumentSceneLayoutMode::flex: layoutFlex(index, content); break;
        case InstrumentSceneLayoutMode::grid: layoutGrid(index, content); break;
        case InstrumentSceneLayoutMode::none:
        {
            // Overlay: each relative child fills the content box, or sits at its
            // definite size aligned by alignSelf (start when unset).
            for (auto c : kids)
            {
                const auto& k = nodes[static_cast<std::size_t>(c)];
                if (! shown[static_cast<std::size_t>(c)] || k.position == LayoutPosition::absolute) { continue; }
                auto area = juce::Rectangle<float>::leftTopRightBottom(
                    content.getX() + k.margin.left, content.getY() + k.margin.top,
                    content.getRight() - k.margin.right, content.getBottom() - k.margin.bottom);
                const auto w = clampSize(k.width.resolve(content.getWidth(), area.getWidth()),
                                         k.minWidth.resolve(content.getWidth(), 0.0f),
                                         k.maxWidth.resolve(content.getWidth(), kInf));
                const auto h = clampSize(k.height.resolve(content.getHeight(), area.getHeight()),
                                         k.minHeight.resolve(content.getHeight(), 0.0f),
                                         k.maxHeight.resolve(content.getHeight(), kInf));
                const auto a = k.alignSelf == LayoutAlign::automatic ? n.alignItems : k.alignSelf;
                const auto offset = [a](float space, float size)
                {
                    if (a == LayoutAlign::center) { return (space - size) * 0.5f; }
                    if (a == LayoutAlign::end) { return space - size; }
                    return 0.0f;
                };
                place(c, { area.getX() + offset(area.getWidth(), w), area.getY() + offset(area.getHeight(), h), w, h });
            }
            break;
        }
    }

    // Absolute children are placed against the parent's whole box.
    for (auto c : kids)
    {
        const auto& k = nodes[static_cast<std::size_t>(c)];
        if (! shown[static_cast<std::size_t>(c)] || k.position != LayoutPosition::absolute) { continue; }
        const auto x = k.x.resolve(box.getWidth(), 0.0f) + k.margin.left;
        const auto y = k.y.resolve(box.getHeight(), 0.0f) + k.margin.top;
        const auto w = clampSize(k.width.resolve(box.getWidth(), box.getWidth() - x - k.margin.right),
                                 k.minWidth.resolve(box.getWidth(), 0.0f),
                                 k.maxWidth.resolve(box.getWidth(), kInf));
        const auto h = clampSize(k.height.resolve(box.getHeight(), box.getHeight() - y - k.margin.bottom),
                                 k.minHeight.resolve(box.getHeight(), 0.0f),
                                 k.maxHeight.resolve(box.getHeight(), kInf));
        place(c, { box.getX() + x, box.getY() + y, w, h });
    }
}

void InstrumentSceneDocument::layoutFlex(int index, juce::Rectangle<float> content) const
{
    const auto& n = nodes[static_cast<std::size_t>(index)];
    const auto isRow = n.direction == LayoutDirection::row;
    const auto mainSize = isRow ? content.getWidth() : content.getHeight();
    const auto crossSize = isRow ? content.getHeight() : content.getWidth();
    const auto mainGap = isRow ? n.gapX : n.gapY;
    const auto crossGap = isRow ? n.gapY : n.gapX;

    auto& items = flexScratch;
    const auto scratchStart = items.size(); // re-entrant: nested containers append
    for (auto c : children[static_cast<std::size_t>(index)])
    {
        const auto& k = nodes[static_cast<std::size_t>(c)];
        if (! shown[static_cast<std::size_t>(c)] || k.position == LayoutPosition::absolute) { continue; }
        FlexItemScratch it {};
        it.index = c;
        const auto& mainLen = k.basis.isAuto() ? (isRow ? k.width : k.height) : k.basis;
        it.base = mainLen.resolve(mainSize, 0.0f);
        it.minMain = (isRow ? k.minWidth : k.minHeight).resolve(mainSize, 0.0f);
        it.maxMain = juce::jmax(it.minMain, (isRow ? k.maxWidth : k.maxHeight).resolve(mainSize, kInf));
        it.hypo = clampSize(it.base, it.minMain, it.maxMain);
        const auto& crossLen = isRow ? k.height : k.width;
        it.crossDefinite = crossLen.isDefinite();
        it.minCross = (isRow ? k.minHeight : k.minWidth).resolve(crossSize, 0.0f);
        it.maxCross = juce::jmax(it.minCross, (isRow ? k.maxHeight : k.maxWidth).resolve(crossSize, kInf));
        it.crossSize = clampSize(crossLen.resolve(crossSize, 0.0f), it.minCross, it.maxCross);
        if (k.aspect > 0.0f && mainLen.isAuto())
        {
            // aspect-ratio: an auto main size follows the cross size, which is
            // the stretched line when the item has none of its own.
            const auto align = k.alignSelf == LayoutAlign::automatic ? n.alignItems : k.alignSelf;
            const auto crossMargins = isRow ? k.margin.top + k.margin.bottom : k.margin.left + k.margin.right;
            const auto cross = it.crossDefinite ? it.crossSize
                             : align == LayoutAlign::stretch ? clampSize(crossSize - crossMargins, it.minCross, it.maxCross)
                                                             : 0.0f;
            it.base = isRow ? cross * k.aspect : cross / k.aspect;
            it.hypo = clampSize(it.base, it.minMain, it.maxMain);
        }
        it.marginMainStart = isRow ? k.margin.left : k.margin.top;
        it.marginMainEnd = isRow ? k.margin.right : k.margin.bottom;
        it.marginCrossStart = isRow ? k.margin.top : k.margin.left;
        it.marginCrossEnd = isRow ? k.margin.bottom : k.margin.right;
        it.target = it.hypo;
        items.push_back(it);
    }
    const auto count = items.size() - scratchStart;
    if (count == 0) { items.resize(scratchStart); return; }

    // Break into lines.
    auto lineCount = 1;
    {
        auto used = 0.0f;
        auto inLine = 0;
        for (auto i = scratchStart; i < items.size(); ++i)
        {
            auto& it = items[i];
            const auto outer = it.hypo + it.marginMainStart + it.marginMainEnd;
            const auto needed = used + (inLine > 0 ? mainGap : 0.0f) + outer;
            if (n.wrap && inLine > 0 && needed > mainSize + 0.01f)
            {
                ++lineCount;
                used = outer;
                inLine = 1;
            }
            else
            {
                used = needed;
                ++inLine;
            }
            it.line = lineCount - 1;
        }
    }

    // Resolve flexible lengths per line (CSS 9.7, the freeze loop).
    for (int line = 0; line < lineCount; ++line)
    {
        auto first = scratchStart;
        while (items[first].line != line) { ++first; }
        auto last = first;
        while (last < items.size() && items[last].line == line) { ++last; }
        const auto gaps = mainGap * static_cast<float>(last - first - 1);

        auto hypoSum = 0.0f;
        auto margins = 0.0f;
        for (auto i = first; i < last; ++i)
        {
            hypoSum += items[i].hypo;
            margins += items[i].marginMainStart + items[i].marginMainEnd;
        }
        const auto growing = hypoSum + margins + gaps < mainSize;
        for (auto i = first; i < last; ++i)
        {
            auto& it = items[i];
            const auto& k = nodes[static_cast<std::size_t>(it.index)];
            const auto factor = growing ? k.grow : k.shrink;
            it.frozen = factor <= 0.0f || (growing && it.base > it.hypo) || (! growing && it.base < it.hypo);
            it.target = it.hypo;
        }

        for (std::size_t guard = 0; guard <= last - first; ++guard)
        {
            auto frozenSum = 0.0f;
            auto unfrozenBase = 0.0f;
            auto factorSum = 0.0f;
            auto anyUnfrozen = false;
            for (auto i = first; i < last; ++i)
            {
                const auto& it = items[i];
                const auto& k = nodes[static_cast<std::size_t>(it.index)];
                if (it.frozen) { frozenSum += it.target; continue; }
                anyUnfrozen = true;
                unfrozenBase += it.base;
                factorSum += growing ? k.grow : k.shrink * it.base;
            }
            if (! anyUnfrozen) { break; }

            auto free = mainSize - gaps - margins - frozenSum - unfrozenBase;
            if (growing)
            {
                // A total grow below 1 takes only that fraction of the space.
                auto growSum = 0.0f;
                for (auto i = first; i < last; ++i)
                {
                    if (! items[i].frozen) { growSum += nodes[static_cast<std::size_t>(items[i].index)].grow; }
                }
                if (growSum < 1.0f) { free *= growSum; }
            }

            auto violation = 0.0f;
            for (auto i = first; i < last; ++i)
            {
                auto& it = items[i];
                if (it.frozen) { continue; }
                const auto& k = nodes[static_cast<std::size_t>(it.index)];
                const auto share = factorSum > 0.0f ? (growing ? k.grow : k.shrink * it.base) / factorSum : 0.0f;
                const auto unclamped = it.base + free * share;
                it.target = clampSize(unclamped, it.minMain, it.maxMain);
                violation += it.target - unclamped;
            }

            const auto eps = 0.001f;
            for (auto i = first; i < last; ++i)
            {
                auto& it = items[i];
                if (it.frozen) { continue; }
                const auto& k = nodes[static_cast<std::size_t>(it.index)];
                const auto share = factorSum > 0.0f ? (growing ? k.grow : k.shrink * it.base) / factorSum : 0.0f;
                const auto unclamped = it.base + free * share;
                const auto v = it.target - unclamped;
                if (std::abs(violation) < eps || (violation > 0.0f && v > eps) || (violation < 0.0f && v < -eps))
                {
                    it.frozen = true;
                }
            }
            if (std::abs(violation) < eps) { break; }
        }
    }

    // Cross sizes of the lines.
    float lineCross[64] {};
    const auto lines = juce::jmin(lineCount, 64);
    if (! n.wrap)
    {
        lineCross[0] = crossSize;
    }
    else
    {
        for (auto i = scratchStart; i < items.size(); ++i)
        {
            const auto& it = items[i];
            const auto l = juce::jmin(it.line, 63);
            const auto outer = (it.crossDefinite ? it.crossSize : it.minCross) + it.marginCrossStart + it.marginCrossEnd;
            lineCross[l] = juce::jmax(lineCross[l], outer);
        }
        auto used = crossGap * static_cast<float>(lines - 1);
        for (int l = 0; l < lines; ++l) { used += lineCross[l]; }
        const auto extra = crossSize - used;
        if (extra > 0.0f)
        {
            for (int l = 0; l < lines; ++l) { lineCross[l] += extra / static_cast<float>(lines); }
        }
    }

    // Position.
    auto crossPos = 0.0f;
    for (int line = 0; line < lines; ++line)
    {
        auto first = scratchStart;
        while (first < items.size() && items[first].line != line) { ++first; }
        auto last = first;
        while (last < items.size() && items[last].line == line) { ++last; }
        const auto itemCount = static_cast<float>(last - first);

        auto used = mainGap * (itemCount - 1.0f);
        for (auto i = first; i < last; ++i) { used += items[i].target + items[i].marginMainStart + items[i].marginMainEnd; }
        const auto leftover = mainSize - used;
        auto pos = 0.0f;
        auto between = mainGap;
        if (leftover > 0.0f)
        {
            switch (n.justify)
            {
                case LayoutJustify::start: break;
                case LayoutJustify::center: pos = leftover * 0.5f; break;
                case LayoutJustify::end: pos = leftover; break;
                case LayoutJustify::spaceBetween:
                    if (itemCount > 1.0f) { between += leftover / (itemCount - 1.0f); }
                    break;
                case LayoutJustify::spaceAround:
                    pos = leftover / itemCount * 0.5f;
                    between += leftover / itemCount;
                    break;
                case LayoutJustify::spaceEvenly:
                    pos = leftover / (itemCount + 1.0f);
                    between += leftover / (itemCount + 1.0f);
                    break;
            }
        }

        const auto thisLineCross = lineCross[line];
        for (auto i = first; i < last; ++i)
        {
            const auto& it = items[i];
            const auto& k = nodes[static_cast<std::size_t>(it.index)];
            pos += it.marginMainStart;
            const auto align = k.alignSelf == LayoutAlign::automatic ? n.alignItems : k.alignSelf;
            const auto room = thisLineCross - it.marginCrossStart - it.marginCrossEnd;
            auto cross = it.crossDefinite ? it.crossSize : it.minCross;
            if (align == LayoutAlign::stretch && ! it.crossDefinite) { cross = clampSize(room, it.minCross, it.maxCross); }
            auto crossOffset = it.marginCrossStart;
            if (align == LayoutAlign::center) { crossOffset += (room - cross) * 0.5f; }
            else if (align == LayoutAlign::end) { crossOffset += room - cross; }

            const auto mainStart = (isRow ? content.getX() : content.getY()) + pos;
            const auto crossStart = (isRow ? content.getY() : content.getX()) + crossPos + crossOffset;
            const auto rect = isRow ? juce::Rectangle<float>(mainStart, crossStart, it.target, cross)
                                    : juce::Rectangle<float>(crossStart, mainStart, cross, it.target);
            pos += it.target + it.marginMainEnd + between;
            place(it.index, rect);
        }
        crossPos += thisLineCross + crossGap;
    }

    items.resize(scratchStart);
}

namespace
{
// Sizes tracks in place: px/% fixed, auto = largest definite single-span item
// (or 1fr when none), fr share what is left.
void sizeTracks(const std::vector<Length>& templ, float available, float gap,
                const std::vector<float>& autoContent, std::vector<float>& out)
{
    const auto n = templ.size();
    out.assign(n, 0.0f);
    const auto space = available - gap * static_cast<float>(n > 0 ? n - 1 : 0);
    auto fixed = 0.0f;
    auto frTotal = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& t = templ[i];
        if (t.isDefinite()) { out[i] = t.resolve(available, 0.0f); fixed += out[i]; }
        else if (t.unit == Length::Unit::fr) { frTotal += t.value; }
        else if (autoContent[i] > 0.0f) { out[i] = autoContent[i]; fixed += out[i]; }
        else { frTotal += 1.0f; }
    }
    const auto remaining = juce::jmax(0.0f, space - fixed);
    if (frTotal <= 0.0f) { return; }
    for (std::size_t i = 0; i < n; ++i)
    {
        const auto& t = templ[i];
        if (t.unit == Length::Unit::fr) { out[i] = remaining * t.value / frTotal; }
        else if (t.isAuto() && autoContent[i] <= 0.0f) { out[i] = remaining / frTotal; }
    }
}
}

void InstrumentSceneDocument::layoutGrid(int index, juce::Rectangle<float> content) const
{
    const auto& n = nodes[static_cast<std::size_t>(index)];
    const auto columns = static_cast<int>(n.gridColumns.size());

    struct Placed { int index, col, row, colSpan, rowSpan; };
    std::vector<Placed> placed;
    std::vector<char> occupied; // rows * columns
    auto rowsUsed = 0;
    const auto isFree = [&](int col, int row, int cs, int rs)
    {
        if (col + cs > columns) { return false; }
        for (int r = row; r < row + rs; ++r)
        {
            for (int c = col; c < col + cs; ++c)
            {
                const auto i = static_cast<std::size_t>(r * columns + c);
                if (i < occupied.size() && occupied[i] != 0) { return false; }
            }
        }
        return true;
    };
    const auto occupy = [&](int col, int row, int cs, int rs)
    {
        const auto needed = static_cast<std::size_t>((row + rs) * columns);
        if (occupied.size() < needed) { occupied.resize(needed, 0); }
        for (int r = row; r < row + rs; ++r)
        {
            for (int c = col; c < col + cs; ++c) { occupied[static_cast<std::size_t>(r * columns + c)] = 1; }
        }
        rowsUsed = juce::jmax(rowsUsed, row + rs);
    };

    // Explicitly placed first, then auto-placed in order (row-major, sparse).
    for (int pass = 0; pass < 2; ++pass)
    {
        auto cursorCol = 0;
        auto cursorRow = 0;
        for (auto c : children[static_cast<std::size_t>(index)])
        {
            const auto& k = nodes[static_cast<std::size_t>(c)];
            if (! shown[static_cast<std::size_t>(c)] || k.position == LayoutPosition::absolute) { continue; }
            const auto explicitBoth = k.gridColumn > 0 && k.gridRow > 0;
            if ((pass == 0) != explicitBoth) { continue; }
            const auto cs = juce::jlimit(1, columns, k.columnSpan);
            const auto rs = juce::jmax(1, k.rowSpan);
            if (explicitBoth)
            {
                const auto col = juce::jlimit(0, columns - cs, k.gridColumn - 1);
                occupy(col, k.gridRow - 1, cs, rs);
                placed.push_back({ c, col, k.gridRow - 1, cs, rs });
                continue;
            }
            auto col = k.gridColumn > 0 ? juce::jlimit(0, columns - cs, k.gridColumn - 1) : cursorCol;
            auto row = k.gridRow > 0 ? k.gridRow - 1 : cursorRow;
            for (int guard = 0; guard < 10000; ++guard)
            {
                if (isFree(col, row, cs, rs)) { break; }
                if (k.gridColumn > 0) { ++row; continue; }
                if (++col + cs > columns) { col = 0; ++row; }
            }
            occupy(col, row, cs, rs);
            placed.push_back({ c, col, row, cs, rs });
            if (k.gridColumn == 0 && k.gridRow == 0)
            {
                cursorCol = col + cs;
                cursorRow = row;
                if (cursorCol >= columns) { cursorCol = 0; ++cursorRow; }
            }
        }
    }

    std::vector<Length> rowTemplate = n.gridRows;
    while (static_cast<int>(rowTemplate.size()) < rowsUsed) { rowTemplate.push_back(Length::fr(1.0f)); }

    std::vector<float> autoCols(n.gridColumns.size(), 0.0f);
    std::vector<float> autoRows(rowTemplate.size(), 0.0f);
    for (const auto& p : placed)
    {
        const auto& k = nodes[static_cast<std::size_t>(p.index)];
        if (p.colSpan == 1 && k.width.isDefinite())
        {
            auto& a = autoCols[static_cast<std::size_t>(p.col)];
            a = juce::jmax(a, k.width.resolve(content.getWidth(), 0.0f) + k.margin.left + k.margin.right);
        }
        if (p.rowSpan == 1 && k.height.isDefinite())
        {
            auto& a = autoRows[static_cast<std::size_t>(p.row)];
            a = juce::jmax(a, k.height.resolve(content.getHeight(), 0.0f) + k.margin.top + k.margin.bottom);
        }
    }
    std::vector<float> colSizes, rowSizes;
    sizeTracks(n.gridColumns, content.getWidth(), n.gapX, autoCols, colSizes);
    sizeTracks(rowTemplate, content.getHeight(), n.gapY, autoRows, rowSizes);

    const auto trackStart = [](const std::vector<float>& sizes, float gap, int i)
    {
        auto p = 0.0f;
        for (int t = 0; t < i; ++t) { p += sizes[static_cast<std::size_t>(t)] + gap; }
        return p;
    };

    for (const auto& p : placed)
    {
        const auto& k = nodes[static_cast<std::size_t>(p.index)];
        const auto x0 = trackStart(colSizes, n.gapX, p.col);
        const auto x1 = trackStart(colSizes, n.gapX, p.col + p.colSpan) - n.gapX;
        const auto y0 = trackStart(rowSizes, n.gapY, p.row);
        const auto y1 = trackStart(rowSizes, n.gapY, p.row + p.rowSpan) - n.gapY;
        const auto area = juce::Rectangle<float>::leftTopRightBottom(
            content.getX() + x0 + k.margin.left, content.getY() + y0 + k.margin.top,
            juce::jmax(content.getX() + x0 + k.margin.left, content.getX() + x1 - k.margin.right),
            juce::jmax(content.getY() + y0 + k.margin.top, content.getY() + y1 - k.margin.bottom));
        const auto align = k.alignSelf == LayoutAlign::automatic ? n.alignItems : k.alignSelf;
        const auto size = [align](const Length& l, float room, float ref, float lo, float hi)
        {
            if (l.isDefinite()) { return clampSize(l.resolve(ref, 0.0f), lo, hi); }
            return clampSize(align == LayoutAlign::stretch ? room : 0.0f, lo, hi);
        };
        const auto w = size(k.width, area.getWidth(), content.getWidth(),
                            k.minWidth.resolve(content.getWidth(), 0.0f), k.maxWidth.resolve(content.getWidth(), kInf));
        const auto h = size(k.height, area.getHeight(), content.getHeight(),
                            k.minHeight.resolve(content.getHeight(), 0.0f), k.maxHeight.resolve(content.getHeight(), kInf));
        const auto offset = [align](float room, float s)
        {
            if (align == LayoutAlign::center) { return (room - s) * 0.5f; }
            if (align == LayoutAlign::end) { return room - s; }
            return 0.0f;
        };
        place(p.index, { area.getX() + offset(area.getWidth(), w), area.getY() + offset(area.getHeight(), h), w, h });
    }
}

// ---- editing (designer builds and tests only) ---------------------------------

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)
void InstrumentSceneDocument::restore(const Snapshot& s)
{
    nodes = s.nodes;
    styleTokens = s.styles;
    touched();
}

void InstrumentSceneDocument::recordStep(Snapshot before)
{
    if (transactionOpen)
    {
        transactionChanged = true;
        return;
    }
    undoStack.push_back(std::move(before));
    if (undoStack.size() > 200) { undoStack.erase(undoStack.begin()); }
    redoStack.clear();
}

bool InstrumentSceneDocument::addStyleToken(const InstrumentSceneStyleToken& token, juce::String& error)
{
    if (! validStyle(token)) { error = "Scene style token has invalid colours or dimensions."; return false; }
    if (findStyleToken(token.id) != nullptr) { error = "Style token already exists."; return false; }
    auto before = snapshot();
    styleTokens.push_back(token);
    touched();
    recordStep(std::move(before));
    return true;
}

bool InstrumentSceneDocument::updateStyleToken(const InstrumentSceneStyleToken& token, juce::String& error)
{
    if (! validStyle(token)) { error = "Scene style token has invalid colours or dimensions."; return false; }
    for (auto& t : styleTokens)
    {
        if (t.id == token.id)
        {
            auto before = snapshot();
            t = token;
            touched();
            recordStep(std::move(before));
            return true;
        }
    }
    error = "Unknown style token.";
    return false;
}

bool InstrumentSceneDocument::addNode(const InstrumentSceneNode& node, juce::String& error)
{
    if (indexOf(node.id) >= 0) { error = "Node id already exists: " + node.id; return false; }
    auto before = snapshot();
    nodes.push_back(node);
    rebuildTopology();
    if (! validate(error))
    {
        restore(before);
        return false;
    }
    touched();
    recordStep(std::move(before));
    return true;
}

bool InstrumentSceneDocument::removeNode(const juce::String& id, juce::String& error)
{
    const auto i = indexOf(id);
    if (i < 0) { error = "Unknown node."; return false; }
    if (! children[static_cast<std::size_t>(i)].empty()) { error = "Remove the children first."; return false; }
    if (i == root) { error = "The root cannot be removed."; return false; }
    auto before = snapshot();
    nodes.erase(nodes.begin() + i);
    touched();
    recordStep(std::move(before));
    return true;
}

bool InstrumentSceneDocument::updateNode(const InstrumentSceneNode& node, juce::String& error)
{
    const auto i = indexOf(node.id);
    if (i < 0) { error = "Unknown node: " + node.id; return false; }
    auto before = snapshot();
    nodes[static_cast<std::size_t>(i)] = node;
    rebuildTopology();
    if (! validate(error))
    {
        nodes = before.nodes;
        rebuildTopology();
        return false;
    }
    touched();
    recordStep(std::move(before));
    return true;
}

bool InstrumentSceneDocument::setVisible(const juce::String& id, bool visible, juce::String& error)
{
    const auto* n = findNode(id);
    if (n == nullptr) { error = "Unknown node."; return false; }
    auto copy = *n;
    copy.visible = visible;
    return updateNode(copy, error);
}

bool InstrumentSceneDocument::setOrder(const juce::String& id, int order, juce::String& error)
{
    const auto* n = findNode(id);
    if (n == nullptr) { error = "Unknown node."; return false; }
    auto copy = *n;
    copy.order = order;
    return updateNode(copy, error);
}

bool InstrumentSceneDocument::replaceFromJson(const juce::String& text, juce::String& error)
{
    auto before = snapshot();
    if (! loadJson(text, error)) { return false; }
    recordStep(std::move(before));
    return true;
}

void InstrumentSceneDocument::beginTransaction()
{
    if (transactionOpen) { return; }
    transactionStart = snapshot();
    transactionOpen = true;
    transactionChanged = false;
}

bool InstrumentSceneDocument::commitTransaction()
{
    if (! transactionOpen) { return false; }
    transactionOpen = false;
    if (! transactionChanged) { return false; }
    transactionChanged = false;
    recordStep(std::move(transactionStart));
    return true;
}

void InstrumentSceneDocument::cancelTransaction()
{
    if (! transactionOpen) { return; }
    transactionOpen = false;
    if (transactionChanged) { restore(transactionStart); }
    transactionChanged = false;
}

bool InstrumentSceneDocument::undo()
{
    if (transactionOpen || undoStack.empty()) { return false; }
    redoStack.push_back(snapshot());
    restore(undoStack.back());
    undoStack.pop_back();
    return true;
}

bool InstrumentSceneDocument::redo()
{
    if (transactionOpen || redoStack.empty()) { return false; }
    undoStack.push_back(snapshot());
    restore(redoStack.back());
    redoStack.pop_back();
    return true;
}
#endif
} // namespace px3::ui
