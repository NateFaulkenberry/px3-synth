#include "SettingsPanel.h"

#include "../DSP/AnalogEngine.h"
#include "UIConfig.h"

namespace
{
juce::Colour colourFrom(const UIConfig* config,
                        const juce::String& key,
                        juce::Colour fallback)
{
    return config != nullptr ? config->getColour(key, fallback) : fallback;
}

int intFrom(const UIConfig* config, const juce::String& key, int fallback)
{
    return config != nullptr ? config->getInt(key, fallback) : fallback;
}
} // namespace

SettingsPanel::SettingsPanel(PX3SynthAudioProcessor& processorIn, juce::Colour panelAccent)
    : processor(processorIn), accent(panelAccent)
{
    title.setText("SETTINGS", juce::dontSendNotification);
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);

    // ---- animations --------------------------------------------------------
    animationsToggle.setButtonText({});
    animationsToggle.setTooltip("Keyboard sparks, performance sparkles and the logo animation");
    animationsToggle.onClick = [this]
    {
        if (updatingFromProcessor) { return; }

        // To the global service, not to this processor: the preference belongs
        // to the install, and every other open window is listening to it.
        px3::GlobalSettings::getInstance().setAnimationsEnabled(
            animationsToggle.getToggleState());
    };
    addAndMakeVisible(animationsToggle);

    // ---- analog engine profile ---------------------------------------------
    analogProfileBox.setTooltip("The console colour applied to the whole output");
    analogProfileBox.addItemList(px3::AnalogEngine::profileNames(), 1);
    analogProfileBox.onChange = [this]
    {
        if (updatingFromProcessor) { return; }

        const auto chosen = analogProfileBox.getSelectedId() - 1;
        if (chosen < 0) { return; }

        // Through the parameter, not the engine. The parameter is what the
        // host automates and what preset and session state carry, and the
        // engine follows it every block - writing the engine directly would
        // last until the next block and then be overwritten.
        auto& parameter = processor.getAnalogProfileParam();
        parameter.beginChangeGesture();
        parameter.setValueNotifyingHost(
            parameter.convertTo0to1(static_cast<float>(chosen)));
        parameter.endChangeGesture();
    };
    addAndMakeVisible(analogProfileBox);

    const auto addRow = [this](const juce::String& caption,
                               const juce::String& help,
                               juce::Component& control)
    {
        auto row = std::make_unique<Row>();
        row->caption.setText(caption, juce::dontSendNotification);
        row->caption.setJustificationType(juce::Justification::centredLeft);
        row->help.setText(help, juce::dontSendNotification);
        row->help.setJustificationType(juce::Justification::centredLeft);
        row->control = &control;

        addAndMakeVisible(row->caption);
        addAndMakeVisible(row->help);
        rows.push_back(std::move(row));
    };

    addRow("Enable animations",
           "Keyboard sparks, performance sparkles and the logo. Kept per install, not per patch.",
           animationsToggle);
    addRow("Analog Engine",
           "Console colour on the output. Saved with the patch.",
           analogProfileBox);

    px3::GlobalSettings::getInstance().addChangeListener(this);
    refreshFromParameters();
}

SettingsPanel::~SettingsPanel()
{
    px3::GlobalSettings::getInstance().removeChangeListener(this);
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshFromParameters();
}

void SettingsPanel::setUIConfig(std::shared_ptr<const UIConfig> configIn)
{
    uiConfig = std::move(configIn);

    if (uiConfig != nullptr)
    {
        uiConfig->applyComboStyle(uiConfig->getObject("styles.combos.default"), analogProfileBox);
    }

    const auto captionColour = colourFrom(uiConfig.get(), "settings.colors.caption",
                                          juce::Colour::fromRGB(232, 236, 242));
    const auto helpColour = colourFrom(uiConfig.get(), "settings.colors.help",
                                       juce::Colour::fromRGB(150, 156, 166));

    title.setColour(juce::Label::textColourId, accent);
    title.setFont(juce::FontOptions(static_cast<float>(
        intFrom(uiConfig.get(), "settings.layout.titleFontSize", 15)), juce::Font::bold));

    for (auto& row : rows)
    {
        row->caption.setColour(juce::Label::textColourId, captionColour);
        row->caption.setFont(juce::FontOptions(static_cast<float>(
            intFrom(uiConfig.get(), "settings.layout.captionFontSize", 13))));
        row->help.setColour(juce::Label::textColourId, helpColour);
        row->help.setFont(juce::FontOptions(static_cast<float>(
            intFrom(uiConfig.get(), "settings.layout.helpFontSize", 11))));
    }

    animationsToggle.setColour(juce::ToggleButton::tickColourId, accent);
    animationsToggle.setColour(juce::ToggleButton::tickDisabledColourId,
                               colourFrom(uiConfig.get(), "settings.colors.tickOutline",
                                          juce::Colour::fromRGB(120, 126, 136)));

    resized();
    repaint();
}

void SettingsPanel::refreshFromParameters()
{
    // Guarded, so writing the controls here cannot be mistaken for the user
    // moving them and written straight back.
    const juce::ScopedValueSetter<bool> guard(updatingFromProcessor, true);

    animationsToggle.setToggleState(px3::GlobalSettings::getInstance().areAnimationsEnabled(),
                                    juce::dontSendNotification);

    const auto profile = processor.getAnalogProfileParam().getIndex();
    analogProfileBox.setSelectedId(profile + 1, juce::dontSendNotification);
}

void SettingsPanel::paint(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto radius = static_cast<float>(intFrom(uiConfig.get(), "settings.layout.cornerRadius", 8));

    g.setColour(colourFrom(uiConfig.get(), "settings.colors.background",
                           juce::Colour::fromRGBA(22, 24, 28, 190)));
    g.fillRoundedRectangle(area, radius);

    g.setColour(colourFrom(uiConfig.get(), "settings.colors.border",
                           juce::Colour::fromRGBA(255, 255, 255, 26)));
    g.drawRoundedRectangle(area.reduced(0.5f), radius, 1.0f);

    // A hairline under each row, which is what makes a form read as a list of
    // settings rather than a paragraph of controls.
    g.setColour(colourFrom(uiConfig.get(), "settings.colors.rowDivider",
                           juce::Colour::fromRGBA(255, 255, 255, 20)));

    for (std::size_t i = 0; i + 1 < rows.size(); ++i)
    {
        const auto& row = rows[i];
        const auto bottom = juce::jmax(row->help.getBottom(),
                                       row->control != nullptr ? row->control->getBottom() : 0);
        const auto padX = intFrom(uiConfig.get(), "settings.layout.padX", 18);
        g.fillRect(juce::Rectangle<int>(padX,
                                        bottom + intFrom(uiConfig.get(), "settings.layout.rowGap", 14) / 2,
                                        getWidth() - padX * 2,
                                        1));
    }
}

void SettingsPanel::layoutRow(Row& row, juce::Rectangle<int> area)
{
    const auto controlWidth = intFrom(uiConfig.get(), "settings.layout.controlWidth", 190);
    const auto helpHeight = intFrom(uiConfig.get(), "settings.layout.helpHeight", 16);
    const auto captionHeight = intFrom(uiConfig.get(), "settings.layout.captionHeight", 20);

    auto controlArea = area.removeFromRight(controlWidth);
    area.removeFromRight(intFrom(uiConfig.get(), "settings.layout.captionGap", 24));

    row.caption.setBounds(area.removeFromTop(captionHeight));
    row.help.setBounds(area.removeFromTop(helpHeight));

    if (row.control == nullptr) { return; }

    // The control is centred against the caption and its help text together,
    // so a one-line row and a two-line row both read as one row.
    const auto controlHeight = intFrom(uiConfig.get(), "settings.layout.controlHeight", 24);
    const auto height = juce::jmin(controlHeight, controlArea.getHeight());

    if (auto* toggle = dynamic_cast<juce::ToggleButton*>(row.control))
    {
        // A checkbox is a square, left-aligned in the control column: stretched
        // to the column's width its tick floats far from the box.
        const auto side = juce::jmin(height, controlArea.getHeight());
        toggle->setBounds(juce::Rectangle<int>(side, side)
                              .withCentre({ controlArea.getX() + side / 2,
                                            controlArea.getCentreY() }));
        return;
    }

    row.control->setBounds(controlArea.withSizeKeepingCentre(controlArea.getWidth(), height));
}

void SettingsPanel::resized()
{
    const auto padX = intFrom(uiConfig.get(), "settings.layout.padX", 18);
    const auto padY = intFrom(uiConfig.get(), "settings.layout.padY", 16);
    const auto rowGap = intFrom(uiConfig.get(), "settings.layout.rowGap", 14);
    const auto rowHeight = intFrom(uiConfig.get(), "settings.layout.rowHeight", 44);
    const auto titleHeight = intFrom(uiConfig.get(), "settings.layout.titleHeight", 24);

    auto area = getLocalBounds().reduced(padX, padY);
    title.setBounds(area.removeFromTop(titleHeight));
    area.removeFromTop(rowGap);

    for (auto& row : rows)
    {
        if (area.getHeight() <= 0) { break; }

        layoutRow(*row, area.removeFromTop(juce::jmin(rowHeight, area.getHeight())));
        area.removeFromTop(rowGap);
    }
}
