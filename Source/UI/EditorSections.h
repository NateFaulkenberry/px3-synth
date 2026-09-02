#pragma once

// Which panel is which, by index.
//
// These were file-local constants in PluginEditor.cpp, which was fine while
// the editor was one translation unit. It is not one any more: the section
// indices are used 39 times across the editor's implementation, by the timer,
// the layout, the top bar and the panel visibility alike, so every file split
// out of PluginEditor.cpp needs them.
//
// A header rather than a copy in each file. Two definitions of "MIX is 5" are
// two things that can disagree, and the failure would be a panel that shows
// the wrong contents rather than anything that fails to build.
//
// The static_asserts tying kSectionSettings to the processor's view count and
// to TopMenuBar stay in PluginEditor.cpp: they need both of those headers, and
// an assertion only has to hold in one place to hold everywhere.

namespace px3::ui
{

constexpr int kFxSectionDrive = 0;
constexpr int kFxSectionDelay = 1;
constexpr int kFxSectionReverb = 2;
constexpr int kFxSectionMood = 3;

constexpr int kSectionOsc = 0;
constexpr int kSectionMod = 1;
constexpr int kSectionAmp = 2;
constexpr int kSectionFilter = 3;
constexpr int kSectionFx = 4;
constexpr int kSectionMix = 5;

// A view like the six panels, but reached from its own button rather than the
// section row - and laid out full width, because the macro strip is a
// performance surface and SETTINGS has nothing to assign to a macro.
constexpr int kSectionSettings = 6;

} // namespace px3::ui
