#include "UILayoutEditing.h"

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)

#include <cmath>

namespace px3::ui::layoutedit
{
namespace
{
const InstrumentSceneNode& nodeAt(const InstrumentSceneDocument& d, int i)
{
    return d.getNodes()[static_cast<std::size_t>(i)];
}

// A new px value expressed in the unit the property already had.
Length keepUnit(const Length& original, float px, float reference)
{
    if (original.unit == Length::Unit::percent && reference > 0.0f)
    {
        return Length::percent(px / reference * 100.0f);
    }
    return Length::px(px);
}

float snapValue(float v, float snap) { return snap > 0.0f ? std::round(v / snap) * snap : v; }

juce::Rectangle<float> contentOf(const InstrumentSceneDocument& d, int parent)
{
    const auto& p = nodeAt(d, parent);
    const auto r = d.rectOf(parent);
    return juce::Rectangle<float>::leftTopRightBottom(r.getX() + p.padding.left, r.getY() + p.padding.top,
                                                      r.getRight() - p.padding.right, r.getBottom() - p.padding.bottom);
}

bool movesLeft(Handle h) { return h == Handle::left || h == Handle::topLeft || h == Handle::bottomLeft; }
bool movesRight(Handle h) { return h == Handle::right || h == Handle::topRight || h == Handle::bottomRight; }
bool movesTop(Handle h) { return h == Handle::top || h == Handle::topLeft || h == Handle::topRight; }
bool movesBottom(Handle h) { return h == Handle::bottom || h == Handle::bottomLeft || h == Handle::bottomRight; }

// The rect the user is asking for, edges snapped.
juce::Rectangle<float> desiredRect(const Drag& drag, juce::Point<float> mouse, float snap)
{
    const auto d = mouse - drag.startMouse;
    auto r = drag.startRect;
    if (drag.handle == Handle::move)
    {
        return r.withPosition(snapValue(r.getX() + d.x, snap), snapValue(r.getY() + d.y, snap));
    }
    auto l = r.getX(), t = r.getY(), rr = r.getRight(), b = r.getBottom();
    if (movesLeft(drag.handle)) { l = juce::jmin(rr - 1.0f, snapValue(l + d.x, snap)); }
    if (movesRight(drag.handle)) { rr = juce::jmax(l + 1.0f, snapValue(rr + d.x, snap)); }
    if (movesTop(drag.handle)) { t = juce::jmin(b - 1.0f, snapValue(t + d.y, snap)); }
    if (movesBottom(drag.handle)) { b = juce::jmax(t + 1.0f, snapValue(b + d.y, snap)); }
    return juce::Rectangle<float>::leftTopRightBottom(l, t, rr, b);
}

bool renumber(InstrumentSceneDocument& document, const std::vector<int>& sequence, juce::String& error)
{
    for (std::size_t k = 0; k < sequence.size(); ++k)
    {
        const auto& n = nodeAt(document, sequence[k]);
        if (n.order != static_cast<int>(k))
        {
            auto copy = n;
            copy.order = static_cast<int>(k);
            if (! document.updateNode(copy, error)) { return false; }
        }
    }
    return true;
}
}

Role roleOf(const InstrumentSceneDocument& document, int index)
{
    if (index < 0 || index == document.rootIndex()) { return Role::root; }
    const auto& n = nodeAt(document, index);
    if (n.position == LayoutPosition::absolute) { return Role::absolute; }
    const auto* parent = document.findNode(n.parentId);
    if (parent == nullptr) { return Role::root; }
    switch (parent->layout)
    {
        case InstrumentSceneLayoutMode::flex: return Role::flexChild;
        case InstrumentSceneLayoutMode::grid: return Role::gridChild;
        case InstrumentSceneLayoutMode::none: return Role::overlayChild;
    }
    return Role::overlayChild;
}

juce::String describe(Role role)
{
    switch (role)
    {
        case Role::root: return "root";
        case Role::absolute: return "absolute child";
        case Role::flexChild: return "flex child";
        case Role::gridChild: return "grid child";
        case Role::overlayChild: return "overlay child";
    }
    return {};
}

int hitTest(const InstrumentSceneDocument& document, juce::Point<float> point,
            const std::function<bool(int)>& eligible)
{
    auto best = -1;
    auto bestDepth = -1;
    const auto count = static_cast<int>(document.getNodes().size());
    for (int i = 0; i < count; ++i)
    {
        if (! document.isShown(i) || ! document.rectOf(i).contains(point)) { continue; }
        if (eligible && ! eligible(i)) { continue; }
        const auto depth = document.depthOf(i);
        if (depth >= bestDepth)
        {
            best = i;
            bestDepth = depth;
        }
    }
    return best;
}

Handle handleAt(juce::Rectangle<float> rect, juce::Point<float> p, float tol)
{
    if (! rect.expanded(tol).contains(p)) { return Handle::none; }
    const auto nearL = std::abs(p.x - rect.getX()) <= tol;
    const auto nearR = std::abs(p.x - rect.getRight()) <= tol;
    const auto nearT = std::abs(p.y - rect.getY()) <= tol;
    const auto nearB = std::abs(p.y - rect.getBottom()) <= tol;
    if (nearT && nearL) { return Handle::topLeft; }
    if (nearT && nearR) { return Handle::topRight; }
    if (nearB && nearL) { return Handle::bottomLeft; }
    if (nearB && nearR) { return Handle::bottomRight; }
    if (nearL) { return Handle::left; }
    if (nearR) { return Handle::right; }
    if (nearT) { return Handle::top; }
    if (nearB) { return Handle::bottom; }
    return rect.contains(p) ? Handle::move : Handle::none;
}

bool beginDrag(InstrumentSceneDocument& document, Drag& drag, const juce::String& id, Handle handle,
               juce::Point<float> mouse, juce::Rectangle<float> rootBounds)
{
    drag = {};
    const auto index = document.indexOf(id);
    if (index < 0 || handle == Handle::none) { return false; }
    const auto& n = nodeAt(document, index);
    const auto role = roleOf(document, index);
    if (n.locked || role == Role::root) { return false; }
    document.resolve(rootBounds);
    drag.id = id;
    drag.handle = handle;
    drag.role = role;
    drag.startMouse = mouse;
    drag.startRect = document.rectOf(index);
    drag.startNode = n;
    drag.rootBounds = rootBounds;
    drag.active = true;
    document.beginTransaction();
    return true;
}

bool dragTo(InstrumentSceneDocument& document, Drag& drag, juce::Point<float> mouse, float snap, juce::String& error)
{
    if (! drag.active) { return false; }
    const auto index = document.indexOf(drag.id);
    if (index < 0) { return false; }
    const auto parent = document.indexOf(drag.startNode.parentId);
    if (parent < 0) { return false; }

    // Every step starts from the node as it was when the drag began, so the
    // result depends only on where the mouse is now.
    auto node = drag.startNode;
    node.order = nodeAt(document, index).order;
    if (! document.updateNode(node, error)) { return false; }
    document.resolve(drag.rootBounds);

    const auto r = desiredRect(drag, mouse, snap);
    const auto parentRect = document.rectOf(parent);
    const auto content = contentOf(document, parent);
    const auto& p = nodeAt(document, parent);

    switch (drag.role)
    {
        case Role::root: return false;

        case Role::absolute:
        {
            const auto h = drag.handle;
            if (h == Handle::move || movesLeft(h))
            {
                node.x = keepUnit(node.x, r.getX() - parentRect.getX() - node.margin.left, parentRect.getWidth());
            }
            if (h == Handle::move || movesTop(h))
            {
                node.y = keepUnit(node.y, r.getY() - parentRect.getY() - node.margin.top, parentRect.getHeight());
            }
            if (h != Handle::move || node.width.isAuto())
            {
                if (h == Handle::move || movesLeft(h) || movesRight(h) || node.width.isAuto())
                {
                    node.width = keepUnit(node.width, r.getWidth(), parentRect.getWidth());
                }
            }
            if (h != Handle::move || node.height.isAuto())
            {
                if (h == Handle::move || movesTop(h) || movesBottom(h) || node.height.isAuto())
                {
                    node.height = keepUnit(node.height, r.getHeight(), parentRect.getHeight());
                }
            }
            return document.updateNode(node, error);
        }

        case Role::overlayChild:
        {
            if (drag.handle == Handle::move)
            {
                node.margin.left += r.getX() - drag.startRect.getX();
                node.margin.top += r.getY() - drag.startRect.getY();
            }
            else
            {
                if (movesLeft(drag.handle) || movesRight(drag.handle))
                {
                    node.width = keepUnit(node.width, r.getWidth(), content.getWidth());
                }
                if (movesTop(drag.handle) || movesBottom(drag.handle))
                {
                    node.height = keepUnit(node.height, r.getHeight(), content.getHeight());
                }
                if (movesLeft(drag.handle)) { node.margin.left += r.getX() - drag.startRect.getX(); }
                if (movesTop(drag.handle)) { node.margin.top += r.getY() - drag.startRect.getY(); }
            }
            return document.updateNode(node, error);
        }

        case Role::gridChild:
        {
            const auto cellW = drag.startRect.getWidth() / static_cast<float>(juce::jmax(1, node.columnSpan)) + p.gapX;
            const auto cellH = drag.startRect.getHeight() / static_cast<float>(juce::jmax(1, node.rowSpan)) + p.gapY;
            const auto colOf = [&](float x) { return juce::jmax(1, juce::roundToInt((x - content.getX()) / juce::jmax(1.0f, cellW)) + 1); };
            const auto rowOf = [&](float y) { return juce::jmax(1, juce::roundToInt((y - content.getY()) / juce::jmax(1.0f, cellH)) + 1); };
            if (drag.handle == Handle::move)
            {
                node.gridColumn = juce::jmin(colOf(r.getX()), static_cast<int>(p.gridColumns.size()));
                node.gridRow = rowOf(r.getY());
            }
            else
            {
                node.gridColumn = colOf(r.getX());
                node.gridRow = rowOf(r.getY());
                node.columnSpan = juce::jmax(1, juce::roundToInt((r.getWidth() + p.gapX) / juce::jmax(1.0f, cellW)));
                node.rowSpan = juce::jmax(1, juce::roundToInt((r.getHeight() + p.gapY) / juce::jmax(1.0f, cellH)));
            }
            return document.updateNode(node, error);
        }

        case Role::flexChild:
        {
            const auto isRow = p.direction == LayoutDirection::row;
            if (drag.handle == Handle::move)
            {
                // Reorder: the node goes after every shown sibling whose centre
                // the dragged centre has passed along the main axis.
                const auto centre = isRow ? r.getCentreX() : r.getCentreY();
                std::vector<int> others;
                auto insertAt = 0;
                auto passed = 0;
                for (auto c : document.childrenOf(parent))
                {
                    if (c == index) { continue; }
                    others.push_back(c);
                    const auto& k = nodeAt(document, c);
                    if (k.position == LayoutPosition::absolute || ! document.isShown(c)) { continue; }
                    const auto kr = document.rectOf(c);
                    if ((isRow ? kr.getCentreX() : kr.getCentreY()) < centre)
                    {
                        ++passed;
                        insertAt = static_cast<int>(others.size());
                    }
                }
                if (passed == 0) { insertAt = 0; }
                others.insert(others.begin() + insertAt, index);
                return renumber(document, others, error);
            }

            const auto mainSides = isRow ? (movesLeft(drag.handle) || movesRight(drag.handle))
                                         : (movesTop(drag.handle) || movesBottom(drag.handle));
            const auto crossSides = isRow ? (movesTop(drag.handle) || movesBottom(drag.handle))
                                          : (movesLeft(drag.handle) || movesRight(drag.handle));
            if (crossSides)
            {
                if (isRow) { node.height = keepUnit(node.height, r.getHeight(), content.getHeight()); }
                else { node.width = keepUnit(node.width, r.getWidth(), content.getWidth()); }
                if (! document.updateNode(node, error)) { return false; }
            }
            if (! mainSides) { return true; }

            // Solve for the basis that makes the RESOLVED main size the target.
            const auto target = isRow ? r.getWidth() : r.getHeight();
            const auto mainRef = isRow ? content.getWidth() : content.getHeight();
            const auto basisUnit = node.basis.isAuto() ? (isRow ? node.width : node.height) : node.basis;
            const auto measure = [&](float basisPx)
            {
                auto trial = node;
                trial.basis = keepUnit(basisUnit, juce::jmax(0.0f, basisPx), mainRef);
                if (! document.updateNode(trial, error)) { return -1.0f; }
                document.resolve(drag.rootBounds);
                node = trial;
                const auto rr = document.rectOf(index);
                return isRow ? rr.getWidth() : rr.getHeight();
            };

            auto b0 = basisUnit.resolve(mainRef, 0.0f);
            auto f0 = (isRow ? drag.startRect.getWidth() : drag.startRect.getHeight()) - target;
            auto b1 = juce::jmax(0.0f, b0 - f0);
            auto f1 = measure(b1) - target;
            for (int i = 0; i < 8 && std::abs(f1) > 0.5f; ++i)
            {
                const auto slope = (b1 != b0) ? (f1 - f0) / (b1 - b0) : 0.0f;
                if (std::abs(slope) < 0.05f) { break; }
                const auto b2 = juce::jmax(0.0f, b1 - f1 / slope);
                b0 = b1; f0 = f1;
                b1 = b2;
                f1 = measure(b1) - target;
            }
            if (std::abs(f1) > 0.5f)
            {
                // The node's grow decides its size (it is the only grower, say):
                // the basis alone cannot move it, so it stops growing.
                node.grow = 0.0f;
                f1 = measure(target) - target;
            }
            return true;
        }
    }
    return false;
}

void endDrag(InstrumentSceneDocument& document, Drag& drag, bool cancel)
{
    if (! drag.active) { return; }
    drag.active = false;
    if (cancel) { document.cancelTransaction(); }
    else { document.commitTransaction(); }
}

bool reorder(InstrumentSceneDocument& document, const juce::String& id, int direction, juce::String& error)
{
    const auto index = document.indexOf(id);
    if (index < 0 || index == document.rootIndex()) { return false; }
    const auto parent = document.indexOf(nodeAt(document, index).parentId);
    auto sequence = document.childrenOf(parent);
    const auto it = std::find(sequence.begin(), sequence.end(), index);
    const auto pos = static_cast<int>(it - sequence.begin());
    const auto target = pos + (direction < 0 ? -1 : 1);
    if (target < 0 || target >= static_cast<int>(sequence.size())) { return false; }
    std::swap(sequence[static_cast<std::size_t>(pos)], sequence[static_cast<std::size_t>(target)]);
    document.beginTransaction();
    const auto ok = renumber(document, sequence, error);
    if (ok) { document.commitTransaction(); }
    else { document.cancelTransaction(); }
    return ok;
}
}

#endif
