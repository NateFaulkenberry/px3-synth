#pragma once

#include <JuceHeader.h>

// MOOD's WET TIME, WET MODIFY and LOOP MODIFY mean different things in each
// mode (docs/MOOD_DSP_DESIGN.md section 5), so their captions and tooltips
// follow the mode instead of reading "WET MOD" / "LOOP MOD" everywhere. Shared
// by the Synth and the standalone PX3 Mood so the two panels stay identical.
namespace px3::ui::moodCaptions
{
struct Meaning { const char* caption; const char* tip; };

inline constexpr Meaning wetTime[] = {
    { "DECAY", "REVERB: how long the wet tail lasts" },
    { "TIME", "DELAY: echo time" },
    { "LAG", "SLIP: how far behind the slipped voices trail" } };
inline constexpr Meaning wetModify[] = {
    { "SMEAR", "REVERB: diffusion - how smeared the tail is" },
    { "REPEATS", "DELAY: feedback, up to infinite at the top" },
    { "PITCH", "SLIP: pitch of the slipped voices, +/-24 semitones in steps" } };
inline constexpr Meaning loopModify[] = {
    { "SENS", "ENV: trigger sensitivity - higher catches quieter playing" },
    { "SPEED", "TAPE: playback speed, eight musical rates from 1/2x to 4x (and reversed)" },
    { "WALK", "STRETCH: grain drift, frozen at noon and walking either way" } };

inline void apply(juce::Label& label, juce::Slider& knob, const Meaning& meaning)
{
    if (label.getText() != meaning.caption) { label.setText(meaning.caption, juce::dontSendNotification); }
    label.setTooltip(meaning.tip);
    knob.setTooltip(meaning.tip);
}

// wetMode: 0 REVERB, 1 DELAY, 2 SLIP. loopMode: 0 ENV, 1 TAPE, 2 STRETCH.
inline void applyAll(int wetMode, int loopMode,
                     juce::Label& wetTimeLabel, juce::Slider& wetTimeKnob,
                     juce::Label& wetModifyLabel, juce::Slider& wetModifyKnob,
                     juce::Label& loopModifyLabel, juce::Slider& loopModifyKnob)
{
    const auto wet = static_cast<std::size_t>(juce::jlimit(0, 2, wetMode));
    const auto loop = static_cast<std::size_t>(juce::jlimit(0, 2, loopMode));
    apply(wetTimeLabel, wetTimeKnob, wetTime[wet]);
    apply(wetModifyLabel, wetModifyKnob, wetModify[wet]);
    apply(loopModifyLabel, loopModifyKnob, loopModify[loop]);
}
} // namespace px3::ui::moodCaptions
