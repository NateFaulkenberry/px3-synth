#pragma once

// Components register here against stable scene node ids; apply() hands each
// one its resolved rect. This is the ONLY place scene geometry turns into
// setBounds calls, in release and designer builds alike.

#include "UILayout.h"

#include <JuceHeader.h>

#include <vector>

namespace px3::ui
{
class SceneBinding final
{
public:
    enum class Visibility
    {
        // The owner decides whether the component is visible; the scene only
        // places it. Panels whose visibility is the selected section use this.
        ownerManaged,
        // As ownerManaged, AND the node collapses (takes no space) whenever the
        // owner hides the component - a mode that hides a knob closes the gap.
        followsComponent,
    };

    // Replaces any earlier binding for this id.
    void bind(const juce::String& nodeId, juce::Component& component,
              Visibility visibility = Visibility::ownerManaged);
    void unbind(const juce::String& nodeId);
    void clear() { entries.clear(); }

    juce::Component* componentFor(const juce::String& nodeId) const noexcept;
    juce::String nodeFor(const juce::Component* component) const;
    int size() const noexcept { return static_cast<int>(entries.size()); }

    // Mirrors followsComponent visibility into the document's runtime-hidden
    // set. Returns true if anything changed (the document must re-resolve).
    bool syncVisibility(InstrumentSceneDocument& document) const;

    // Resolves (cached) with the root at `root`'s local bounds and sets every
    // bound component's bounds, parents before children. Components whose node
    // is not shown keep their bounds. Re-entrant calls are ignored, so a
    // component's resized() may safely ask for a relayout.
    void apply(InstrumentSceneDocument& document, juce::Component& root);
    bool isApplying() const noexcept { return applying; }

private:
    struct Entry
    {
        juce::String id;
        juce::Component::SafePointer<juce::Component> component;
        Visibility visibility;
        int index { -1 };
        int depth { 0 };
    };

    std::vector<Entry> entries;
    std::uint64_t sortedForRevision { 0 };
    bool applying { false };
};

// Implemented by the editor. A scene-managed component calls this (via
// requestSceneLayout) instead of laying out its own children.
class SceneLayoutHost
{
public:
    virtual ~SceneLayoutHost() = default;
    virtual void sceneLayoutRequested() = 0;
};

// Asks the nearest SceneLayoutHost ancestor to relayout. Returns false when
// there is none (the component is not scene-managed and must lay itself out).
bool requestSceneLayout(juce::Component& from);
}
