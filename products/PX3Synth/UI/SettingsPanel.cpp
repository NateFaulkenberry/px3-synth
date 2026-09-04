#include "SettingsPanel.h"

#include "AnalogEngine.h"
#include "UIConfig.h"
#include "PX3Version.h"

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
    animationsToggle.setTooltip("Display performance animations (global)");
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
    analogProfileBox.setTooltip("Console color applied to the whole output");
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
           "Display performance animations (global)",
           animationsToggle);
    addRow("Analog Engine",
           "Console color applied to the whole output",
           analogProfileBox);

    // ---- updates -----------------------------------------------------------
    //
    // Its own block rather than a settings row: a row is a caption and one
    // control, and this is a heading, a version, a status, release notes, a
    // progress bar and a button whose meaning changes with the state.
    px3::update::installDefaultConfiguration();

    updatesHeading.setText("UPDATES", juce::dontSendNotification);
    updatesHeading.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(updatesHeading);

    // The version comes from the build, never from a string kept here. One
    // source of truth, shared with the comparison, the installer and the logs.
    versionLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(versionLabel);

    updateStatus.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(updateStatus);

    releaseNotes.setJustificationType(juce::Justification::topLeft);
    addChildComponent(releaseNotes);

    downloadBar.setPercentageDisplay(true);
    addChildComponent(downloadBar);

    updateButton.onClick = [this] { onUpdateButtonClicked(); };
    addAndMakeVisible(updateButton);

    px3::update::UpdateService::getInstance().addChangeListener(this);
    refreshUpdateSection();

    closeButton.setButtonText("CLOSE");
    closeButton.onClick = [this]
    {
        if (onCloseRequested != nullptr) { onCloseRequested(); }
    };
    addAndMakeVisible(closeButton);

    px3::GlobalSettings::getInstance().addChangeListener(this);
    refreshFromParameters();
}

SettingsPanel::~SettingsPanel()
{
    px3::GlobalSettings::getInstance().removeChangeListener(this);
    // The service outlives every editor, so a listener left registered here is
    // a call into freed memory the next time an update check finishes.
    px3::update::UpdateService::getInstance().removeChangeListener(this);
}

void SettingsPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Two broadcasters reach this now: the global animation preference, and
    // the update service. They are told apart rather than both triggering a
    // full refresh, because the update section repaints and re-lays out.
    if (source == &px3::update::UpdateService::getInstance())
    {
        logUpdateStateIfChanged();
        refreshUpdateSection();
        return;
    }

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

    closeButton.setColour(juce::TextButton::buttonColourId,
                          colourFrom(uiConfig.get(), "settings.colors.closeButton",
                                     juce::Colour::fromRGBA(52, 56, 64, 235)));
    closeButton.setColour(juce::TextButton::textColourOffId,
                          colourFrom(uiConfig.get(), "settings.colors.closeButtonText",
                                     juce::Colour::fromRGB(232, 236, 242)));

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

void SettingsPanel::refreshUpdateSection()
{
    using namespace px3::update;

    auto& service = UpdateService::getInstance();
    const auto state = service.getState();
    const auto product = service.getProduct();
    const auto release = service.getAvailableRelease();

    versionLabel.setText(
        (product.displayName.isNotEmpty() ? product.displayName : juce::String("PX3 Synth"))
            + "    Version " + px3::version::string()
            + (service.isPreReleaseChannelEnabled() ? "    (pre-release channel)"
                                                    : juce::String()),
        juce::dontSendNotification);

    // One switch over the service's state, so this section cannot show
    // something the service is not doing. The button's meaning changes with
    // the state rather than there being four buttons that are mostly hidden.
    switch (state)
    {
        case UpdateState::checking:
            updateStatus.setText("Checking for updates...", juce::dontSendNotification);
            updateButton.setButtonText("Check for Updates");
            updateButton.setEnabled(false);
            break;

        case UpdateState::upToDate:
            updateStatus.setText("You're up to date", juce::dontSendNotification);
            updateButton.setButtonText("Check for Updates");
            updateButton.setEnabled(true);
            break;

        case UpdateState::updateAvailable:
            // A pre-release is labelled wherever it appears. Somebody on the
            // debug channel has opted into unfinished builds, and must never
            // have to work out which kind they are being offered.
            updateStatus.setText("PX3 Synth " + release.version.toString()
                                     + (release.looksLikePreRelease()
                                            ? "  -  PRE-RELEASE  -  is available"
                                            : " is available"),
                                 juce::dontSendNotification);
            updateButton.setButtonText(release.looksLikePreRelease() ? "Prepare Pre-Release"
                                                                     : "Prepare Update");
            updateButton.setEnabled(true);
            break;

        case UpdateState::downloading:
            updateStatus.setText("Downloading PX3 Synth " + release.version.toString(),
                                 juce::dontSendNotification);
            updateButton.setButtonText("Cancel");
            updateButton.setEnabled(true);
            break;

        case UpdateState::verifying:
            updateStatus.setText("Verifying download...", juce::dontSendNotification);
            updateButton.setButtonText("Cancel");
            updateButton.setEnabled(true);
            break;

        case UpdateState::readyToInstall:
            // The whole point of preparing: the user is told to carry on
            // working, not to close their DAW and go looking for an installer.
            updateStatus.setText("PX3 Synth " + release.version.toString()
                                     + " is ready. Quit your DAW when you're finished and "
                                       "PX3 will finish the update.",
                                 juce::dontSendNotification);
            updateButton.setButtonText("Install Now");
            updateButton.setEnabled(true);
            break;

        case UpdateState::installing:
            updateStatus.setText("Installing...", juce::dontSendNotification);
            updateButton.setEnabled(false);
            break;

        case UpdateState::updated:
            updateStatus.setText("Updated. Restart your host to load the new version.",
                                 juce::dontSendNotification);
            updateButton.setButtonText("Check for Updates");
            updateButton.setEnabled(true);
            break;

        case UpdateState::failed:
            // The sentence the service wrote for a person. The HTTP status and
            // the file path are in the log, which is where they are useful and
            // where they cannot alarm somebody who wanted a new version.
            updateStatus.setText(service.getErrorMessage(), juce::dontSendNotification);
            updateButton.setButtonText("Try Again");
            updateButton.setEnabled(true);
            break;

        case UpdateState::notConfigured:
            updateStatus.setText("Updates are not available in this build.",
                                 juce::dontSendNotification);
            updateButton.setButtonText("Check for Updates");
            updateButton.setEnabled(false);
            break;

        case UpdateState::idle:
        default:
            updateStatus.setText({}, juce::dontSendNotification);
            updateButton.setButtonText("Check for Updates");
            updateButton.setEnabled(true);
            break;
    }

    const auto showNotes = state == UpdateState::updateAvailable
                        && release.releaseNotes.isNotEmpty();
    releaseNotes.setText(showNotes ? release.releaseNotes.substring(0, 400) : juce::String(),
                         juce::dontSendNotification);
    releaseNotes.setVisible(showNotes);

    const auto downloading = state == UpdateState::downloading;
    downloadProgress = downloading ? static_cast<double>(service.getProgress()) : 0.0;
    downloadBar.setVisible(downloading);

    resized();
    repaint();
}

void SettingsPanel::logUpdateEvent(const juce::String& event, const juce::String& details)
{
    processor.debugLogEvent("UPDATE", event, details);
}

void SettingsPanel::logUpdateStateIfChanged()
{
    using namespace px3::update;

    auto& service = UpdateService::getInstance();
    const auto stateIndex = static_cast<int>(service.getState());

    if (stateIndex != lastLoggedUpdateState)
    {
        const auto error = service.getErrorMessage();
        logUpdateEvent("STATE",
                       "state=" + describe(service.getState())
                           + (error.isNotEmpty() ? " error=" + error : juce::String())
                           + " staged=" + (service.hasStagedUpdate() ? "yes" : "no"));
        lastLoggedUpdateState = stateIndex;
        lastLoggedProgressPercent = -1;
        return;
    }

    // Progress, in ten-percent steps. The service broadcasts far more often
    // than that, and a log that scrolls itself is a log nobody reads.
    if (service.getState() == UpdateState::downloading)
    {
        const auto percent = static_cast<int>(service.getProgress() * 100.0f) / 10 * 10;
        if (percent != lastLoggedProgressPercent)
        {
            logUpdateEvent("PROGRESS", "percent=" + juce::String(percent));
            lastLoggedProgressPercent = percent;
        }
    }
}

void SettingsPanel::onUpdateButtonClicked()
{
    using namespace px3::update;

    auto& service = UpdateService::getInstance();

    switch (service.getState())
    {
        case UpdateState::updateAvailable:
        {
            // What was on offer at the moment the button was pressed. When a
            // download fails, the useful question is which asset it was given -
            // the url and the checksum decide whether the failure was the
            // network, the wrong file, or a release published without a sum.
            const auto release = service.getAvailableRelease();
            logUpdateEvent("CLICK_PREPARE",
                           "version=" + release.version.toString()
                               + " file=" + release.installerFilename
                               + " archive=" + (release.installerIsArchive ? "yes" : "no")
                               + " prerelease=" + (release.isPreRelease ? "yes" : "no")
                               + " sha256=" + (release.sha256.isNotEmpty() ? release.sha256.substring(0, 12) + "..."
                                                                           : "(none published)")
                               + " url=" + release.downloadUrl.toString(false));
            service.prepareUpdate();
            break;
        }

        case UpdateState::downloading:
        case UpdateState::verifying:
            logUpdateEvent("CLICK_CANCEL", "state=" + describe(service.getState()));
            service.cancel();
            break;

        case UpdateState::readyToInstall:
        {
            // Hands the staged installer to the helper application, which is
            // what waits for the host to quit. Nothing destructive happens in
            // this process - it is a plugin inside somebody's DAW.
            //
            // Logged in detail because this is the step that has to survive the
            // host quitting: once the DAW goes, so does any chance of seeing
            // what happened. The file, its size and whether the helper was
            // found are the three things worth knowing afterwards.
            const auto installer = service.stagedInstaller();
            logUpdateEvent("CLICK_INSTALL",
                           "installer=" + installer.getFullPathName()
                               + " exists=" + (installer.existsAsFile() ? "yes" : "no")
                               + " bytes=" + juce::String(installer.existsAsFile() ? installer.getSize() : 0));

            const auto launched = service.launchInstaller();
            logUpdateEvent(launched ? "INSTALL_LAUNCHED" : "INSTALL_LAUNCH_FAILED",
                           launched ? "the helper was started; it waits for this host to quit"
                                    : "error=" + service.getErrorMessage());
            break;
        }

        default:
            logUpdateEvent("CLICK_CHECK", "state=" + describe(service.getState()));
            service.checkForUpdates(true);
            break;
    }
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

    // Along the bottom, before the rows are laid out into what is left, so it
    // stays put however many settings there are.
    {
        const auto closeWidth = intFrom(uiConfig.get(), "settings.layout.closeWidth", 120);
        const auto closeHeight = intFrom(uiConfig.get(), "settings.layout.closeHeight", 28);
        auto closeRow = area.removeFromBottom(juce::jmin(closeHeight, area.getHeight()));
        closeButton.setBounds(closeRow.removeFromLeft(juce::jmin(closeWidth, closeRow.getWidth())));
        area.removeFromBottom(rowGap);
    }

    for (auto& row : rows)
    {
        if (area.getHeight() <= 0) { break; }

        layoutRow(*row, area.removeFromTop(juce::jmin(rowHeight, area.getHeight())));
        area.removeFromTop(rowGap);
    }

    // ---- the updates block --------------------------------------------------
    //
    // Under the settings rows, in what is left. Laid out top-down and each
    // piece taking only what it needs, so a state that has no release notes or
    // no progress bar simply leaves that space to whatever is below it.
    if (area.getHeight() > 0)
    {
        const auto lineHeight = intFrom(uiConfig.get(), "settings.layout.updateLineHeight", 20);
        const auto headingHeight = intFrom(uiConfig.get(), "settings.layout.updateHeadingHeight", 22);
        const auto buttonWidth = intFrom(uiConfig.get(), "settings.layout.updateButtonWidth", 150);
        const auto buttonHeight = intFrom(uiConfig.get(), "settings.layout.updateButtonHeight", 26);

        area.removeFromTop(rowGap);
        updatesHeading.setBounds(area.removeFromTop(juce::jmin(headingHeight, area.getHeight())));
        versionLabel.setBounds(area.removeFromTop(juce::jmin(lineHeight, area.getHeight())));
        updateStatus.setBounds(area.removeFromTop(juce::jmin(lineHeight, area.getHeight())));

        if (releaseNotes.isVisible())
        {
            releaseNotes.setBounds(area.removeFromTop(juce::jmin(lineHeight * 3, area.getHeight())));
        }

        if (downloadBar.isVisible())
        {
            area.removeFromTop(4);
            downloadBar.setBounds(area.removeFromTop(juce::jmin(14, area.getHeight()))
                                      .removeFromLeft(juce::jmin(320, area.getWidth())));
        }

        area.removeFromTop(8);
        updateButton.setBounds(area.removeFromTop(juce::jmin(buttonHeight, area.getHeight()))
                                   .removeFromLeft(juce::jmin(buttonWidth, area.getWidth())));
    }
}
