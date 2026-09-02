#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

#include "../Core/GlobalSettings.h"
#include "PluginProcessor.h"

class UIConfig;

// The SETTINGS view.
//
// A panel like the others in every way but two: it is full width - the macro
// strip is a performance surface and there is nothing here to assign to a
// macro - and its contents are a form rather than an instrument. Rows of a
// label and a control, laid out and styled from UIConfig under
// "settings.<key>" the same way every other panel reads its own prefix.
class SettingsPanel final : public juce::Component,
                           private juce::ChangeListener
{
public:
    explicit SettingsPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent);
    ~SettingsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Pulls every control back into line with what the processor holds. Called
    // on the editor's refresh like the other panels, so a preset load or a
    // host automation move is reflected here without this panel having to
    // listen for anything itself.
    void refreshFromParameters();
    void setUIConfig(std::shared_ptr<const UIConfig> configIn);

    // Raised when the Close button is pressed. The panel does not know what
    // "closing" means - the editor decides which view to go back to - so it
    // asks rather than acting.
    std::function<void()> onCloseRequested;

    // For the tests: the two controls, by the setting they carry.
    juce::ToggleButton& debugAnimationsToggle() { return animationsToggle; }
    juce::ComboBox& debugAnalogProfileBox() { return analogProfileBox; }
    juce::TextButton& debugCloseButton() { return closeButton; }

private:
    struct Row
    {
        juce::Label caption;
        juce::Component* control { nullptr };
        juce::Label help;
    };

    void layoutRow(Row& row, juce::Rectangle<int> area);

    // The animation preference is global, so another window can move it while
    // this page is open. The checkbox follows rather than showing what it was
    // when the page was drawn.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    PX3SynthAudioProcessor& processor;
    juce::Colour accent;
    std::shared_ptr<const UIConfig> uiConfig;

    juce::Label title;
    std::vector<std::unique_ptr<Row>> rows;

    juce::ToggleButton animationsToggle;
    juce::ComboBox analogProfileBox;
    juce::TextButton closeButton { "CLOSE" };

    // Set while a refresh is writing the controls, so the change callbacks can
    // tell a user's edit from the panel catching up with the processor. Without
    // it, refreshing writes the value straight back and a preset load fights
    // whatever the user last touched.
    bool updatingFromProcessor { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPanel)
};
