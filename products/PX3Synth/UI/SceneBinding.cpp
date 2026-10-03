#include "SceneBinding.h"

#include <algorithm>

namespace px3::ui
{
void SceneBinding::bind(const juce::String& nodeId, juce::Component& component, Visibility visibility)
{
    for (auto& e : entries)
    {
        if (e.id == nodeId)
        {
            e.component = &component;
            e.visibility = visibility;
            return;
        }
    }
    entries.push_back({ nodeId, &component, visibility });
    sortedForRevision = 0;
}

void SceneBinding::unbind(const juce::String& nodeId)
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&nodeId](const Entry& e) { return e.id == nodeId; }),
                  entries.end());
}

juce::Component* SceneBinding::componentFor(const juce::String& nodeId) const noexcept
{
    for (const auto& e : entries)
    {
        if (e.id == nodeId) { return e.component.getComponent(); }
    }
    return nullptr;
}

juce::String SceneBinding::nodeFor(const juce::Component* component) const
{
    for (const auto& e : entries)
    {
        if (e.component.getComponent() == component) { return e.id; }
    }
    return {};
}

bool SceneBinding::syncVisibility(InstrumentSceneDocument& document) const
{
    auto changed = false;
    for (const auto& e : entries)
    {
        if (e.visibility != Visibility::followsComponent) { continue; }
        auto* c = e.component.getComponent();
        changed = document.setRuntimeHidden(e.id, c == nullptr || ! c->isVisible()) || changed;
    }
    return changed;
}

void SceneBinding::apply(InstrumentSceneDocument& document, juce::Component& root)
{
    if (applying) { return; }
    const juce::ScopedValueSetter<bool> guard(applying, true);

    syncVisibility(document);
    document.resolve(root.getLocalBounds().toFloat());

    if (sortedForRevision != document.getRevision())
    {
        for (auto& e : entries)
        {
            e.index = document.indexOf(e.id);
            e.depth = document.depthOf(e.index);
        }
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry& a, const Entry& b) { return a.depth < b.depth; });
        sortedForRevision = document.getRevision();
    }

    for (const auto& e : entries)
    {
        auto* c = e.component.getComponent();
        if (c == nullptr || e.index < 0 || ! document.isShown(e.index)) { continue; }
        auto rect = snapToPixels(document.rectOf(e.index));
        if (auto* parent = c->getParentComponent(); parent != nullptr && parent != &root)
        {
            rect = rect - root.getLocalPoint(parent, juce::Point<int>());
        }
        c->setBounds(rect);
    }
}

bool requestSceneLayout(juce::Component& from)
{
    if (auto* host = from.findParentComponentOfClass<SceneLayoutHost>())
    {
        host->sceneLayoutRequested();
        return true;
    }
    return false;
}
}
