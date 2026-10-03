#pragma once

#include <JuceHeader.h>

#include "Theme.h"

#include <functional>
#include <memory>

// The Reverb card's IR loader: the loaded impulse response's name, LOAD IR...
// and CLEAR. Shown in the card's footer while the Reverb MODE is IR.
//
// It owns no audio state: loading and clearing go through the callbacks, which
// the editor points at the processor (whose loader validates on the message
// thread and hands the convolution its own thread). A failed load shows the
// processor's reason in place of the name, in the warning colour.
class ReverbIrStrip final : public juce::Component
{
public:
    std::function<juce::String(const juce::File&)> onLoad;   // empty on success, else the reason
    std::function<void()> onClear;
    std::function<juce::String()> currentName;

    ReverbIrStrip()
    {
        nameLabel.setJustificationType(juce::Justification::centredLeft);
        nameLabel.setFont(px3::ui::theme::font(px3::ui::theme::Type::value));
        nameLabel.setMinimumHorizontalScale(0.75f);
        addAndMakeVisible(nameLabel);

        loadButton.setButtonText("LOAD IR...");
        loadButton.setTooltip("Load an impulse response (WAV, AIFF or FLAC) for the IR mode");
        loadButton.onClick = [this] { chooseFile(); };
        addAndMakeVisible(loadButton);

        clearButton.setButtonText("CLEAR");
        clearButton.setTooltip("Unload the impulse response");
        clearButton.onClick = [this]
        {
            if (onClear != nullptr) { onClear(); }
            error.clear();
            refresh();
        };
        addAndMakeVisible(clearButton);
        refresh();
    }

    void refresh()
    {
        const auto name = currentName != nullptr ? currentName() : juce::String();
        const auto text = error.isNotEmpty() ? error : (name.isNotEmpty() ? name : juce::String("No IR loaded"));
        if (nameLabel.getText() != text) { nameLabel.setText(text, juce::dontSendNotification); }
        nameLabel.setTooltip(text);
        nameLabel.setColour(juce::Label::textColourId,
                            error.isNotEmpty() ? juce::Colour(0xffff8a6a)
                                               : (name.isNotEmpty() ? px3::ui::theme::colour::textValue
                                                                    : px3::ui::theme::colour::textSecondary));
        clearButton.setEnabled(name.isNotEmpty());
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto buttonWidth = juce::jmin(78, area.getWidth() / 3);
        clearButton.setBounds(area.removeFromRight(juce::jmin(52, buttonWidth)).reduced(1, 2));
        area.removeFromRight(4);
        loadButton.setBounds(area.removeFromRight(buttonWidth).reduced(1, 2));
        area.removeFromRight(4);
        nameLabel.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        px3::ui::theme::drawInset(g, nameLabel.getBounds().toFloat().expanded(2.0f, 0.0f).reduced(0.0f, 2.0f));
    }

    juce::TextButton& debugLoadButton() { return loadButton; }
    juce::TextButton& debugClearButton() { return clearButton; }
    juce::String debugNameText() const { return nameLabel.getText(); }
    // The load path the chooser takes, without the chooser.
    void debugLoad(const juce::File& file) { load(file); }

private:
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser>("Load impulse response",
                                                      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                                                      "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<ReverbIrStrip> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safe](const juce::FileChooser& fc)
                             {
                                 if (safe == nullptr) { return; }
                                 const auto file = fc.getResult();
                                 if (file.existsAsFile()) { safe->load(file); }
                             });
    }

    void load(const juce::File& file)
    {
        error = onLoad != nullptr ? onLoad(file) : juce::String("IR loading is unavailable");
        refresh();
    }

    juce::Label nameLabel;
    juce::TextButton loadButton;
    juce::TextButton clearButton;
    juce::String error;
    std::unique_ptr<juce::FileChooser> chooser;
};
