#pragma once

// The instrument scene: one data-driven layout for the whole editor.
//
// A scene is a tree of nodes. Each node is a box; a container node lays out
// its children with a small flex/grid engine (a Yoga-like subset, see
// UILayout.cpp), and absolutely-positioned children are placed against the
// parent's box. Components register against node ids and take their bounds
// from the resolved rects - the editor has no layout arithmetic of its own for
// anything the scene describes.
//
// The document is pure data: it knows nothing about components or parameters.
// Changing a layout can therefore never change a sound - the scene lives in its
// own file (shared/UI/Style/InstrumentScene.json), separate from presets.
//
// Release builds only load and resolve. Editing (transactions, undo, the
// mutation API) is compiled for PX3_UI_DESIGNER builds and the tests only.

#include <JuceHeader.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace px3::ui
{
// A size along one axis. JSON: a number is px, "40%" is a percentage of the
// parent's content box on that axis, "1fr" is a grid fraction (grid templates
// only), "auto" defers to the engine.
struct Length
{
    enum class Unit : std::uint8_t { automatic, px, percent, fr };

    Unit unit { Unit::automatic };
    float value { 0.0f };

    static Length autoSize() noexcept { return {}; }
    static Length px(float v) noexcept { return { Unit::px, v }; }
    static Length percent(float v) noexcept { return { Unit::percent, v }; }
    static Length fr(float v) noexcept { return { Unit::fr, v }; }

    bool isAuto() const noexcept { return unit == Unit::automatic; }
    bool isDefinite() const noexcept { return unit == Unit::px || unit == Unit::percent; }
    // px or percent-of-reference; anything else yields `fallback`.
    float resolve(float reference, float fallback) const noexcept
    {
        if (unit == Unit::px) { return value; }
        if (unit == Unit::percent) { return reference * value * 0.01f; }
        return fallback;
    }

    juce::var toVar() const;
    static bool fromVar(const juce::var& value, Length& result, bool allowFr);
    juce::String toString() const;
    static bool fromString(const juce::String& text, Length& result, bool allowFr);

    bool operator==(const Length& other) const noexcept
    {
        return unit == other.unit && (unit == Unit::automatic || value == other.value);
    }
    bool operator!=(const Length& other) const noexcept { return ! (*this == other); }
};

// Insets in px, CSS order in JSON: [top, right, bottom, left] or one number.
struct Edges
{
    float top { 0.0f }, right { 0.0f }, bottom { 0.0f }, left { 0.0f };

    bool isZero() const noexcept { return top == 0.0f && right == 0.0f && bottom == 0.0f && left == 0.0f; }
    bool operator==(const Edges& o) const noexcept
    {
        return top == o.top && right == o.right && bottom == o.bottom && left == o.left;
    }
    bool operator!=(const Edges& o) const noexcept { return ! (*this == o); }
};

// What the node IS. Decides which properties the designer offers; the engine
// treats every kind the same.
enum class InstrumentSceneNodeKind
{
    container, // pure layout box
    panel,     // a section/panel
    card,      // a card inside a panel
    control,   // knob, slider, combo
    button,
    label,
    display,   // graphs, keyboard, meters
    decoration
};

// How a node lays out its RELATIVE children. `none` = leaf (children, if any,
// must be absolute).
enum class InstrumentSceneLayoutMode { none, flex, grid };
enum class LayoutDirection { row, column };
enum class LayoutAlign { automatic, start, center, end, stretch };
enum class LayoutJustify { start, center, end, spaceBetween, spaceAround, spaceEvenly };
enum class LayoutPosition { relative, absolute };

struct InstrumentSceneNode
{
    // identity
    juce::String id;
    juce::String parentId;
    InstrumentSceneNodeKind kind { InstrumentSceneNodeKind::container };
    juce::String label;
    juce::String bindingId;
    juce::String styleToken;
    int order { 0 };
    bool visible { true };
    bool locked { false };

    // as a container
    InstrumentSceneLayoutMode layout { InstrumentSceneLayoutMode::none };
    LayoutDirection direction { LayoutDirection::row };
    bool wrap { false };
    float gapX { 0.0f };
    float gapY { 0.0f };
    Edges padding;
    LayoutAlign alignItems { LayoutAlign::stretch };
    LayoutJustify justify { LayoutJustify::start };
    std::vector<Length> gridColumns; // px / % / fr / auto
    std::vector<Length> gridRows;    // empty = implicit equal rows

    // as a child
    LayoutPosition position { LayoutPosition::relative };
    Length x, y;          // absolute only, from the parent's box
    Length width, height; // preferred size (absolute: the size)
    Length basis;         // flex main-axis basis; auto = width/height
    float grow { 0.0f };
    float shrink { 1.0f };
    Length minWidth, minHeight, maxWidth, maxHeight;
    Edges margin;
    LayoutAlign alignSelf { LayoutAlign::automatic };
    int gridColumn { 0 }; // 1-based, 0 = auto placement
    int gridRow { 0 };
    int columnSpan { 1 };
    int rowSpan { 1 };
    // > 0: the final box is the largest rect of this width/height ratio that
    // fits the slot, centred. Knobs use 1.
    float aspect { 0.0f };
    // A flex/grid container whose children are all hidden normally collapses;
    // this keeps its box (a card row that is empty in some modes).
    bool keepEmpty { false };

    bool isContainer() const noexcept { return layout != InstrumentSceneLayoutMode::none; }
};

struct InstrumentSceneStyleToken
{
    juce::String id;
    juce::String background { "#171A1C" };
    juce::String foreground { "#E6E9E7" };
    juce::String accent { "#68A9C8" };
    juce::String texture { "none" };
    float knobDiameter { 54.0f };
    float labelSize { 11.0f };
    float borderRadius { 4.0f };
    float borderWidth { 1.0f };
};

class InstrumentSceneDocument final
{
public:
    static constexpr int currentSchemaVersion = 2;

    bool loadJson(const juce::String& text, juce::String& error);
    juce::String toJson() const;

    const InstrumentSceneNode* findNode(const juce::String& id) const noexcept;
    int indexOf(const juce::String& id) const noexcept;
    // The node and every ancestor are visible.
    bool isNodeVisible(const juce::String& id) const noexcept;
    const InstrumentSceneStyleToken* findStyleToken(const juce::String& id) const noexcept;
    const std::vector<InstrumentSceneNode>& getNodes() const noexcept { return nodes; }
    const std::vector<InstrumentSceneStyleToken>& getStyleTokens() const noexcept { return styleTokens; }
    // Children of node `index` in layout order (order, then document order).
    const std::vector<int>& childrenOf(int index) const noexcept;
    int rootIndex() const noexcept { return root; }
    int depthOf(int index) const noexcept;

    // Bumped by every change; resolve caches are keyed on it.
    std::uint64_t getRevision() const noexcept { return revision; }

    // Lays out the whole tree with the root at `rootBounds` and caches the
    // result until the bounds or the document change. Hidden nodes (and their
    // subtrees) resolve to an empty rect.
    void resolve(juce::Rectangle<float> rootBounds) const;
    // The cached rect for a node (call resolve first). Empty if unknown/hidden.
    juce::Rectangle<float> rectOf(int index) const noexcept;
    juce::Rectangle<float> rectOf(const juce::String& id) const noexcept { return rectOf(indexOf(id)); }
    // Convenience: resolve then look up.
    juce::Rectangle<float> resolveBounds(const juce::String& id, juce::Rectangle<float> rootBounds) const;
    // Runtime state the scene cannot know (which section is selected, which
    // controls a mode shows). Not saved, not undoable; hides like visible=false.
    bool setRuntimeHidden(const juce::String& id, bool hidden);
    bool isRuntimeHidden(const juce::String& id) const { return runtimeHidden.count(id) != 0; }
    // Effective visibility from the last resolve.
    bool isShown(int index) const noexcept;
    // How many times the cache missed - for tests that pin the caching.
    int getResolveCount() const noexcept { return resolveCount; }

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)
    bool addStyleToken(const InstrumentSceneStyleToken& token, juce::String& error);
    bool updateStyleToken(const InstrumentSceneStyleToken& token, juce::String& error);
    bool addNode(const InstrumentSceneNode& node, juce::String& error);
    bool removeNode(const juce::String& id, juce::String& error);
    // Replaces the node with the same id. The whole document is re-validated
    // and the edit is rolled back if it would make it invalid.
    bool updateNode(const InstrumentSceneNode& node, juce::String& error);
    bool setVisible(const juce::String& id, bool visible, juce::String& error);
    bool setOrder(const juce::String& id, int order, juce::String& error);
    // Load/reset as an undoable edit (loadJson itself is not undoable).
    bool replaceFromJson(const juce::String& text, juce::String& error);

    // Edits inside a transaction become ONE undo step (a drag). Edits outside
    // one each become their own step.
    void beginTransaction();
    bool commitTransaction();
    void cancelTransaction();
    bool isInTransaction() const noexcept { return transactionOpen; }
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    bool undo();
    bool redo();
#endif

private:
    struct Snapshot
    {
        std::vector<InstrumentSceneNode> nodes;
        std::vector<InstrumentSceneStyleToken> styles;
    };

    bool validate(juce::String& error) const;
    void rebuildTopology();
    void touched();

    void layoutChildren(int index, juce::Rectangle<float> box) const;
    void layoutFlex(int index, juce::Rectangle<float> content) const;
    void layoutGrid(int index, juce::Rectangle<float> content) const;
    void place(int index, juce::Rectangle<float> rect) const;

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)
    Snapshot snapshot() const { return { nodes, styleTokens }; }
    void restore(const Snapshot& s);
    void recordStep(Snapshot before);
#endif

    struct StringHash
    {
        std::size_t operator()(const juce::String& s) const noexcept { return static_cast<std::size_t>(s.hash()); }
    };

    std::vector<InstrumentSceneNode> nodes;
    std::vector<InstrumentSceneStyleToken> styleTokens;
    std::unordered_map<juce::String, int, StringHash> indexById;
    std::vector<std::vector<int>> children;
    std::vector<int> depths;
    std::vector<int> postOrder;
    std::unordered_set<juce::String, StringHash> runtimeHidden;
    int root { -1 };
    std::uint64_t revision { 1 };

    mutable std::vector<juce::Rectangle<float>> rects;
    mutable std::vector<char> shown;
    mutable juce::Rectangle<float> cachedRoot;
    mutable std::uint64_t cachedRevision { 0 };
    mutable int resolveCount { 0 };
    // Scratch reused across resolves so a resolve does not allocate once warm.
    struct FlexItemScratch
    {
        int index;
        float base, hypo, target, minMain, maxMain, crossSize, minCross, maxCross;
        float marginMainStart, marginMainEnd, marginCrossStart, marginCrossEnd;
        bool frozen;
        bool crossDefinite;
        int line;
    };
    mutable std::vector<FlexItemScratch> flexScratch;

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)
    bool transactionOpen { false };
    bool transactionChanged { false };
    Snapshot transactionStart;
    std::vector<Snapshot> undoStack;
    std::vector<Snapshot> redoStack;
#endif
};

juce::String toString(InstrumentSceneNodeKind);
juce::String toString(InstrumentSceneLayoutMode);
juce::String toString(LayoutDirection);
juce::String toString(LayoutAlign);
juce::String toString(LayoutJustify);
juce::String toString(LayoutPosition);
bool parse(const juce::String&, InstrumentSceneNodeKind&);
bool parse(const juce::String&, InstrumentSceneLayoutMode&);
bool parse(const juce::String&, LayoutDirection&);
bool parse(const juce::String&, LayoutAlign&);
bool parse(const juce::String&, LayoutJustify&);
bool parse(const juce::String&, LayoutPosition&);

// Rounds a resolved rect to pixels by EDGES, so adjacent boxes stay adjacent.
inline juce::Rectangle<int> snapToPixels(juce::Rectangle<float> r) noexcept
{
    const auto l = juce::roundToInt(r.getX());
    const auto t = juce::roundToInt(r.getY());
    return juce::Rectangle<int>::leftTopRightBottom(l, t,
                                                    juce::jmax(l, juce::roundToInt(r.getRight())),
                                                    juce::jmax(t, juce::roundToInt(r.getBottom())));
}
}
