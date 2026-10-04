#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace px3::ui
{
// Pushes a parameter's choice into a dropdown from a refresh tick without
// stepping on a choice the user has just made.
//
// Picking from a dropdown's menu selects the item at once but tells its
// listeners (the parameter attachment, an onChange) ASYNCHRONOUSLY. A refresh
// landing in between still reads the OLD parameter value; writing that back
// into the box meant the queued notification found the old item selected and
// did nothing - the click was lost. So a box showing a choice the refresh did
// not put there, and that the parameter has not caught up with, is left alone
// until the notification lands. After a short grace period it is resynced
// anyway, so a box can never drift from its parameter for good.
inline void syncComboItemId(juce::ComboBox& box, int itemId)
{
    static const juce::Identifier syncedKey { "px3SyncedItemId" };
    static const juce::Identifier pendingKey { "px3PendingSinceMs" };
    constexpr juce::uint32 graceMs = 250;

    auto& props = box.getProperties();
    const auto shown = box.getSelectedId();
    const auto synced = static_cast<int>(props.getWithDefault(syncedKey, shown));

    if (shown != synced && shown != itemId)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (! props.contains(pendingKey))
        {
            props.set(pendingKey, static_cast<juce::int64>(now));
            return;
        }
        const auto since = static_cast<juce::uint32>(static_cast<juce::int64>(props[pendingKey]));
        if (now - since < graceMs) { return; }
    }

    props.remove(pendingKey);
    props.set(syncedKey, itemId);
    if (shown != itemId) { box.setSelectedId(itemId, juce::dontSendNotification); }
}

// For a box whose own change handler writes its parameter: call it there,
// once the choice is applied. The choice is then the synced value at once, so
// a host or preset moving the parameter straight after the click is shown on
// the next sync instead of being held off as if the click were still pending.
inline void markComboSynced(juce::ComboBox& box)
{
    box.getProperties().set("px3SyncedItemId", box.getSelectedId());
    box.getProperties().remove("px3PendingSinceMs");
}

inline void syncComboItemIndex(juce::ComboBox& box, int index)
{
    syncComboItemId(box, box.getItemId(index));
}
} // namespace px3::ui
