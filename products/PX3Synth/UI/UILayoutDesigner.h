#pragma once

// The live layout designer (PX3_UI_DESIGNER builds only; nothing here exists
// in a release binary).
//
//  - An overlay on the live editor: hover highlights the node under the
//    cursor, click selects the deepest node there, Alt/Cmd-click (or Esc)
//    selects the containing node. Drag the body to move, drag one of the 8
//    handles to resize. Every gesture edits the node's REAL layout properties
//    (see UILayoutEditing.h) and the editor relays out immediately; the frame
//    is always drawn from the resolved rect, i.e. where the component is.
//  - An inspector in a separate dev window showing only the properties that
//    apply to the selected node (container / flex child / grid child /
//    absolute child / control), kept in sync with direct manipulation.
//  - Undo/redo (Cmd-Z / Shift-Cmd-Z or buttons; a drag is one step), snap,
//    reorder, visible, lock, save, load and reset to the shipped default.

#include "UILayout.h"
#include "SceneBinding.h"
#include "UILayoutEditing.h"

#if PX3_UI_DESIGNER

#include <JuceHeader.h>

#include <functional>
#include <memory>

namespace px3::ui
{
class LayoutDesignerOverlay;
class LayoutInspectorWindow;

class LayoutDesigner final
{
public:
    struct Host
    {
        // Re-applies the scene to the editor (resolve + setBounds).
        std::function<void()> relayout;
        // The source-tree InstrumentScene.json, if found ("Save default").
        juce::File sourceFile;
        // The compiled-in shipped default ("Reset").
        juce::String defaultJson;
    };

    LayoutDesigner(juce::Component& editor, InstrumentSceneDocument& document, SceneBinding& binding, Host host);
    ~LayoutDesigner();

    void setActive(bool shouldBeActive);
    bool isActive() const noexcept { return active; }
    // Called at the end of every editor resized(): keep the overlay on top.
    void editorLaidOut();

    // Where "Save" writes and "Load" starts: the user's layout, never a preset.
    static juce::File userLayoutFile();

    // ---- used by the overlay and the inspector ----
    InstrumentSceneDocument& getDocument() noexcept { return document; }
    SceneBinding& getBinding() noexcept { return binding; }
    juce::Component& getEditor() noexcept { return editor; }
    int getSelected() const noexcept { return selected; }
    void select(int index);
    void selectParent();
    // After any edit: relayout the editor, repaint, resync the inspector.
    void documentChanged();
    void undo();
    void redo();
    void moveInOrder(int direction);
    void toggleVisible();
    void toggleLocked();
    void save();
    void saveAsDefault();
    void load();
    void reset();
    void setStatus(const juce::String& text);
    bool isEligible(int index) const;

    bool snapEnabled { true };
    float snapSize { 4.0f };
    bool editMode { true };
    void setEditMode(bool shouldEdit);

private:
    juce::Component& editor;
    InstrumentSceneDocument& document;
    SceneBinding& binding;
    Host host;
    bool active { false };
    int selected { -1 };
    std::unique_ptr<LayoutDesignerOverlay> overlay;
    std::unique_ptr<LayoutInspectorWindow> inspector;
    std::unique_ptr<juce::FileChooser> chooser;
};
}

#endif
