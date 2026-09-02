// Presets: the browser's list, saving, loading, importing, exporting, and the
// dirty mark that says the sound no longer matches what it was loaded from.
//
// Split out of PluginEditor.cpp. These are member functions of the same class,
// so this needs no change to the header - PluginEditorLook.cpp and
// PluginEditorDebug.cpp work the same way.
//
// Three blocks rather than one, because the preset methods are not contiguous
// in the original: the bus-insert sheets and the top-menu section handling sit
// between them and belong to neither. Moving those too would have made this a
// bigger cut than it claims to be, so they stayed.
//
// getNumRows / paintListBoxItem / selectedRowsChanged are here rather than
// with the rest of the editor's overrides because they are the preset
// browser's list model and nothing else uses them.

#include "PluginEditor.h"
#include "EditorSections.h"
#include "ParameterKnob.h"
#include "KnobOverlays.h"
#include "Card.h"
#include "UIConfig.h"
#include "PX3Version.h"
#include "../DSP/PluginProcessorInternals.h"

#include <algorithm>
#include <cmath>

using namespace px3::ui;

void PX3SynthAudioProcessorEditor::rebuildPresetFilteredList()
{
    PresetManager::Query query;

    switch (presetScopeBox.getSelectedId())
    {
        case 2:
            query.includeFactory = true;
            query.includeUser = false;
            break;
        case 3:
            query.includeFactory = false;
            query.includeUser = true;
            break;
        case 4:
            query.favoritesOnly = true;
            break;
        default:
            break;
    }

    query.category = presetCategoryBox.getText();
    query.searchText = presetSearchEditor.getText();

    presetFiltered = presetManager.queryPresets(query);
    presetListBox.updateContent();
    presetListBox.repaint();

    if (hasCurrentPreset)
    {
        int row = -1;
        for (int i = 0; i < static_cast<int>(presetFiltered.size()); ++i)
        {
            if (presetFiltered[static_cast<std::size_t>(i)].file == currentPreset.file)
            {
                row = i;
                break;
            }
        }

        if (row >= 0)
        {
            presetListBox.selectRow(row);
        }
    }

    if (presetFiltered.empty())
    {
        presetBrowserDetails.setText("No presets match this filter.", juce::dontSendNotification);
    }
}

void PX3SynthAudioProcessorEditor::refreshPresetNameDisplay()
{
    juce::String name = hasCurrentPreset ? currentPreset.metadata.name
                                         : px3::processor_internal::kNoPresetLabel;
    if (currentPresetDirty)
    {
        name << "*";
    }

    if (topMenuBar != nullptr)
    {
        topMenuBar->setPresetName(name);

        // Blank for INIT, which is not a preset and has no category or author
        // to report - the tab then draws as a single centred name.
        topMenuBar->setPresetDetails(hasCurrentPreset ? currentPreset.metadata.category : juce::String(),
                                     hasCurrentPreset ? currentPreset.metadata.author : juce::String());
    }

    // Hand the identity to the processor so it survives in DAW state. The
    // editor is destroyed with the window; without this, reopening it showed
    // INIT and no category or author over a patch that had not changed.
    PX3SynthAudioProcessor::LoadedPreset published;
    if (hasCurrentPreset)
    {
        published.name = currentPreset.metadata.name;
        published.category = currentPreset.metadata.category;
        published.author = currentPreset.metadata.author;
        published.filePath = currentPreset.file.getFullPathName();
        published.valid = true;
    }
    audioProcessor.setLoadedPreset(published);
}

void PX3SynthAudioProcessorEditor::applyPresetRecord(const PresetManager::PresetRecord& record)
{
    juce::String error;
    if (!presetManager.loadPreset(record, error))
    {
        showPresetError("Preset Load Failed", error);
        return;
    }

    hasCurrentPreset = true;
    currentPreset = record;
    loadedStateHash = computeCurrentStateHash();
    currentPresetDirty = false;
    refreshPresetNameDisplay();
    repaint();
}

void PX3SynthAudioProcessorEditor::openPresetBrowser()
{
    presetBrowserBackdropSnapshot = createComponentSnapshot(getLocalBounds());
    presetBrowserVisible = true;
    presetBrowserDragging = false;
    presetBrowserScrim.setBounds(getLocalBounds());
    presetBrowserScrim.setVisible(true);
    presetBrowserScrim.setAlwaysOnTop(true);
    presetBrowserScrim.toFront(false);
    presetBrowserPanel.setVisible(true);
    presetBrowserPanel.setAlwaysOnTop(true);
    presetBrowserPanel.toFront(true);
    rebuildPresetFilteredList();
    repaint();
}

void PX3SynthAudioProcessorEditor::closePresetBrowser()
{
    presetBrowserVisible = false;
    presetBrowserDragging = false;
    presetBrowserPanel.setAlwaysOnTop(false);
    presetBrowserPanel.setVisible(false);
    presetBrowserScrim.setAlwaysOnTop(false);
    presetBrowserScrim.setVisible(false);
    presetBrowserBackdropSnapshot = {};
    repaint();
}
void PX3SynthAudioProcessorEditor::showPresetError(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                           title,
                                           message,
                                           "OK",
                                           this);
}

void PX3SynthAudioProcessorEditor::savePreset(bool saveAs)
{
    if (!saveAs && hasCurrentPreset && !currentPreset.isFactory)
    {
        PresetManager::PresetMetadata metadata = currentPreset.metadata;
        juce::String error;
        juce::File outFile;
        if (!presetManager.saveUserPreset(metadata, true, error, &outFile))
        {
            showPresetError("Save Failed", error);
            return;
        }

        presetManager.refreshIndex();
        rebuildPresetFilteredList();
        if (const auto* record = presetManager.findByFile(outFile))
        {
            currentPreset = *record;
            hasCurrentPreset = true;
            loadedStateHash = computeCurrentStateHash();
            currentPresetDirty = false;
            refreshPresetNameDisplay();
        }
        return;
    }

    auto initialCategory = hasCurrentPreset ? currentPreset.metadata.category : juce::String("LEADS");
    if (initialCategory.trim().isEmpty())
    {
        initialCategory = "LEADS";
    }

    auto defaultDir = presetManager.getUserPresetRootDir().getChildFile(initialCategory.toUpperCase());
    if (!defaultDir.exists())
    {
        defaultDir.createDirectory();
    }

    auto defaultName = hasCurrentPreset ? currentPreset.metadata.name : juce::String("New Preset");
    auto chooser = std::make_shared<juce::FileChooser>("Save P(X3) preset",
                                                        defaultDir.getChildFile(defaultName + ".px3preset"),
                                                        "*.px3preset");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             auto destination = fc.getResult();
                             if (destination == juce::File())
                             {
                                 return;
                             }

                             if (!destination.hasFileExtension(".px3preset"))
                             {
                                 destination = destination.withFileExtension(".px3preset");
                             }

                             PresetManager::PresetMetadata metadata;
                             metadata.name = destination.getFileNameWithoutExtension();
                             metadata.category = destination.getParentDirectory().getFileName();
                             metadata.author = hasCurrentPreset ? currentPreset.metadata.author : juce::String();
                             metadata.description = hasCurrentPreset ? currentPreset.metadata.description : juce::String();

                             bool overwrite = destination.existsAsFile();
                             if (overwrite)
                             {
                                 const auto proceed = juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                                                            "Overwrite Preset?",
                                                                                            "A preset with this name already exists. Overwrite it?",
                                                                                            "Overwrite",
                                                                                            "Cancel",
                                                                                            this,
                                                                                            nullptr);
                                 if (!proceed)
                                 {
                                     return;
                                 }
                             }

                             juce::String error;
                             juce::File outFile;
                             if (!presetManager.saveUserPreset(metadata, overwrite, error, &outFile))
                             {
                                 showPresetError("Save Failed", error);
                                 return;
                             }

                             presetManager.refreshIndex();
                             rebuildPresetFilteredList();
                             if (const auto* record = presetManager.findByFile(outFile))
                             {
                                 currentPreset = *record;
                                 hasCurrentPreset = true;
                                 loadedStateHash = computeCurrentStateHash();
                                 currentPresetDirty = false;
                                 refreshPresetNameDisplay();
                             }
                         });
}

void PX3SynthAudioProcessorEditor::importPreset()
{
    auto chooser = std::make_shared<juce::FileChooser>("Import P(X3) preset",
                                                        juce::File(),
                                                        "*.px3preset");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             const auto file = fc.getResult();
                             if (!file.existsAsFile())
                             {
                                 return;
                             }

                             PresetManager::PresetRecord imported;
                             juce::String error;
                             if (!presetManager.importPreset(file, error, &imported))
                             {
                                 showPresetError("Import Failed", error);
                                 return;
                             }

                             rebuildPresetFilteredList();
                             applyPresetRecord(imported);
                         });
}

void PX3SynthAudioProcessorEditor::exportCurrentPreset()
{
    if (!hasCurrentPreset)
    {
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>("Export P(X3) preset",
                                                        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                            .getChildFile(currentPreset.metadata.name + ".px3preset"),
                                                        "*.px3preset");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser](const juce::FileChooser& fc)
                         {
                             auto destination = fc.getResult();
                             if (destination == juce::File())
                             {
                                 return;
                             }

                             if (!destination.hasFileExtension(".px3preset"))
                             {
                                 destination = destination.withFileExtension(".px3preset");
                             }

                             juce::String error;
                             if (!presetManager.exportPreset(currentPreset, destination, error))
                             {
                                 showPresetError("Export Failed", error);
                             }
                         });
}

void PX3SynthAudioProcessorEditor::showPresetMenu()
{
    enum MenuItemId
    {
        save = 1,
        saveAs,
        favorite,
        import,
        exportPreset,
        debug,
        // Never dispatched: the item carrying it is disabled, so it is a label
        // in the menu rather than a command.
        versionInfo
    };

    juce::PopupMenu menu;
    menu.addItem(MenuItemId::save, "Save");
    menu.addItem(MenuItemId::saveAs, "Save As");
    menu.addSeparator();
    menu.addItem(MenuItemId::favorite,
                 "Add to Favorites",
                 hasCurrentPreset,
                 hasCurrentPreset && currentPreset.isFavorite);
    menu.addSeparator();
    menu.addItem(MenuItemId::import, "Import");
    menu.addItem(MenuItemId::exportPreset, "Export", hasCurrentPreset);
#if PX3_DEBUG_PANEL
    menu.addSeparator();
    menu.addItem(MenuItemId::debug, "Debug");

    // Disabled, so it reads as information rather than as something to click.
    // This is where the version lives now that the logo panel does not show it.
    menu.addSeparator();
    menu.addItem(MenuItemId::versionInfo, "P(X3) Synth v" + px3::version::string(), false, false);
#endif

    if (topMenuBar == nullptr)
    {
        return;
    }

    // The button wears its active face for as long as the menu is up. Without
    // it the strip gives no sign of where the open menu came from - it is the
    // one control here that opens something and then looks untouched.
    auto& menuButton = topMenuBar->getPresetMenuButton();
    menuButton.setToggleState(true, juce::dontSendNotification);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton),
                       [this](int result)
                       {
                           // Runs for every dismissal, including clicking away,
                           // where result is 0 - so the button always comes
                           // back rather than sticking on.
                           if (topMenuBar != nullptr)
                           {
                               topMenuBar->getPresetMenuButton()
                                   .setToggleState(false, juce::dontSendNotification);
                           }

                           switch (result)
                           {
                               case MenuItemId::save:
                                   savePreset(false);
                                   break;
                               case MenuItemId::saveAs:
                                   savePreset(true);
                                   break;
                               case MenuItemId::favorite:
                               {
                                   if (!hasCurrentPreset)
                                   {
                                       return;
                                   }

                                   const auto nextFavorite = !currentPreset.isFavorite;
                                   juce::String error;
                                   if (!presetManager.setFavorite(currentPreset, nextFavorite, error))
                                   {
                                       showPresetError("Favorite Failed", error);
                                       return;
                                   }

                                   presetManager.refreshIndex();
                                   rebuildPresetFilteredList();
                                   if (const auto* found = presetManager.findByFile(currentPreset.file))
                                   {
                                       currentPreset = *found;
                                       refreshPresetNameDisplay();
                                   }
                                   break;
                               }
                               case MenuItemId::import:
                                   importPreset();
                                   break;
                               case MenuItemId::exportPreset:
                                   exportCurrentPreset();
                                   break;
#if PX3_DEBUG_PANEL
                               case MenuItemId::debug:
                                   toggleDebugWindow();
                                   break;
#endif
                               default:
                                   break;
                           }
                       });
}
juce::String PX3SynthAudioProcessorEditor::computeCurrentStateHash() const
{
    auto state = audioProcessor.createPresetStateTree();
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        return {};
    }

    const auto text = xml->toString();
    return juce::String::toHexString(static_cast<juce::int64>(text.hashCode64()));
}

void PX3SynthAudioProcessorEditor::updatePresetDirtyState()
{
    if (!hasCurrentPreset)
    {
        return;
    }

    const auto currentHash = computeCurrentStateHash();
    const auto dirty = loadedStateHash.isNotEmpty() && currentHash != loadedStateHash;
    if (dirty != currentPresetDirty)
    {
        currentPresetDirty = dirty;
        refreshPresetNameDisplay();
    }
}

int PX3SynthAudioProcessorEditor::getNumRows()
{
    return static_cast<int>(presetFiltered.size());
}

void PX3SynthAudioProcessorEditor::paintListBoxItem(int rowNumber,
                                                        juce::Graphics& g,
                                                        int width,
                                                        int height,
                                                        bool rowIsSelected)
{
    g.fillAll(rowIsSelected ? juce::Colour::fromRGBA(76, 120, 184, 170)
                            : juce::Colour::fromRGBA(0, 0, 0, 0));

    if (rowNumber < 0 || rowNumber >= static_cast<int>(presetFiltered.size()))
    {
        return;
    }

    const auto& item = presetFiltered[static_cast<std::size_t>(rowNumber)];
    const auto favoritePrefix = item.isFavorite ? juce::String("★ ") : juce::String();
    const auto sourcePrefix = item.isFactory ? juce::String("[F] ") : juce::String("[U] ");

    g.setColour(juce::Colour::fromRGB(234, 234, 234));
    g.setFont(juce::FontOptions(12.5f));
    g.drawText(favoritePrefix + sourcePrefix + item.metadata.name,
               6,
               0,
               width - 8,
               height,
               juce::Justification::centredLeft,
               true);
}

void PX3SynthAudioProcessorEditor::selectedRowsChanged(int lastRowSelected)
{
    if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(presetFiltered.size()))
    {
        presetBrowserDetails.setText("", juce::dontSendNotification);
        return;
    }

    const auto& preset = presetFiltered[static_cast<std::size_t>(lastRowSelected)];
    juce::String details;
    details << "Name: " << preset.metadata.name << "\n";
    details << "Category: " << preset.metadata.category << "\n";
    details << "Source: " << (preset.isFactory ? "Factory" : "User") << "\n";
    if (preset.metadata.author.isNotEmpty())
    {
        details << "Author: " << preset.metadata.author << "\n";
    }
    if (preset.metadata.description.isNotEmpty())
    {
        details << "\n" << preset.metadata.description;
    }

    presetBrowserDetails.setText(details, juce::dontSendNotification);
}
