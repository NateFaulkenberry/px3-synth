#pragma once

#include <JuceHeader.h>

namespace px3
{

// Preferences that belong to the INSTALL rather than to a plugin instance or to
// a patch.
//
// Every instance reads the same value and is told when it changes, so turning
// animations off in one window turns them off in all of them. Instances know
// about this service; they never know about each other, which is what keeps the
// relationship a star rather than a mesh.
//
// Deliberately NOT a parameter. A parameter is per instance, is written into
// presets, is restored by loading one, and is automatable by the host - all four
// of which are wrong for "does this editor animate on this machine".
//
// MESSAGE THREAD ONLY. Every consumer is a UI component and every write comes
// from a control, so ownership stays where the readers are; nothing here is
// touched from the audio thread, and nothing here may be. The value is an
// atomic anyway, so a stray read cannot tear.
class GlobalSettings final : public juce::ChangeBroadcaster
{
public:
    static GlobalSettings& getInstance();

    bool areAnimationsEnabled() const noexcept;

    // Broadcasts synchronously when the value actually changes. Synchronous
    // because every listener is on this thread already and "immediately" is the
    // requirement; a posted message would leave the other open editors a tick
    // behind for no benefit.
    void setAnimationsEnabled(bool shouldBeEnabled);

    // Where the preference is kept: beside the preset library and the wavetable
    // library, under the same P(X3) folder those already use.
    static juce::File settingsFile();

    // For the tests: point the service at a scratch file and reload from it.
    // Without this the suite would read and write the developer's own
    // preference file, which is somebody's real setting.
    static void debugUseSettingsFile(const juce::File& file);
    void debugReloadFromDisk();

private:
    GlobalSettings();

    void load();
    void save() const;

    // Empty means the real location. Only the tests set it.
    static juce::File& testFileOverride();

    std::atomic<bool> animationsEnabled { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlobalSettings)
};

} // namespace px3
