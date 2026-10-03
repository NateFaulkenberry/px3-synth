#pragma once

// Direct manipulation of the instrument scene: hit testing, and turning a
// drag of a handle into edits of the REAL layout properties of a node.
//
//  absolute child   move -> x/y, resize -> x/y/width/height (units kept)
//  flex child       move along the main axis -> order (reorder among siblings)
//                   resize along the main axis -> basis (solved so the RESOLVED
//                   size lands where the handle is, with grow kept when it can
//                   be; otherwise grow is set to 0)
//                   resize across -> width/height
//  grid child       move -> column/row, resize -> spans
//  overlay child    move -> margin, resize -> width/height
//
// Pure document logic, no components: the designer overlay drives it and the
// tests drive it the same way. Compiled for designer builds and tests only.

#include "UILayout.h"

#if PX3_UI_DESIGNER || defined(PX3_UNIT_TESTS)

#include <functional>

namespace px3::ui::layoutedit
{
enum class Handle { none, move, left, right, top, bottom, topLeft, topRight, bottomLeft, bottomRight };
enum class Role { root, absolute, flexChild, gridChild, overlayChild };

Role roleOf(const InstrumentSceneDocument& document, int index);
juce::String describe(Role role);

// The deepest shown node whose resolved rect contains `point`. Later siblings
// win ties (they are drawn on top). `eligible` may veto nodes (e.g. nodes whose
// component is not on screen). -1 if nothing.
int hitTest(const InstrumentSceneDocument& document,
            juce::Point<float> point,
            const std::function<bool(int)>& eligible = {});

// Which handle of `rect` is under `point`: corners, then edges, then the body.
Handle handleAt(juce::Rectangle<float> rect, juce::Point<float> point, float tolerance = 6.0f);

struct Drag
{
    juce::String id;
    Handle handle { Handle::none };
    Role role { Role::root };
    juce::Point<float> startMouse;
    juce::Rectangle<float> startRect;
    InstrumentSceneNode startNode;
    juce::Rectangle<float> rootBounds;
    bool active { false };
};

// Opens a transaction (one undo step for the whole drag). Fails for locked
// nodes and the root.
bool beginDrag(InstrumentSceneDocument& document, Drag& drag, const juce::String& id, Handle handle,
               juce::Point<float> mouse, juce::Rectangle<float> rootBounds);
// Applies the drag so far. `snap` > 0 snaps the dragged edges to that grid.
bool dragTo(InstrumentSceneDocument& document, Drag& drag, juce::Point<float> mouse, float snap,
            juce::String& error);
// Commits (or, with cancel, rolls back) the transaction.
void endDrag(InstrumentSceneDocument& document, Drag& drag, bool cancel = false);

// Moves a node one place earlier (-1) or later (+1) among its siblings.
bool reorder(InstrumentSceneDocument& document, const juce::String& id, int direction, juce::String& error);
}

#endif
