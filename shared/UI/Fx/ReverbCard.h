#pragma once

#include <JuceHeader.h>

#include "ComboSync.h"
#include "FxCardComponent.h"
#include "ReverbParameters.h"
#include "ReverbPresets.h"

#include <functional>

// The Reverb card's mode-dependent behaviour, shared by the Synth's FX page
// and PX3 Reverb so the two cannot drift: which per-type control the slot
// shows, which captions change meaning, which controls the type does not read
// (dimmed), and the PRESET menu - its items follow MODE, and it reads
// "Name*" once a knob has moved away from the preset.
namespace px3::ui::reverbCard
{
// Card knob id for each control, in px3::reverb::Control order.
inline constexpr const char* kKnobIds[px3::reverb::kControlCount] = {
    "preDelay", "decay", "size", "damping", "low", "diffusion", "modulation", "width", "early", "shimmer", "drip", "shape"
};

inline const char* slotMemberFor(int type)
{
    switch (type)
    {
        case px3::reverb::room:
        case px3::reverb::hall:   return "early";
        case px3::reverb::cloud:  return "shimmer";
        case px3::reverb::spring: return "drip";
        case px3::reverb::gated:  return "shape";
        default:                  return "";
    }
}

inline void syncControls(FxCardComponent& card, int type)
{
    using px3::reverb::Control;
    card.showSlotMember("typeSlot", slotMemberFor(type));
    for (int c = 0; c < px3::reverb::kControlCount; ++c)
        card.setKnobDimmed(kKnobIds[c], ! px3::reverb::isLive(static_cast<Control>(c), type));
    const auto gated = type == px3::reverb::gated;
    card.setKnobCaption("decay", gated ? "LENGTH" : "DECAY",
                        gated ? "GATED: how long the burst lasts before the gate closes" : "Reverberation time");
    const auto spring = type == px3::reverb::spring;
    card.setKnobCaption("diffusion", spring ? "SPLASH" : "DIFFUSION",
                        spring ? "SPRING: the high, splashy part of each echo" : "Grainy to smooth");
    card.setKnobCaption("size", spring ? "LENGTH" : "SIZE", spring ? "SPRING: spring length (transit time)" : "Size of the space");
}

// Rebuilds the PRESET items for `type` (only when the type changed), and
// shows the current selection: the preset's name, "Name*" once modified, or
// the placeholder. Never writes over a choice the user has just made but the
// change handler has not yet applied (see ComboSync.h).
inline void syncPresetMenu(juce::ComboBox& box, int type, const juce::String& selection,
                           const std::function<float(px3::reverb::Control)>& current)
{
    static const juce::Identifier typeKey { "px3ReverbPresetType" };
    auto& props = box.getProperties();
    if (box.isPopupActive()) { return; }
    const auto presets = px3::reverb::presetsForType(type);
    if (static_cast<int>(props.getWithDefault(typeKey, -1)) != type)
    {
        box.clear(juce::dontSendNotification);
        for (std::size_t i = 0; i < presets.size(); ++i) { box.addItem(presets[i].name, static_cast<int>(i) + 1); }
        box.setTextWhenNothingSelected("PRESET");
        props.set(typeKey, type);
        markComboSynced(box);
    }

    int id = 0;
    juce::String modifiedText;
    const auto prefix = juce::String(px3::reverb::kTypeNames[px3::reverb::clampType(type)]) + "/";
    if (selection.startsWith(prefix))
    {
        const auto name = selection.fromFirstOccurrenceOf("/", false, false);
        for (std::size_t i = 0; i < presets.size(); ++i)
        {
            if (name != presets[i].name) { continue; }
            if (px3::reverb::presetMatches(presets[i], type, current)) { id = static_cast<int>(i) + 1; }
            else { modifiedText = name + "*"; }
        }
    }

    if (modifiedText.isNotEmpty())
    {
        // Pending user choice? Leave it to the change handler.
        static const juce::Identifier syncedKey { "px3SyncedItemId" };
        const auto shown = box.getSelectedId();
        if (shown != 0 && shown != static_cast<int>(props.getWithDefault(syncedKey, 0))) { syncComboItemId(box, 0); return; }
        if (box.getText() != modifiedText) { box.setText(modifiedText, juce::dontSendNotification); }
        props.set(syncedKey, 0);
        return;
    }
    syncComboItemId(box, id);
    if (id == 0 && box.getText().isNotEmpty() && box.getSelectedId() == 0) { box.setText({}, juce::dontSendNotification); }
}

// Wires the PRESET menu: choosing an item applies that preset (message thread).
inline void wirePresetMenu(juce::ComboBox& box, std::function<int()> currentType,
                           std::function<void(int, const juce::String&)> apply)
{
    box.onChange = [&box, currentType, apply]
    {
        const auto id = box.getSelectedId();
        if (id <= 0) { return; }
        const auto type = currentType();
        const auto presets = px3::reverb::presetsForType(type);
        if (id - 1 < static_cast<int>(presets.size())) { apply(type, presets[static_cast<std::size_t>(id - 1)].name); }
        markComboSynced(box);
    };
}
} // namespace px3::ui::reverbCard
